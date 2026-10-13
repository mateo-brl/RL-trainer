// Spawn table, mirroring, paths, pads, benchmarks, ghost model, 50/50 resolver and ball impulses.
#include "tests/TestFramework.h"
#include "tests/kickoff/KickoffTestUtil.h"

using namespace rlt;
using namespace kot;

TEST(ko_spawn_table_mirror_paths) {
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const KickoffSpawn sp = kickoffSpawnFromIndex(i);
        const KickoffPose b = kickoffPose(sp, KickoffTeam::Blue), o = kickoffPose(sp, KickoffTeam::Orange);
        const KickoffPath pb = kickoffPath(sp, KickoffTeam::Blue), po = kickoffPath(sp, KickoffTeam::Orange);
        CHECK_NEAR(o.pos.x, -b.pos.x, 1e-3);  // orange = point reflection
        CHECK_NEAR(o.pos.y, -b.pos.y, 1e-3);
        CHECK_NEAR(std::fabs(wrapPi(o.yawRad - b.yawRad)), kPi, 1e-4);
        CHECK_NEAR(pb.length, po.length, 1e-2);
        CHECK(dot(pb.dir, normalize(flat(Vec3{} - b.pos))) > 0.9999f);
        CHECK_NEAR(b.pos.z, kKoCarPlaceZ, 1e-6);
        // The spawn heading is NOT aimed at the ball (6.34 deg diagonal, 3.81 deg off-centre).
        const Vec3 fwd{std::cos(b.yawRad), std::sin(b.yawRad), 0.f};
        const float headingErr = std::acos(clampf(dot(fwd, pb.dir), -1.f, 1.f)) * kRadToDeg;
        const KickoffSpawnKind k = kickoffKind(sp);
        const float expect = k == KickoffSpawnKind::Diagonal ? 6.34f : (k == KickoffSpawnKind::OffCenter ? 3.81f : 0.f);
        CHECK_NEAR(headingErr, expect, 0.03);
        // The car basis from the rotator agrees with the spawn yaw.
        CHECK(dot(basisFromRot(b.rot).fwd, fwd) > 0.9999f);
        // Right of the path is the car's right (left-handed frame).
        CHECK(dot(pb.right, basisFromRot(rotFromDirection(pb.dir)).right) > 0.9999f);
    }
    CHECK_EQ(kickoffPose(KickoffSpawn::DiagonalRight, KickoffTeam::Blue).rot.yaw, 8192);
    CHECK_EQ(kickoffPose(KickoffSpawn::DiagonalLeft, KickoffTeam::Blue).rot.yaw, 24576);
    CHECK_EQ(kickoffPose(KickoffSpawn::BackCenter, KickoffTeam::Orange).rot.yaw, -16384);
    CHECK_EQ(kickoffPose(KickoffSpawn::DiagonalLeft, KickoffTeam::Orange).rot.yaw, -8192);
    CHECK_EQ(kickoffPose(KickoffSpawn::DiagonalRight, KickoffTeam::Orange).rot.yaw, -24576);
    // Blue diagonal right is at -X (the right side as seen from blue).
    CHECK(kickoffPose(KickoffSpawn::DiagonalRight, KickoffTeam::Blue).pos.x < 0.f);
    CHECK(kickoffPose(KickoffSpawn::OffCenterLeft, KickoffTeam::Blue).pos.x > 0.f);
    CHECK_NEAR(kickoffPath(KickoffSpawn::DiagonalRight, KickoffTeam::Blue).length, 3120.f, 2.f);
    CHECK_NEAR(kickoffPath(KickoffSpawn::OffCenterLeft, KickoffTeam::Blue).length, 3690.5f, 2.f);
    CHECK_NEAR(kickoffPath(KickoffSpawn::BackCenter, KickoffTeam::Blue).length, 4450.f, 1.f);
}

