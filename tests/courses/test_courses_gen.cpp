// Course generation (determinism, golden hashes, arena bounds, physical feasibility) and course codes.

#include "tests/TestFramework.h"

#include <algorithm>
#include <cctype>

#include "core/modules/courses/CourseCode.h"
#include "core/modules/courses/CourseGen.h"

using namespace rlt;
using namespace rlt::courses;

namespace {

bool sameRings(const Course& a, const Course& b) {
    if (a.rings.size() != b.rings.size()) return false;
    for (size_t i = 0; i < a.rings.size(); ++i) {
        const Ring &x = a.rings[i], &y = b.rings[i];
        if (!(x.center == y.center) || !(x.normal == y.normal) || x.radius != y.radius || x.flags != y.flags) return false;
    }
    return a.start.carPos == b.start.carPos;
}

const CourseType kTypes[4] = {CourseType::Aerial, CourseType::GroundWall, CourseType::AirDribble, CourseType::DribbleGates};

struct BulkStats {
    int courses = 0, rings = 0, empty = 0, outOfBounds = 0, infeasible = 0, tooClose = 0, badStart = 0;
    size_t minCount = 1000, maxCount = 0;
};

void checkCourse(const Course& c, CourseType type, int dp, BulkStats& st) {
    ++st.courses;
    if (c.rings.empty()) {
        ++st.empty;
        return;
    }
    st.minCount = std::min(st.minCount, c.rings.size());
    st.maxCount = std::max(st.maxCount, c.rings.size());
    const double clr = type == CourseType::Aerial ? 150 : 120;
    if (std::fabs(c.start.carPos.x) > 3900 || std::fabs(c.start.carPos.y) > 4900) ++st.badStart;
    for (size_t i = 0; i < c.rings.size(); ++i) {
        const Ring& r = c.rings[i];
        ++st.rings;
        if (!ringInsideArena(r, clr - 1e-6)) ++st.outOfBounds;
        if (std::fabs(r.center.x) > phys::kArenaX || std::fabs(r.center.y) > phys::kArenaY || r.center.z < 0 ||
            r.center.z > phys::kCeilingZ || std::fabs(r.center.x) + std::fabs(r.center.y) > phys::kCornerSum)
            ++st.outOfBounds;
        if (i > 0 && (type == CourseType::Aerial || type == CourseType::AirDribble)) {
            const AirParams p = airParamsFor(type, dp / 100.0, c.seed);
            const double seg = length(r.center - c.rings[i - 1].center);
            const double thrust = requiredThrust(c.rings[i - 1].normal, r.normal, seg, p.vRef);
            if (thrust > p.eta * phys::kBoostAccelAir * 1.001 * 1.02) ++st.infeasible;  // 2 % slack: quantised chord
            for (size_t j = 0; j + 1 < i; ++j)
                if (length(r.center - c.rings[j].center) < p.minSep - 1e-6) ++st.tooClose;
        }
    }
}

} // namespace

TEST(courses_generator_is_deterministic) {
    for (auto t : kTypes) {
        const Course a = generateCourse(0x1234ABCDull, t, 50), b = generateCourse(0x1234ABCDull, t, 50);
        const Course c = generateCourse(0x1234ABCEull, t, 50);
        CHECK(!a.rings.empty());
        CHECK(sameRings(a, b));
        CHECK_EQ(courseHash(a), courseHash(b));
        CHECK(!sameRings(a, c));
    }
}

