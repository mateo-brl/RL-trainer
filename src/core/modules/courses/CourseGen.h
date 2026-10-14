#pragma once
// Seeded procedural courses (rings in the air, arches on the floor and the side walls, air dribble rings,
// dribble slalom gates). Same seed + type + difficulty = bit-identical course on every platform:
//  - randomness is the integer-only PCG32 of core/common/Rng.h (never <random> distributions),
//  - every candidate centre is rounded to integer uu before any branch decision,
//  - all maths is double precision.
// Generators respect physics (boost thrust budget in the air, ground turning radius, roof dribble lateral
// acceleration) and keep every ring inside the arena (walls, ceiling, corner chamfers).
//
// Ring count: the top byte of the 64-bit seed optionally carries an explicit ring count (0 = from the
// difficulty), so a course code (seed + type + difficulty) always reproduces the exact course.
//
// Generator versions (the version is part of the course code, "RLT1-" / "RLT2-"): version 1 is kept bit-exact so
// old codes and records still give the same course. Version 2 (drill audit of v1.0.6):
//  - aerial rings: niveaux 1 to 3 are an "Initiation" band (4 to 6 big low rings, car released at rest), the regular
//    courses start at niveau 4; ring 1 is always at least 2500 uu ahead;
//  - ground & walls: floor arches only (radius 450) up to difficulty 30, wall arches from niveau 4;
//  - air dribble: ring 1 is placed from the real start (ball on the roof of a car at rest, or the popped ball,
//    chosen by kSeedPoppedStart), niveau 1 = one ring, niveau 2 = two rings;
//  - slalom: start at rest, first gate 1500 uu ahead, a real weave (150 to 300 uu sideways for a clean pass).

#include <cstdint>
#include <vector>

#include "core/common/Rng.h"
#include "core/modules/courses/CourseGeom.h"