TEST(ko_pads_on_path) {
    const auto diag = kickoffPadsOnPath(kickoffPath(KickoffSpawn::DiagonalRight, KickoffTeam::Blue));
    CHECK_EQ(diag.size(), size_t(1));
    if (!diag.empty()) {
        CHECK_NEAR(diag[0].along, 365.f, 5.f);
        CHECK_NEAR(diag[0].amount, 12.f, 1e-6);
    }
    CHECK_EQ(kickoffPadsOnPath(kickoffPath(KickoffSpawn::BackCenter, KickoffTeam::Blue)).size(), size_t(3));
    CHECK_EQ(kickoffPadsOnPath(kickoffPath(KickoffSpawn::OffCenterLeft, KickoffTeam::Orange)).size(), size_t(1));
    CHECK_EQ(kickoffSoccarPads().size(), size_t(34));
}

TEST(ko_spawn_keys_and_find) {
    KickoffSpawn s = KickoffSpawn::BackCenter;
    CHECK(kickoffSpawnFromKey("diagonal_left", s) && s == KickoffSpawn::DiagonalLeft);
    CHECK(kickoffSpawnFromKey("DIAGONAL_RIGHT", s) && s == KickoffSpawn::DiagonalRight);
    CHECK(kickoffSpawnFromKey("offcenter_left", s) && s == KickoffSpawn::OffCenterLeft);
    CHECK(kickoffSpawnFromKey("offcenter_right", s) && s == KickoffSpawn::OffCenterRight);
    CHECK(kickoffSpawnFromKey("back", s) && s == KickoffSpawn::BackCenter);
    CHECK(!kickoffSpawnFromKey("middle", s));
    CHECK(kickoffSpawnFromIndex(-3) == KickoffSpawn::DiagonalLeft);
    CHECK(kickoffSpawnFromIndex(99) == KickoffSpawn::BackCenter);
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < kKoSpawnCount; ++i) {
            const KickoffPose p = kickoffPose(kickoffSpawnFromIndex(i), static_cast<KickoffTeam>(t), kKoCarRestZ);
            KickoffSpawn fs;
            KickoffTeam ft;
            CHECK(kickoffFindSpawn(p.pos + Vec3{20.f, -15.f, 0.f}, 60.f, fs, ft));
            CHECK(fs == kickoffSpawnFromIndex(i) && ft == static_cast<KickoffTeam>(t));
        }
    KickoffSpawn fs;
    KickoffTeam ft;
    CHECK(!kickoffFindSpawn({1000.f, 1000.f, 17.f}, 60.f, fs, ft));
    CHECK(std::string(kickoffKindSkill(KickoffSpawnKind::Diagonal)) == "diagonal");
    CHECK(std::string(kickoffKindSkill(KickoffSpawnKind::OffCenter)) == "decale");
    CHECK(std::string(kickoffKindSkill(KickoffSpawnKind::BackCenter)) == "centre");
}

TEST(ko_benchmarks_and_interpolation) {
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::DiagonalLeft, KickoffLevel::Pro).arrive, 1.95f, 1e-6);
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::OffCenterRight, KickoffLevel::Pro).arrive, 2.20f, 1e-6);
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::BackCenter, KickoffLevel::Pro).arrive, 2.48f, 1e-6);
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::DiagonalLeft, KickoffLevel::Standard).arrive, 2.07f, 1e-6);
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::BackCenter, KickoffLevel::Beginner).arrive, 2.95f, 1e-6);
    CHECK_NEAR(kickoffBenchmark(KickoffSpawn::DiagonalLeft, KickoffLevel::Elite).arrive, 1.88f, 1e-6);
    for (int k = 0; k < 3; ++k) {
        const auto kind = static_cast<KickoffSpawnKind>(k);
        // Faster level = earlier arrival, higher contact speed.
        for (int l = 1; l < kKoLevelCount; ++l) {
            CHECK(kickoffBenchmark(kind, kickoffLevelFromIndex(l)).arrive <
                  kickoffBenchmark(kind, kickoffLevelFromIndex(l - 1)).arrive);
            CHECK(kickoffBenchmark(kind, kickoffLevelFromIndex(l)).contactSpeed >=
                  kickoffBenchmark(kind, kickoffLevelFromIndex(l - 1)).contactSpeed);
        }
        CHECK_NEAR(kickoffBenchmarkAt(kind, 0.f).arrive, kickoffBenchmark(kind, KickoffLevel::Beginner).arrive, 1e-5);
        CHECK_NEAR(kickoffBenchmarkAt(kind, 2.f / 3.f).arrive, kickoffBenchmark(kind, KickoffLevel::Pro).arrive, 1e-4);
        CHECK_NEAR(kickoffBenchmarkAt(kind, 1.f).arrive, kickoffBenchmark(kind, KickoffLevel::Elite).arrive, 1e-5);
        const float mid = kickoffBenchmarkAt(kind, 0.5f).arrive;  // between Standard and Pro
        CHECK(mid < kickoffBenchmark(kind, KickoffLevel::Standard).arrive &&
              mid > kickoffBenchmark(kind, KickoffLevel::Pro).arrive);
    }
}