TEST(courses_generator_golden_hashes_match_reference) {
    // Same values as the research reference (rings_ref.hpp, generator v1): the port is bit-exact.
    const uint64_t golden[4] = {0xa4413aa955078c1aull, 0x4600526e4412f939ull, 0x936babe828857464ull, 0x463f523d9e05083dull};
    const size_t counts[4] = {12, 12, 7, 6};
    for (int i = 0; i < 4; ++i) {
        const Course g = generateCourse(42, kTypes[i], 50);
        CHECK_EQ(g.rings.size(), counts[i]);
        CHECK_MSG(courseHash(g) == golden[i], std::to_string(i));
    }
    const Course aerial = generateCourse(42, CourseType::Aerial, 50);
    CHECK_NEAR(aerial.estLength, 15441.0, 1.0);
    CHECK_NEAR(aerial.parTime, 14.36, 0.01);
    CHECK_NEAR(aerial.estBoost, 297.6, 0.1);
    CHECK_NEAR(aerial.start.carPos.x, 258.0, 0.5);
    CHECK_NEAR(aerial.start.carPos.y, -3577.0, 0.5);
}

TEST(courses_generator_bulk_inside_arena_and_feasible) {
    const int seeds = 120;
    const int diffs[5] = {0, 25, 50, 75, 100};
    for (int ti = 0; ti < 4; ++ti) {
        BulkStats st;
        for (int s = 0; s < seeds; ++s)
            for (int dp : diffs) {
                const uint64_t seed = splitmix64(static_cast<uint64_t>(s) * 977 + static_cast<uint64_t>(dp)) & 0x00FFFFFFFFFFFFFFULL;
                checkCourse(generateCourse(seed, kTypes[ti], dp), kTypes[ti], dp, st);
            }
        CHECK_MSG(st.empty == 0, std::to_string(ti));
        CHECK_MSG(st.outOfBounds == 0, std::to_string(ti));
        CHECK_MSG(st.infeasible == 0, std::to_string(ti));
        CHECK_MSG(st.tooClose == 0, std::to_string(ti));
        CHECK_MSG(st.badStart == 0, std::to_string(ti));
    }
}

TEST(courses_generator_difficulty_changes_course) {
    const Course easy = generateCourse(99, CourseType::Aerial, 0), hard = generateCourse(99, CourseType::Aerial, 100);
    CHECK(!easy.rings.empty() && !hard.rings.empty());
    if (easy.rings.empty() || hard.rings.empty()) return;
    CHECK(easy.rings.size() < hard.rings.size());
    CHECK(easy.rings[0].radius > hard.rings[0].radius);
    CHECK_EQ(easy.rings.size(), 8u);
    CHECK_EQ(hard.rings.size(), 16u);
    // Final flag on the last ring, checkpoints every 5th ring.
    CHECK(hard.rings.back().flags & kFlagFinal);
    CHECK(hard.rings[4].flags & kFlagCheckpoint);
    CHECK(!(hard.rings[3].flags & kFlagCheckpoint));
}

TEST(courses_generator_types_have_expected_shape) {
    const Course ad = generateCourse(7, CourseType::AirDribble, 40);
    CHECK(ad.ballCourse);
    CHECK(ad.start.hasBall);
    CHECK_NEAR(ad.objRadius, phys::kBallRadius, 1e-9);
    for (const auto& r : ad.rings) CHECK(r.normal.y > 0.3);  // air dribble courses head towards +Y
    const Course sl = generateCourse(7, CourseType::DribbleGates, 40);
    CHECK(sl.ballCourse && sl.start.ballOnRoof);
    for (const auto& r : sl.rings) {
        CHECK(r.shape == RingShape::Gate);
        CHECK_NEAR(r.center.z, 230.0, 1e-9);
    }
    const Course gw = generateCourse(7, CourseType::GroundWall, 80);
    bool surfaces = true;
    for (const auto& r : gw.rings) surfaces = surfaces && r.surface != Surface::Air;
    CHECK(surfaces);
    CHECK(!gw.ballCourse);
    const Course aer = generateCourse(7, CourseType::Aerial, 40);
    CHECK(!aer.ballCourse);
    CHECK_NEAR(aer.objRadius, phys::kCarPassRadius, 1e-9);
    CHECK(aer.parTime > 3.0 && aer.parTime < 60.0);
    // Start velocity points towards the first ring (horizontal part).
    const DVec3 toFirst = aer.rings[0].center - aer.start.carPos;
    CHECK(dot(DVec3{toFirst.x, toFirst.y, 0}, aer.start.carVel) > 0);
}