namespace rlt::courses {

enum class CourseType : uint8_t { Aerial = 0, GroundWall = 1, AirDribble = 2, DribbleGates = 3 };
constexpr int kCourseTypeCount = 4;
constexpr char kTypeLetters[kCourseTypeCount] = {'A', 'G', 'D', 'S'};
constexpr uint32_t kGeneratorVersion = 2;     // version of new courses
constexpr uint32_t kOldestGeneratorVersion = 1;  // oldest version a code may still carry
// Air dribble (generator v2): this seed bit set = the ball starts popped in front of the car, clear = on the roof.
constexpr uint64_t kSeedPoppedStart = 1ULL << 55;

struct StartPose {
    DVec3 carPos{0, -4000, phys::kCarRestZ};
    double carYaw = phys::kPi / 2;  // radians
    DVec3 carVel;                   // velocity given at the start (towards the first ring)
    bool hasBall = false;           // false: the ball is parked out of the way
    bool ballOnRoof = false;
    DVec3 ballPos, ballVel;         // popped ball start (air dribble)
};

struct Course {
    uint64_t seed = 0;
    CourseType type = CourseType::Aerial;
    int difficultyPct = 50;   // 0..100
    uint32_t version = kGeneratorVersion;
    StartPose start;
    std::vector<Ring> rings;  // empty when generation failed
    double estLength = 0;     // uu along ring chords
    double estBoost = 0;      // boost units for the whole course, before refills
    double parTime = 0;       // s, estLength / reference speed
    double objRadius = phys::kCarPassRadius;
    bool ballCourse = false;  // the BALL must pass (air dribble, slalom)
};

struct GenStats {
    int attempts = 0, restarts = 0, fallbacks = 0, backtracks = 0, rejThrust = 0, rejBounds = 0, rejSep = 0;
};

// Shared parameters of the air walkers (aerial rings and air dribble rings).
struct AirParams {
    int count = 0;
    double rMin = 0, rMax = 0;          // ring radius range
    double sMin = 0, sMax = 0;          // spacing centre to centre
    double yawStep = 0, pitchStep = 0;  // max change of travel direction per ring (rad)
    double pitchMin = 0, pitchMax = 0;  // allowed travel pitch (rad)
    double vRef = 0, eta = 0;           // reference speed, max fraction of the boost acceleration demanded
    double clearance = 0;               // from arena surfaces
    double minSep = 0;                  // min distance to non-adjacent rings
    double noise = 0;                   // random share of each turn (rest = steering to the waypoint)
    double wpX = 0, wpYMin = 0, wpYMax = 0, wpZMin = 0, wpZMax = 0;  // waypoint box
    bool biasPlusY = false;             // keep the heading within +-yawCone of +Y
    double yawCone = 0;
};

AirParams aerialParams(double d);
AirParams airDribbleParams(double d);
// Parameters actually used for an air course: the difficulty ones, with the ring count carried by the seed and,
// when that count is above the automatic one, a relaxed spacing between non-adjacent rings so it still fits.
AirParams airParamsFor(CourseType type, double d, uint64_t seed);

// Uniform double in [0,1) with 53 random bits (two draws), exactly as the research reference.
double rngUniform01(Rng& rng);
double rngRange(Rng& rng, double a, double b);
// SplitMix64 finaliser (same as rlt::mix64).
uint64_t splitmix64(uint64_t x);

// Rounds to integer uu.
DVec3 quantize(const DVec3& v);
// Max ground turning curvature (1/uu) vs speed (RLUtilities table).
double maxGroundCurvature(double v);
// Boost thrust needed to bend the velocity from dirA to dirB over arc length s at speed v (gravity included).
double requiredThrust(const DVec3& dirA, const DVec3& dirB, double s, double v);

// Ring count carried by the seed (0 = automatic) and helper to build such a seed.
int seedRingCount(uint64_t seed);
uint64_t seedWithRingCount(uint64_t seed, int count);
// Valid explicit ring count range per course type. The maximum depends on the difficulty (big rings of easy
// courses need more room); it was measured so that generation never fails (see the tests).
int minRingCount(CourseType t);
int maxRingCount(CourseType t, double d = 1.0);

// Niveau 1..10 shown to the player for a difficulty 0..100 (same formula as CoursesModule::levelNumber).
int niveauOf(int difficultyPct);
// Generator v2 beginner bands: 1..3 for the "Initiation" of the aerial rings (niveaux 1 to 3), 1..2 for the one and
// two ring air dribble courses (niveaux 1 and 2); 0 = a regular course (and always 0 in version 1).
int beginnerStage(CourseType type, int difficultyPct, uint32_t version = kGeneratorVersion);
// Difficulty 0..1 given to the regular generator: pct / 100 in version 1; in version 2 the aerial rings start at
// niveau 4 and the air dribble at niveau 3 (their first niveaux are the beginner bands).
double regularD(CourseType type, int difficultyPct, uint32_t version = kGeneratorVersion);
// Ground & walls (v2): no wall arch at or below this difficulty (niveaux 1 to 3).
constexpr int kGroundFloorOnlyPct = 30;
// Aerial rings (v2): ring 1 at least this far ahead of the car start (horizontal distance, uu).
constexpr double kAerialFirstRingMin = 2500.0;
// Slalom (v2): first gate this far ahead of the start; sideways move (uu) a clean pass asks, niveau 1 to 10.
constexpr double kSlalomFirstGate = 1500.0;
constexpr double kSlalomWeaveMin = 150.0;
constexpr double kSlalomWeaveMax = 300.0;
// Air dribble (v2): ball resting on the roof of a car at rest at the origin (Octane hitbox), as the module places it.
DVec3 roofBallOffset();

Course generateCourse(uint64_t seed, CourseType type, int difficultyPct, GenStats* statsOut = nullptr,
                      uint32_t version = kGeneratorVersion);

// FNV-1a over the generated geometry (centre, radius, shape, flags): golden value for regression tests.
uint64_t courseHash(const Course& c);

} // namespace rlt::courses