TEST(ko_ghost_accel_cruise_hits_benchmarks) {
    for (int l = 0; l < kKoLevelCount; ++l)
        for (int i = 0; i < kKoSpawnCount; ++i) {
            const KickoffSpawn sp = kickoffSpawnFromIndex(i);
            const KickoffBenchmark b = kickoffBenchmark(sp, kickoffLevelFromIndex(l));
            const KickoffGhost g(sp, KickoffTeam::Orange, KickoffGhostProfile::fromBenchmark(b));
            CHECK(g.model() == KickoffGhostModel::AccelCruise);
            CHECK_NEAR(g.along(b.arrive), g.path().length, 1.0);
            const KickoffGhostState end = g.stateAt(b.arrive);
            CHECK_NEAR(length2D(end.pos), kKoContactDist, 1.5);
            CHECK_NEAR(g.contactSpeed(), b.contactSpeed, 2.0);
            CHECK_NEAR(g.speedAlong(b.arrive), b.contactSpeed, 2.0);
            const float half = g.path().length * 0.5f;
            CHECK_NEAR(g.along(g.timeAt(half)), half, 1.0);
            CHECK(!g.stateAt(b.arrive - 0.01f).arrived && g.stateAt(b.arrive + 0.01f).arrived);
            CHECK(g.along(0.f) == 0.f);
            // Monotonic.
            float prev = -1.f;
            bool mono = true;
            for (int k = 0; k <= 300; ++k) {
                const float a = g.along(static_cast<float>(k) * kTickDt);
                mono = mono && a >= prev;
                prev = a;
            }
            CHECK(mono);
        }
    // Pro diagonal shape check (research: s(0.5 s) ~ 259 uu, s(1.0 s) ~ 1035 uu).
    const KickoffGhost pro(
        KickoffSpawn::DiagonalRight, KickoffTeam::Orange,
        KickoffGhostProfile::fromBenchmark(kickoffBenchmark(KickoffSpawn::DiagonalRight, KickoffLevel::Pro)));
    CHECK_NEAR(pro.along(0.5f), 258.6f, 2.f);
    CHECK_NEAR(pro.along(1.0f), 1034.5f, 3.f);
}