TEST(courses_generator_explicit_ring_count) {
    CHECK_EQ(seedRingCount(seedWithRingCount(0x123456789ABCull, 20)), 20);
    CHECK_EQ(seedWithRingCount(0x123456789ABCull, 0), 0x123456789ABCull);
    // Seeds without a count byte are untouched (reference courses keep their golden hash).
    CHECK_EQ(seedRingCount(42), 0);
    const int counts[3] = {5, 20, 30};
    for (int want : counts) {
        BulkStats st;
        for (int s = 0; s < 12; ++s) {
            const uint64_t seed = seedWithRingCount(splitmix64(static_cast<uint64_t>(s) + 77) & 0x00FFFFFFFFFFFFFFULL, want);
            for (int dp : {0, 50, 100}) {
                const Course c = generateCourse(seed, CourseType::Aerial, dp);
                checkCourse(c, CourseType::Aerial, dp, st);
                CHECK_EQ(static_cast<int>(c.rings.size()),
                         std::clamp(want, minRingCount(CourseType::Aerial), maxRingCount(CourseType::Aerial, dp / 100.0)));
            }
        }
        CHECK(st.empty == 0 && st.outOfBounds == 0 && st.infeasible == 0 && st.tooClose == 0);
    }
    // Ground course with 20 arches, air dribble with 9 rings (12 asked: clamped), slalom clamped to the field.
    BulkStats g;
    for (int s = 0; s < 10; ++s) {
        const Course c = generateCourse(seedWithRingCount(static_cast<uint64_t>(s) * 31 + 5, 20), CourseType::GroundWall, 50);
        checkCourse(c, CourseType::GroundWall, 50, g);
        CHECK_EQ(c.rings.size(), 20u);
        const Course d = generateCourse(seedWithRingCount(static_cast<uint64_t>(s) * 31 + 5, 12), CourseType::AirDribble, 50);
        checkCourse(d, CourseType::AirDribble, 50, g);
        CHECK_EQ(d.rings.size(), 9u);
        const Course sl = generateCourse(seedWithRingCount(static_cast<uint64_t>(s) * 31 + 5, 30), CourseType::DribbleGates, 0);
        CHECK(!sl.rings.empty() && sl.rings.size() <= 8u);
    }
    CHECK(g.empty == 0 && g.outOfBounds == 0 && g.infeasible == 0 && g.tooClose == 0);
    // Easy ground course asking for the maximum: never empty (cap measured per difficulty, auto fallback).
    for (int s = 0; s < 20; ++s) {
        const uint64_t seed = seedWithRingCount(splitmix64(static_cast<uint64_t>(s) + 900) & 0x00FFFFFFFFFFFFFFULL, 30);
        const Course c = generateCourse(seed, CourseType::GroundWall, 0);
        CHECK(!c.rings.empty());
        CHECK_EQ(c.seed, seed);
        CHECK(static_cast<int>(c.rings.size()) <= maxRingCount(CourseType::GroundWall, 0.0));
    }
}

TEST(courses_code_roundtrip) {
    const uint64_t seeds[4] = {0ull, 1ull, 0xFFFFFFFFFFFFFFFFull, 0x0123456789ABCDEFull};
    for (uint64_t s : seeds)
        for (int t = 0; t < 4; ++t)
            for (int d : {0, 7, 50, 100}) {
                const std::string code = encodeCourseCode(s, static_cast<CourseType>(t), d);
                CHECK_EQ(code.size(), kCodeLength);
                const auto dec = decodeCourseCode(code);
                CHECK(dec.has_value());
                if (dec) {
                    CHECK_EQ(dec->seed, s);
                    CHECK(static_cast<int>(dec->type) == t);
                    CHECK_EQ(dec->difficultyPct, d);
                }
            }
    CHECK_EQ(encodeCourseCode(42, CourseType::Aerial, 50).substr(0, 24), std::string("RLT1-A050-000000000001A-"));
    CHECK_EQ(encodeCourseCode(42, CourseType::Aerial, 150), encodeCourseCode(42, CourseType::Aerial, 100));  // clamped
}

TEST(courses_code_rejects_typos_and_garbage) {
    const std::string code = encodeCourseCode(42, CourseType::Aerial, 50);
    std::string typo = code;
    typo[15] = (typo[15] == '7') ? '8' : '7';
    CHECK(!decodeCourseCode(typo).has_value());  // the check character catches a typo
    std::string typeTypo = code;
    typeTypo[5] = 'D';
    CHECK(!decodeCourseCode(typeTypo).has_value());
    std::string lowerCase = code;
    for (char& ch : lowerCase) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    CHECK(!decodeCourseCode(lowerCase).has_value());  // strict decoder...
    CHECK(parseCourseCode(lowerCase).has_value());    // ...but user input is normalised
    CHECK(parseCourseCode("  " + code + " \n").has_value());
    CHECK(!decodeCourseCode("").has_value());
    CHECK(!parseCourseCode("hello").has_value());
    CHECK(!parseCourseCode(code + "X").has_value());
    CHECK(!decodeCourseCode("RLT2" + code.substr(4)).has_value());  // other generator version
    CHECK(!parseCourseCode(std::string(500, 'A')).has_value());
    // No check character can make an out-of-range difficulty or a 65-bit seed valid.
    bool anyDiff = false, anySeed = false;
    const std::string b32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (char c : b32) {
        anyDiff = anyDiff || decodeCourseCode(std::string("RLT1-A101-000000000001A-") + c).has_value();
        anySeed = anySeed || decodeCourseCode(std::string("RLT1-A050-Z00000000001A-") + c).has_value();
    }
    CHECK(!anyDiff);
    CHECK(!anySeed);
}

TEST(courses_code_check_catches_every_single_substitution_and_swap) {
    const std::string b32 = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    int undetected = 0, tried = 0, swaps = 0, swapMissed = 0;
    for (uint64_t seed : {42ull, 0x0123456789ABCDEFull, 0x00FEDCBA98765432ull}) {
        const std::string code = encodeCourseCode(seed, CourseType::GroundWall, 37);
        for (size_t i = 5; i < kCodeLength; ++i) {
            if (i == 9 || i == 23) continue;
            for (char c : b32) {
                if (c == code[i]) continue;
                std::string t = code;
                t[i] = c;
                ++tried;
                const bool zeroZ = (c == '0' && code[i] == 'Z') || (c == 'Z' && code[i] == '0');
                if (decodeCourseCode(t).has_value() && !zeroZ) ++undetected;
            }
            if (i + 1 < 23 && i != 8 && code[i] != code[i + 1]) {
                std::string t = code;
                std::swap(t[i], t[i + 1]);
                ++swaps;
                if (decodeCourseCode(t).has_value()) ++swapMissed;
            }
        }
    }
    CHECK(tried > 1000);
    CHECK_EQ(undetected, 0);
    CHECK(swaps > 10);
    CHECK_EQ(swapMissed, 0);
}

TEST(courses_code_lookalike_characters) {
    // Seed and check characters typed with O / I / L instead of 0 / 1 still decode to the same course.
    const std::string code = encodeCourseCode(0x10000000001ull, CourseType::GroundWall, 33);
    std::string fuzzy = code;
    for (size_t i = 10; i < 23; ++i) {
        if (fuzzy[i] == '0') fuzzy[i] = 'O';
        else if (fuzzy[i] == '1') fuzzy[i] = (i % 2) ? 'I' : 'l';
    }
    CHECK(fuzzy != code);
    const auto dec = parseCourseCode(fuzzy);
    CHECK(dec.has_value());
    if (dec) CHECK_EQ(dec->seed, 0x10000000001ull);
    CHECK_EQ(normalizeCourseCode(fuzzy), code);
}