TEST(ko_ghost_fallback_reaction_and_box) {
    KickoffGhostProfile p;
    p.arrive = 3.5f;
    p.contactSpeed = 3000.f;  // v*T > 2d: accel-cruise infeasible
    const KickoffGhost g(KickoffSpawn::DiagonalRight, KickoffTeam::Orange, p);
    CHECK(g.model() == KickoffGhostModel::ConstAccel);
    CHECK_NEAR(g.along(3.5f), g.path().length, 1.f);
    CHECK_NEAR(g.contactSpeed(), 2.f * g.path().length / 3.5f, 2.f);
    KickoffGhostProfile r;
    r.arrive = 2.2f;
    r.contactSpeed = 2200.f;
    r.reaction = 0.2f;
    const KickoffGhost gr(KickoffSpawn::DiagonalRight, KickoffTeam::Orange, r);
    CHECK(gr.along(0.19f) == 0.f && gr.along(0.3f) > 0.f);
    CHECK_NEAR(gr.along(2.2f), gr.path().length, 1.f);
    // Ghost box drawn with DrawList::box3D: 12 edges, Octane size.
    DrawList dl;
    const KickoffGhostState st = gr.stateAt(1.f);
    const Basis b = basisFromRot(Rot{0, static_cast<int>(std::lround(st.yawRad * kRadToUnr)), 0});
    dl.box3D(st.pos + b.toWorld({13.88f, 0.f, 20.75f}), b, Vec3{118.01f, 84.2f, 36.16f} * 0.5f, colors::White);
    CHECK_EQ(dl.lines3D.size(), size_t(12));
    float minz = 1e9f, maxz = -1e9f;
    for (const auto& l : dl.lines3D) {
        minz = std::min({minz, l.a.z, l.b.z});
        maxz = std::max({maxz, l.a.z, l.b.z});
    }
    CHECK_NEAR(maxz - minz, 36.16f, 0.01);
}

TEST(ko_ghost_samples_replay) {
    const KickoffGhost model(
        KickoffSpawn::OffCenterLeft, KickoffTeam::Blue,
        KickoffGhostProfile::fromBenchmark(kickoffBenchmark(KickoffSpawn::OffCenterLeft, KickoffLevel::Pro)));
    std::vector<float> s;
    for (int i = 0; i * 0.1f < 2.2f - 1e-4f; ++i) s.push_back(model.along(static_cast<float>(i) * 0.1f));
    const KickoffGhost pb(KickoffSpawn::OffCenterLeft, KickoffTeam::Orange,
                          KickoffGhostProfile::fromSamples(s, 0.1f, 2.2f, 2250.f));
    CHECK(pb.model() == KickoffGhostModel::Samples);
    CHECK_NEAR(pb.arrivalTime(), 2.2f, 1e-5);
    CHECK_NEAR(pb.along(1.0f), model.along(1.0f), 0.5);
    CHECK_NEAR(pb.along(1.05f), model.along(1.05f), 8.0);  // linear interpolation between 0.1 s samples
    CHECK_NEAR(pb.along(2.2f), pb.path().length, 1e-3);
    CHECK_NEAR(pb.along(5.f), pb.path().length, 1e-3);
    CHECK_NEAR(pb.contactSpeed(), 2250.f, 1e-3);
    CHECK_NEAR(pb.timeAt(pb.path().length * 0.5f), model.timeAt(model.path().length * 0.5f), 0.01);
    CHECK(pb.speedAlong(1.5f) > 1000.f);
    // Degenerate: no samples at all still behaves.
    const KickoffGhost empty(KickoffSpawn::BackCenter, KickoffTeam::Orange,
                             KickoffGhostProfile::fromSamples({}, 0.1f, 2.5f, 2000.f));
    CHECK_NEAR(empty.along(2.5f), empty.path().length, 1e-3);
    CHECK(empty.along(1.f) > 0.f && empty.along(1.f) < empty.path().length);
}

TEST(ko_duel_resolver) {
    auto D = [](bool touched, float t, float v, float lat, bool flip, float gT, float gV) {
        KickoffPlayerContact c;
        c.touched = touched;
        c.time = t;
        c.speedToBall = v;
        c.lateralOffset = lat;
        c.flipIntoBall = flip;
        return kickoffResolveDuel(c, gT, gV);
    };
    CHECK(D(true, 1.80f, 2200, 0, false, 1.95f, 2200).outcome == KickoffDuel::PlayerClear);  // 0.15 s earlier
    CHECK(D(true, 2.10f, 2300, 0, true, 1.95f, 2200).outcome == KickoffDuel::GhostClear);    // 0.15 s later
    CHECK(D(true, 1.95f, 2200, 0, false, 1.95f, 2200).outcome == KickoffDuel::Neutral);      // identical
    CHECK(D(true, 1.883f, 2073, 5, false, 1.95f, 2200).outcome == KickoffDuel::PlayerNarrow);
    CHECK(D(true, 1.92f, 2250, 5, true, 1.95f, 2200).outcome == KickoffDuel::PlayerNarrow);
    CHECK(D(true, 1.97f, 1700, 70, false, 1.95f, 2200).outcome == KickoffDuel::GhostNarrow);
    CHECK(D(false, 0, 0, 0, false, 1.95f, 2200).outcome == KickoffDuel::GhostClear);
    // Exactly at the window edge.
    CHECK(D(true, 1.85f, 1000, 0, false, 1.95f, 2200).outcome == KickoffDuel::PlayerClear);
    CHECK(D(true, 2.06f, 3000, 0, true, 1.95f, 2200).outcome == KickoffDuel::GhostClear);
    // Custom window.
    KickoffDuelParams wide;
    wide.clearWindow = 0.2f;
    KickoffPlayerContact c;
    c.touched = true;
    c.time = 1.80f;
    c.speedToBall = 1500.f;
    CHECK(kickoffResolveDuel(c, 1.95f, 2200.f, wide).outcome != KickoffDuel::PlayerClear);
    CHECK(kickoffDuelWon(KickoffDuel::PlayerNarrow) && !kickoffDuelWon(KickoffDuel::Neutral));
    CHECK_NEAR(kickoffContactQuality(0.f, false), 1.0, 1e-6);
    CHECK_NEAR(kickoffContactQuality(140.f, false), 0.5, 1e-6);
    CHECK_NEAR(kickoffContactQuality(500.f, true), 0.58, 1e-6);
}

TEST(ko_ghost_ball_impulse) {
    const Vec3 gpos{0.f, kKoContactDist, kKoCarRestZ};
    const Vec3 ball{0.f, 0.f, 92.75f};
    const Vec3 imp = kickoffGhostBallImpulse(gpos, {0, -2200, 0}, ball, {0, 0, 0});
    CHECK(imp.y < -2500.f && imp.y > -3500.f);
    CHECK(imp.z > 300.f);
    CHECK(std::fabs(imp.x) < 1.f);
    CHECK(length(kickoffGhostBallImpulse(gpos, {0, -2200, 0}, ball, {0, -3000, 0})) ==
          0.f);  // ball already faster away
    const Vec3 half = kickoffGhostBallImpulse(gpos, {0, -2200, 0}, ball, {0, 0, 0}, 0.5f);
    CHECK_NEAR(length(half), 0.5f * length(imp), 1.0);
    const Vec3 off = kickoffGhostBallImpulse(Vec3{30.f, kKoContactDist, kKoCarRestZ}, {0, -2200, 0}, ball, {0, 0, 0});
    const float offAng = std::atan2(-off.x, -off.y) * kRadToDeg;
    CHECK(offAng > 5.f && offAng < 25.f);
    CHECK_NEAR(kickoffLateralToExitAngleDeg(30.f), 16.3f, 0.3f);
    CHECK_NEAR(kickoffExtraImpulseFactor(100.f), 0.65f, 1e-6);
    CHECK_NEAR(kickoffExtraImpulseFactor(2300.f), 0.55f, 1e-6);
    CHECK_NEAR(kickoffExtraImpulseFactor(9000.f), 0.30f, 1e-6);
    const Vec3 bv{0, 2800, 0};
    CHECK(length(kickoffDuelBallImpulse(KickoffDuel::PlayerClear, bv, {0, -1, 0}, imp)) == 0.f);
    CHECK((bv + kickoffDuelBallImpulse(KickoffDuel::PlayerNarrow, bv, {0, -1, 0}, imp)).y > 0.f);
    CHECK((bv + kickoffDuelBallImpulse(KickoffDuel::GhostNarrow, bv, {0, -1, 0}, imp)).y < 0.f);
    CHECK(std::fabs((bv + kickoffDuelBallImpulse(KickoffDuel::Neutral, bv, {0, -1, 0}, imp)).y) < 400.f);
    CHECK((bv + kickoffDuelBallImpulse(KickoffDuel::GhostClear, bv, {0, -1, 0}, imp)).y < -2000.f);
}
