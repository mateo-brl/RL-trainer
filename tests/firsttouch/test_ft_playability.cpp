// Playability of the first-touch drills (audit spec firsttouch-shooting, cross-cutting fix 10): every setup is checked
// with the core ball physics (pr::stepBall / simulatePath of the prediction module) and the repo's car reach model
// (findIntercept), and the module is driven by a small simulated world with a scripted player who reacts 0.4 s after
// the release. The assertions are on success, not only on the presence of a result.
//
// The firsttouch test binary does not link the prediction module: this file compiles its sources itself (namespace
// rlt::pr, no clash). No other test file of the binary may include them.

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "core/modules/prediction/BallPath.cpp"
#include "core/modules/prediction/BallPhysics.cpp"
#include "core/modules/prediction/Intercept.cpp"
#include "core/modules/firsttouch/FirstTouchTutorials.h"
#include "tests/firsttouch/ft_test_util.h"

using namespace rlt;
using namespace rlt::ft;
using namespace fttest;

namespace {

constexpr float kReact = 0.4f;  // scripted player reaction (fix 10: 0.3 to 0.5 s)
const Vec3 kHalf{59.005f, 42.1f, 18.08f};
const Vec3 kOffset{13.88f, 0.f, 20.75f};

// ---- Geometry helpers -------------------------------------------------------------------------------------------
pr::PredictedPath simulate(const Vec3& p, const Vec3& v, const Vec3& w = {}, float seconds = 6.f) {
    pr::SimBall b;
    b.pos = p;
    b.vel = v;
    b.angVel = w;
    pr::PredictedPath path;
    pr::simulatePath(b, seconds, pr::SimParams{}, pr::ArenaMode::Soccar, path);
    pr::analyzePath(path, 93.f, 300.f);
    return path;
}

pr::InterceptResult intercept(const pr::PredictedPath& path, const DrillPose& pose, float minTime = 0.3f,
                              bool boost = false, const Vec3& carVel = {}) {
    static const pr::CarReachModel model;
    pr::InterceptCar car;
    car.pos = pose.pos;
    car.vel = carVel;
    car.forward = pose.fwd;
    car.boost = boost ? 100.f : 0.f;
    pr::InterceptParams prm;
    prm.useBoost = boost;
    prm.minTime = minTime;
    return pr::findIntercept(path, car, prm, model);
}

int floorBouncesBefore(const pr::PredictedPath& path, int index) {
    int n = 0;
    for (int i = 0; i < path.bounceCount; ++i)
        if (path.bounces[static_cast<size_t>(i)].index <= index && (path.bounces[static_cast<size_t>(i)].flags & pr::kStepFloor)) ++n;
    return n;
}

bool wallOrGoalBefore(const pr::PredictedPath& path, int lastIndex) {
    for (int i = 0; i <= lastIndex && i < path.size(); ++i)
        if (path.samples[static_cast<size_t>(i)].flags & (pr::kStepWall | pr::kStepGoal)) return true;
    return false;
}

float touchScore(float relSpeed, float contact01 = 1.f) {
    TouchScore s;
    s.sSpeed = rampScore(relSpeed, 150.f, 600.f);
    s.sContact = contact01;
    return touchOnlyScore(s, ScoreParams{});
}

DrillPose randomPose(Rng& rng) {
    CarState c = groundCar(Vec3{rng.range(-4000.f, 4000.f), rng.range(-5000.f, 5000.f), kCarZ}, Vec3{},
                           rng.range(-180.f, 180.f));
    return poseFromCar(c);
}

// The five freeplay / kickoff spawns of the blue team.
std::vector<DrillPose> spawnPoses() {
    std::vector<DrillPose> v;
    const struct {
        float x, y, yaw;
    } k[] = {{-2048.f, -2560.f, 45.f}, {2048.f, -2560.f, 135.f}, {-256.f, -3840.f, 90.f}, {256.f, -3840.f, 90.f},
             {0.f, -4608.f, 90.f}};
    for (const auto& s : k) v.push_back(poseFromCar(groundCar(Vec3{s.x, s.y, kCarZ}, Vec3{}, s.yaw)));
    return v;
}

// ---- Held single jump (critic probe high.cpp) ---------------------------------------------------------------
// Car origin height `t` seconds after the jump press (button held): impulse 291.67 uu/s, extra 1458.33 uu/s^2 for
// 0.2 s, gravity 650.
float jumpZ(float t, float& vz) {
    if (t <= 0.f) {
        vz = 0.f;
        return 17.f;
    }
    const float th = std::min(t, 0.2f);
    float v = 291.667f + (1458.333f - 650.f) * th;
    float z = 17.f + 291.667f * th + 0.5f * 808.333f * th * th;
    if (t > 0.2f) {
        const float u = t - 0.2f;
        z += v * u - 325.f * u * u;
        v -= 650.f * u;
    }
    if (z < 17.f) {
        z = 17.f;
        v = 0.f;
    }
    vz = v;
    return z;
}

// Hanging ball released with vz upward `ahead` uu in front of a stopped car, jump pressed `jumpDelay` s after the
// release (< -50: no jump). Returns the touch-only score of the first touch (-1 when none in 3 s).
float hangingTouchScore(float ahead, float z0, float vz0, float jumpDelay) {
    pr::SimBall b;
    b.pos = Vec3{ahead, 0.f, z0};
    b.vel = Vec3{0.f, 0.f, vz0};
    for (int i = 1; i <= 360; ++i) {
        pr::stepBall(b);
        const float t = static_cast<float>(i) * kTickDt;
        float vzc = 0.f;
        const float zc = jumpDelay < -50.f ? jumpZ(-1.f, vzc) : jumpZ(t - jumpDelay, vzc);
        const float dx = std::max(0.f, std::fabs(b.pos.x - kOffset.x) - kHalf.x);
        const float dz = std::max(0.f, std::fabs(b.pos.z - (zc + kOffset.z)) - kHalf.z);
        if (std::sqrt(dx * dx + dz * dz) <= 91.25f) return touchScore(std::fabs(b.vel.z - vzc));
    }
    return -1.f;
}

} // namespace

// ==== Roulante vers toi ==========================================================================================
TEST(ft_roll_in_niveau1_is_playable_and_stopping_passes) {
    std::vector<DrillPose> poses = spawnPoses();
    Rng pr7(7);
    for (int k = 0; k < 300; ++k) poses.push_back(randomPose(pr7));
    const DrillCriteria c = drillCriteria(0.f, 60.f, 0.6f, 6.f);
    Rng rng(11);
    int checked = 0;
    float worstT = 0.f;
    for (const DrillPose& player : poses) {
        const DrillPose pose = drillPoseFor(DrillType::RollIn, player, 0);
        const DrillSpawn sp = makeSpawn(DrillType::RollIn, pose, 0.f, rng);
        CHECK_NEAR(length2D(sp.ball.pos - pose.pos), 1600.f, 160.f);
        CHECK(length2D(sp.ball.vel) >= 349.f && length2D(sp.ball.vel) <= 451.f);
        const pr::PredictedPath path = simulate(sp.ball.pos, sp.ball.vel);
        const pr::InterceptResult r = intercept(path, pose);
        CHECK(r.found);
        if (!r.found) continue;
        worstT = std::max(worstT, r.t);
        CHECK(r.t + kReact <= c.timeout - 3.f);
        CHECK(!wallOrGoalBefore(path, r.index));
        CHECK(floorBouncesBefore(path, r.index) == 0);
        // A stopped car facing it: the ball's speed when it reaches the car decides the touch score.
        int at = path.size() - 1;
        for (int i = 0; i < path.size(); ++i)
            if (length2D(path.samples[static_cast<size_t>(i)].pos - hitboxCenter(groundCar(pose.pos, {}, pose.yawDeg))) < 160.f) {
                at = i;
                break;
            }
        CHECK(touchScore(length(path.samples[static_cast<size_t>(at)].vel)) >= c.minScore);
        ++checked;
    }
    CHECK(checked == static_cast<int>(poses.size()));
    CHECK(worstT < 2.5f);
    // The tab no longer tells the player to drive into the ball.
    CHECK(!contains(drillSteps(DrillType::RollIn)[0], "Avance"));
    CHECK(contains(drillSteps(DrillType::RollIn)[0], "sans accélérer"));
}

TEST(ft_floor_drills_use_the_anchor_when_the_pose_has_no_room) {
    // Facing a side wall from 2000 uu, or parked next to it: the drill starts from the player's half, facing the
    // opponent goal, for both teams.
    const DrillPose wallFacing = poseFromCar(groundCar(Vec3{2000.f, 0.f, kCarZ}, Vec3{}, 0.f));
    CHECK(freeAhead(wallFacing) < kNeedAhead);
    for (int team : {0, 1}) {
        const DrillPose a = drillPoseFor(DrillType::RollIn, wallFacing, team);
        CHECK_NEAR(a.pos.x, 2000.f, 1e-3f);
        CHECK_NEAR(a.pos.y, -2500.f * teamSign(team), 1e-3f);
        CHECK_NEAR(a.fwd.y, teamSign(team), 1e-4f);
        CHECK(freeAhead(a) > 7000.f);
        const DrillPose away = drillPoseFor(DrillType::RollAway, wallFacing, team);
        CHECK_NEAR(away.pos.y, -3000.f * teamSign(team), 1e-3f);
    }
    const DrillPose nearWall = poseFromCar(groundCar(Vec3{3500.f, -1000.f, kCarZ}, Vec3{}, 90.f));
    CHECK(std::fabs(drillPoseFor(DrillType::Pass, nearWall, 0).pos.x) <= 2000.f);
    // A pose with room is kept.
    const DrillPose open = poseFromCar(groundCar(Vec3{0.f, -2000.f, kCarZ}, Vec3{}, 90.f));
    CHECK(drillPoseFor(DrillType::Lob, open, 0).pos == open.pos);
    // Floor balls stay off the floor-to-wall ramp.
    Rng rng(3);
    for (int k = 0; k < 400; ++k) {
        const DrillPose p = drillPoseFor(DrillType::RollIn, randomPose(rng), k % 2);
        for (DrillType t : {DrillType::RollIn, DrillType::RollAway, DrillType::Pass}) {
            const DrillSpawn s = makeSpawn(t, drillPoseFor(t, p, k % 2), rng.uniform(), rng);
            CHECK(std::fabs(s.ball.pos.x) <= 3800.5f && std::fabs(s.ball.pos.y) <= 4900.5f);
        }
    }
}

// ==== Roulante qui s'éloigne =====================================================================================
TEST(ft_roll_away_has_room_at_every_level) {
    Rng poses(7), rng(19);
    int fails = 0, total = 0;
    for (int k = 0; k < 300; ++k) {
        const DrillPose player = randomPose(poses);
        for (float level : {0.f, 1.f / 3.f, 1.f}) {
            const DrillPose pose = drillPoseFor(DrillType::RollAway, player, 0);
            CHECK(freeAhead(pose) >= kAwayNeedAhead);
            const DrillSpawn sp = makeSpawn(DrillType::RollAway, pose, level, rng);
            const pr::PredictedPath path = simulate(sp.ball.pos, sp.ball.vel);
            // Without boost the car cannot match the fastest balls: the boost is given every rep.
            const pr::InterceptResult r = intercept(path, pose, 0.3f, level > 0.5f);
            ++total;
            const float timeout = drillCriteria(level, 60.f, 0.6f, 6.f).timeout;
            const bool ok = r.found && r.t + kReact <= timeout &&
                            !wallOrGoalBefore(path, std::min(path.size() - 1, r.index + 180));
            if (!ok) ++fails;
        }
    }
    CHECK_MSG(fails == 0, std::to_string(fails) + "/" + std::to_string(total));
    // Niveau 1: 400 uu ahead, straight, 450..550 uu/s.
    Rng r1(5);
    const DrillPose pose = drillPoseFor(DrillType::RollAway, poseFromCar(groundCar(Vec3{0.f, -3000.f, kCarZ}, {}, 90.f)), 0);
    for (int k = 0; k < 100; ++k) {
        const DrillSpawn sp = makeSpawn(DrillType::RollAway, pose, 0.f, r1);
        CHECK_NEAR(length2D(sp.ball.pos - pose.pos), 400.f, 1.f);
        CHECK(angleBetweenDeg(flat(sp.ball.vel), pose.fwd) < 0.5f);
        CHECK(length2D(sp.ball.vel) >= 449.f && length2D(sp.ball.vel) <= 551.f);
    }
}

// ==== Passe latérale =============================================================================================
TEST(ft_pass_niveau1_leaves_time_to_turn_with_the_ball) {
    Rng rng(23);
    const DrillPose pose = drillPoseFor(DrillType::Pass, poseFromCar(groundCar(Vec3{0.f, -2500.f, kCarZ}, {}, 90.f)), 0);
    const Vec3 meet = pose.pos + pose.fwd * 1000.f;
    int stoppedOk = 0;
    const int n = 300;
    for (int k = 0; k < n; ++k) {
        const DrillSpawn sp = makeSpawn(DrillType::Pass, pose, 0.f, rng);
        CHECK(angleBetweenDeg(flat(sp.ball.vel), pose.fwd) <= 60.f);
        const pr::PredictedPath path = simulate(sp.ball.pos, sp.ball.vel);
        int iMeet = -1;
        for (int i = 0; i < path.size(); ++i)
            if (length2D(path.samples[static_cast<size_t>(i)].pos - meet) < 150.f) {
                iMeet = i;
                break;
            }
        CHECK(iMeet >= 0);
        if (iMeet < 0) continue;
        const float tMeet = path.samples[static_cast<size_t>(iMeet)].t;
        const pr::InterceptResult r = intercept(path, pose);
        CHECK(r.found);
        CHECK(tMeet - r.t >= 1.0f);
        CHECK(floorBouncesBefore(path, iMeet) == 0);
        const Vec3 vb = path.samples[static_cast<size_t>(iMeet)].vel;
        // Running with it: 0.9 x its speed, 15 degrees off its direction.
        const float a = 15.f * kDegToRad;
        const Vec3 d = normalize(flat(vb), pose.fwd);
        const Vec3 side{-d.y, d.x, 0.f};
        const Vec3 vc = (d * std::cos(a) + side * std::sin(a)) * (0.9f * length2D(vb));
        CHECK(touchScore(length(vb - vc)) >= 70.f);
        if (touchScore(length(vb)) >= 50.f) ++stoppedOk;  // a stopped car in its path
    }
    CHECK(stoppedOk >= n * 9 / 10);
}

// ==== Lob ========================================================================================================
TEST(ft_lob_niveau1_instructed_technique_passes) {
    Rng rng(29);
    const DrillPose pose = drillPoseFor(DrillType::Lob, poseFromCar(groundCar(Vec3{0.f, -2500.f, kCarZ}, {}, 90.f)), 0);
    const float bar = drillCriteria(0.f, 60.f, 0.6f, 6.f).minScore;
    int bounceOk = 0, hoodOk = 0;
    const int n = 400;
    for (int k = 0; k < n; ++k) {
        const DrillSpawn sp = makeSpawn(DrillType::Lob, pose, 0.f, rng);
        const pr::PredictedPath path = simulate(sp.ball.pos, sp.ball.vel);
        // Descents through z 150: the first one is the hood catch, the second one comes after the floor bounce.
        int descents = 0;
        for (int i = 1; i < path.size(); ++i) {
            const auto& a = path.samples[static_cast<size_t>(i - 1)];
            const auto& b = path.samples[static_cast<size_t>(i)];
            if (a.pos.z > 150.f && b.pos.z <= 150.f && b.vel.z < 0.f) {
                ++descents;
                const float score = touchScore(length(b.vel));
                if (descents == 1 && score >= 50.f) ++hoodOk;
                if (descents == 2) {
                    if (score >= bar) ++bounceOk;
                    break;
                }
            }
        }
    }
    CHECK_MSG(bounceOk >= n * 95 / 100, std::to_string(bounceOk));
    CHECK(hoodOk == 0);  // why the text says "rebond"
    const std::string instr = drillInstruction(DrillType::Lob, gamepadProfile(), KeyStyle::Auto);
    CHECK(contains(instr, "rebond"));
    for (const auto& s : drillSteps(DrillType::Lob))
        if (contains(s, "capot")) CHECK(contains(s, "rebond"));
    // Niveau 1 apex 450..550.
    Rng r1(2);
    for (int k = 0; k < 100; ++k) {
        const DrillSpawn sp = makeSpawn(DrillType::Lob, pose, 0.f, r1);
        const float apex = sp.ball.pos.z + sp.ball.vel.z * sp.ball.vel.z / 1300.f;
        CHECK(apex >= 449.f && apex <= 551.f);
    }
}

// ==== Chute du mur ===============================================================================================
TEST(ft_wall_car_is_placed_near_the_wall) {
    Rng poses(31), rng(37);
    float medianT[3] = {};
    const float levels[3] = {0.f, 1.f / 3.f, 1.f};
    for (int li = 0; li < 3; ++li) {
        std::vector<float> ts;
        for (int k = 0; k < 300; ++k) {
            const DrillPose player = randomPose(poses);
            SpawnContext sc;
            sc.wallSide = (k % 2 == 0) ? 1.f : -1.f;  // alternates every rep
            const DrillSpawn sp = makeSpawn(DrillType::Wall, player, levels[li], rng, sc);
            CHECK(sp.placesCar);
            const DrillPose& car = sp.carPose;
            CHECK((car.pos.x > 0.f) == (sp.ball.pos.x > 0.f));
            CHECK((sp.ball.pos.x > 0.f) == (sc.wallSide > 0.f));
            const float gap = 4096.f - std::fabs(car.pos.x);
            CHECK(gap >= 1099.f && gap <= 1801.f);
            CHECK(std::fabs(car.fwd.x) < 1e-3f);  // parallel to the wall
            const pr::PredictedPath path = simulate(sp.ball.pos, sp.ball.vel);
            CHECK(path.landingIndex >= 0);
            if (path.landingIndex >= 0) CHECK(std::fabs(path.samples[static_cast<size_t>(path.landingIndex)].pos.x) <= 3800.f);
            const pr::InterceptResult r = intercept(path, car);
            CHECK(r.found);
            if (!r.found) continue;
            ts.push_back(r.t);
            CHECK(floorBouncesBefore(path, r.index) <= 1);
            CHECK(r.t <= (li == 2 ? 2.2f : 2.0f));
        }
        std::sort(ts.begin(), ts.end());
        if (!ts.empty()) medianT[li] = ts[ts.size() / 2];
    }
    CHECK(medianT[0] < 1.8f);
}

TEST(ft_wall_without_car_reset_needs_a_wall_in_reach) {
    SpawnContext sc;
    sc.placeCar = false;
    Rng rng(4);
    // Facing a side wall 1500 uu away: the ball goes on that wall.
    const DrillPose close = poseFromCar(groundCar(Vec3{-2600.f, 0.f, kCarZ}, {}, 180.f));
    const DrillSpawn a = makeSpawn(DrillType::Wall, close, 0.f, rng, sc);
    CHECK(a.valid && !a.placesCar);
    CHECK_NEAR(a.ball.pos.x, -3985.f, 1e-3f);
    // In the middle of the field: no wall close enough, the drill waits.
    const DrillPose middle = poseFromCar(groundCar(Vec3{0.f, 0.f, kCarZ}, {}, 0.f));
    CHECK(!makeSpawn(DrillType::Wall, middle, 0.f, rng, sc).valid);
    // The module shows what to do instead of placing a ball.
    Harness h;
    h.set("rlt_ft_drill_reset_car", "0");
    Snapshot s = makeSnap(100, groundCar(Vec3{0.f, 0.f, kCarZ}, {}), makeBall(Vec3{0.f, 1500.f, kBallZ}, {}));
    h.tick(s);
    h.command("drill", {"wall"});
    s.frame = 101;
    s.time = 101.f / kTickRate;
    CHECK(!h.tick(s).setBall);
    CHECK(h.mod.drillStatus().waitingWall);
    s.frame = 102;
    s.time = 102.f / kTickRate;
    s.car = groundCar(Vec3{2600.f, 0.f, kCarZ}, {}, 0.f);
    CHECK(h.tick(s).setBall.has_value());
    CHECK(!h.mod.drillStatus().waitingWall);
}

// ==== Balle haute ================================================================================================
TEST(ft_high_niveau1_hanging_ball) {
    // Critic probe: ball 60 uu ahead of the car origin, centre 345, released with vz 280 on the jump.
    CHECK(hangingTouchScore(60.f, kHangZ, kHangJumpVz, 0.f) >= 75.f);
    CHECK(hangingTouchScore(60.f, kHangZ, kHangJumpVz, 0.3f) >= 50.f);
    CHECK(hangingTouchScore(60.f, kHangZ, kHangJumpVz, -100.f) < 50.f);  // no jump: released anyway, too hard
    CHECK(hangingTouchScore(60.f, kHangZ, kHangJumpVz, 0.f) > hangingTouchScore(60.f, kHangZ, kHangJumpVz, 0.3f));
    // Niveaux 4 to 6: released as soon as the car is under it, the player times the jump: a jump 0.5 s after the
    // release (niveau 4, a 0.4 s reaction) still passes niveau 4's bar, 0.35 s at niveau 6.
    const float bar4 = drillCriteria(levelOfPlayer(4), 60.f, 0.6f, 6.f).minScore;
    const float bar6 = drillCriteria(levelOfPlayer(6), 60.f, 0.6f, 6.f).minScore;
    Rng rng(1);
    const DrillPose pose = poseFromCar(groundCar(Vec3{0.f, -2500.f, kCarZ}, {}, 90.f));
    const DrillSpawn s4 = makeSpawn(DrillType::High, pose, levelOfPlayer(4), rng);
    const DrillSpawn s6 = makeSpawn(DrillType::High, pose, levelOfPlayer(6), rng);
    CHECK(s4.hang == HangRelease::Under && s6.hang == HangRelease::Under);
    for (float d = 0.f; d <= 0.5f; d += 0.05f) CHECK(hangingTouchScore(60.f, kHangZ, s4.releaseVz, d) >= bar4);
    for (float d = 0.f; d <= 0.35f; d += 0.05f) CHECK(hangingTouchScore(60.f, kHangZ, s6.releaseVz, d) >= bar6 - 2.f);
    // The audit's first fix (ball rising from 300 at 400 uu/s, 900 uu ahead) falls before the car gets there.
    // It is back down through the jump touch height (300) before the car has driven the 900 uu (about 1.5 s).
    pr::SimBall b;
    b.pos = Vec3{900.f, 0.f, 300.f};
    b.vel = Vec3{0.f, 0.f, 400.f};
    float tDown = 0.f;
    for (int i = 1; i < 600 && !(b.vel.z < 0.f && b.pos.z < 300.f); ++i) {
        pr::stepBall(b);
        tDown = static_cast<float>(i) * kTickDt;
    }
    CHECK(tDown < 1.5f);
}

// ---- A small world for the module ---------------------------------------------------------------------------
namespace {

double curvature(double v) {
    static constexpr double tbl[6][2] = {{0, 0.00690}, {500, 0.00398}, {1000, 0.00235}, {1500, 0.00138}, {1750, 0.00110}, {2300, 0.00088}};
    v = std::clamp(std::fabs(v), 0.0, 2300.0);
    for (int i = 0; i < 5; ++i)
        if (v <= tbl[i + 1][0]) return tbl[i][1] + (tbl[i + 1][1] - tbl[i][1]) * (v - tbl[i][0]) / (tbl[i + 1][0] - tbl[i][0]);
    return tbl[5][1];
}

float throttleAccel(float v) {
    v = std::fabs(v);
    if (v < 1400.f) return 1600.f - 1440.f * v / 1400.f;
    if (v < 1410.f) return 160.f * (1410.f - v) / 10.f;
    return 0.f;
}

// What the scripted player asks this tick.
struct Control {
    bool active = false;     // false: hands off (coasting)
    Vec3 steerTo;            // point to turn toward (when steer)
    bool steer = false;
    float speed = 0.f;       // wanted forward speed (negative: reverse)
    bool boost = false;
    bool jump = false;       // jump button held
};

// Car on the floor or in a single jump, ball with the core physics, contacts with a soft "cushion" impulse.
struct World {
    Harness& h;
    CarState car;
    pr::SimBall ball;
    int64_t frame = 1000;
    float yawDeg = 90.f;
    float speed = 0.f;       // signed, along the nose
    float vz = 0.f;
    float jumpHeld = 0.f;
    bool jumped = false;
    bool prevJump = false;
    bool frozen = false;
    bool contact = false;
    int touches = 0;
    int64_t lastTouchFrame = -1;
    float firstTouchRel = -1.f;
    float firstTouchBallVz = 0.f, firstTouchCarVz = 0.f;
    Vec3 firstTouchLocal;
    Inputs lastIn;

    explicit World(Harness& harness) : h(harness) {
        car = groundCar(Vec3{0.f, -2500.f, kCarZ}, {}, 90.f);
        ball.pos = Vec3{0.f, 0.f, 93.15f};
    }

    Snapshot snap(const Inputs& in) const {
        Snapshot s = makeSnap(frame, car, BallState{});
        s.ball.valid = true;
        s.ball.pos = ball.pos;
        s.ball.vel = ball.vel;
        s.ball.angVel = ball.angVel;
        s.in = in;
        return s;
    }

    void apply(const Actions& a) {
        if (a.setCar) {
            car.pos = a.setCar->pos;
            car.rot = a.setCar->rot;
            car.basis = basisFromRot(car.rot);
            yawDeg = static_cast<float>(a.setCar->rot.yaw) * 360.f / 65536.f;
            speed = dot(a.setCar->vel, basisFromRot(a.setCar->rot).fwd);
            vz = 0.f;
            jumped = false;
        }
        if (a.setBall) {
            ball.pos = a.setBall->pos;
            ball.vel = a.setBall->vel;
            ball.angVel = a.setBall->angVel;
        }
        frozen = a.freezeCar;
    }

    bool onGround() const { return car.pos.z <= 17.02f && vz <= 0.f; }

    // One tick: inputs from the control, car motion, ball physics, contact. Returns the module's actions.
    Actions tick(const Control& c) {
        Inputs in;
        if (c.active) {
            in.throttle = c.speed > speed + 20.f ? 1.f : (c.speed < speed - 20.f ? -1.f : 0.f);
            in.boost = c.boost;
            in.jump = c.jump;
        }
        // Car.
        const float dt = kTickDt;
        if (!frozen && c.active) {
            if (onGround()) {
                if (c.steer) {
                    const Vec3 d = flat(c.steerTo - car.pos);
                    if (length(d) > 1.f) {
                        const float want = std::atan2(d.y, d.x) * kRadToDeg;  // the nose turns toward the point
                        float err = want - yawDeg;
                        while (err > 180.f) err -= 360.f;
                        while (err < -180.f) err += 360.f;
                        const float v = std::max(std::fabs(speed), 150.f);
                        const float rate = static_cast<float>(v * curvature(v)) * kRadToDeg;
                        yawDeg += std::clamp(err, -rate * dt, rate * dt);
                    }
                }
                float a = 0.f;
                if (c.speed > speed + 5.f) a = speed >= 0.f ? throttleAccel(speed) + (c.boost ? 991.67f : 0.f) : 3500.f;
                else if (c.speed < speed - 5.f) a = speed > 0.f ? -3500.f : -throttleAccel(speed);
                speed += a * dt;
                if ((a > 0.f && speed > c.speed) || (a < 0.f && speed < c.speed)) speed = c.speed;
                speed = std::clamp(speed, -1410.f, c.boost ? 2300.f : std::max(1410.f, speed));
            }
            if (c.jump && !prevJump && onGround()) {
                vz = 291.667f;
                jumpHeld = 0.f;
                jumped = true;
            }
        } else if (!frozen && onGround()) {
            speed -= std::copysign(std::min(std::fabs(speed), 525.f * dt), speed);  // coasting
        }
        if (!onGround() || vz > 0.f) {
            if (c.jump && jumpHeld < 0.2f) vz += 1458.333f * dt;
            jumpHeld += dt;
            vz -= 650.f * dt;
        }
        prevJump = c.active && c.jump;
        if (!frozen) {
            car.rot = rotFromYawDeg(yawDeg);
            car.basis = basisFromRot(car.rot);
            car.pos += car.basis.fwd * (speed * dt);
            car.pos.z += vz * dt;
            if (car.pos.z < 17.01f) {
                car.pos.z = 17.01f;
                vz = 0.f;
            }
        }
        car.vel = car.basis.fwd * speed + Vec3{0.f, 0.f, vz};
        car.onGround = onGround();
        car.wheelContacts = car.onGround ? 4 : 0;
        car.jumped = !car.onGround;
        // Ball.
        const Snapshot pre = snap(in);
        pr::stepBall(ball);
        ++frame;
        // Contact: ball sphere against the car's box.
        const Vec3 local = car.basis.toLocal(ball.pos - car.pos) - kOffset;
        const Vec3 q{std::clamp(local.x, -kHalf.x, kHalf.x), std::clamp(local.y, -kHalf.y, kHalf.y),
                     std::clamp(local.z, -kHalf.z, kHalf.z)};
        const Vec3 dl = local - q;
        const bool touching = length(dl) <= 91.25f;
        Actions out;
        if (touching && !contact) {
            ++touches;
            const Vec3 n = normalize(car.basis.toWorld(dl), Vec3{0.f, 0.f, 1.f});
            const Vec3 rel = ball.vel - car.vel;
            if (firstTouchRel < 0.f) {
                firstTouchRel = length(rel);
                firstTouchBallVz = ball.vel.z;
                firstTouchCarVz = car.vel.z;
                firstTouchLocal = local;
            }
            const float vn = dot(rel, n);
            if (vn < 0.f) ball.vel -= n * vn;               // no restitution car-ball
            ball.vel += n * (0.3f * length(rel) + 30.f);    // soft push
            ball.pos = car.pos + car.basis.toWorld(q + kOffset) + n * 91.5f;
            car.lastBallTouchFrame = static_cast<int>(frame);
            TouchEvent t;
            t.frame = pre.frame;
            t.pre = pre;
            t.post1 = snap(in);
            t.post = t.post1;
            t.newContact = true;
            t.ticksSincePrevTouch = lastTouchFrame >= 0 ? static_cast<int>(frame - lastTouchFrame) : (1 << 30);
            lastTouchFrame = frame;
            out = h.tickWithTouch(t);
        } else {
            out = h.tick(snap(in));
        }
        contact = touching;
        lastIn = in;
        apply(out);
        return out;
    }

    // Runs until the drill is live, pressing the throttle once (first ball).
    void passReady(int maxTicks = 900) {
        for (int i = 0; i < maxTicks; ++i) {
            const auto ph = h.mod.drillStatus().phase;
            if (ph != FirstTouchModule::DrillPhase::Ready && ph != FirstTouchModule::DrillPhase::Place) return;
            Control c;
            c.active = true;
            c.speed = i < 2 ? 0.f : 2000.f;  // throttle released once, then pressed (the car is held)
            tick(c);
        }
    }
};

Harness& startModule(Harness& h, const std::vector<std::string>& args, World& w, float level = 0.f, bool full = false) {
    if (full) {
        Store st;
        st.setFloat(std::string("firsttouch.full.") + args[0] + ".level", level);
        st.setInt(std::string("firsttouch.full.") + args[0] + ".attempts", 1);
        h.mod.load(st);
    } else if (args[0] != "mix") {
        DrillType t;
        parseDrillId(args[0], t);
        setDrillLevel(h.mod, t, level);
    }
    h.set("rlt_ft_drill_seed", "777");
    h.tick(w.snap(Inputs{}));
    h.command("drill", args);
    return h;
}

using Script = std::function<Control(World&, float)>;  // world, seconds since the release

// Plays one rep with the script (reaction included in the script), until the rep ends. Returns the drill view.
FirstTouchModule::DrillView playRep(World& w, const Script& script, float maxSeconds = 14.f) {
    auto& h = w.h;
    const int repsBefore = h.mod.drillStatus().reps;
    w.passReady();
    const float go = static_cast<float>(w.frame) / kTickRate;
    for (int i = 0; i < static_cast<int>(maxSeconds * kTickRate); ++i) {
        const auto st = h.mod.drillStatus();
        if (!st.active || st.reps > repsBefore) break;
        if (st.phase == FirstTouchModule::DrillPhase::Pause) break;
        const float t = static_cast<float>(w.frame) / kTickRate - go;
        w.tick(t < kReact ? Control{} : script(w, t));
    }
    return h.mod.drillStatus();
}

// Drives to `p` and stops there facing `faceTo` (arrives on a straight line, slows down in time).
Control goTo(const World& w, const Vec3& p, const Vec3& faceTo, float stopRadius = 60.f, bool boost = false) {
    Control c;
    c.active = true;
    const Vec3 hc = hitboxCenter(w.car);
    const float d = length2D(p - hc);
    if (d > stopRadius) {
        c.steer = true;
        c.steerTo = p;
        c.speed = std::min(boost ? 2300.f : 1400.f, std::sqrt(2.f * 3000.f * std::max(0.f, d - stopRadius)));
        c.boost = boost && d > 1200.f;
    } else {
        c.steer = true;
        c.steerTo = faceTo;
        c.speed = 0.f;
    }
    return c;
}

} // namespace

// ==== Module: get-ready hold, hanging ball, Mix ==================================================================
TEST(ft_hold_keeps_car_and_ball_still_and_first_ball_waits_for_the_throttle) {
    Harness h;
    World w(h);
    startModule(h, {"roll_in"}, w);
    // The throttle still held from before does not start it: it must be released once.
    int holdTicks = 0;
    for (int i = 0; i < 240; ++i) {
        Control c;
        c.active = true;
        c.speed = 2000.f;  // held all along
        const Actions a = w.tick(c);
        if (h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Ready) break;
        ++holdTicks;
        CHECK(a.setCar && a.setBall && a.freezeCar);
        if (a.setBall) CHECK(a.setBall->vel == Vec3{});
    }
    CHECK(holdTicks == 240);
    CHECK(contains(h.mod.drillStatus().readyLine, "accélérer"));
    w.passReady();
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch);
    // Later balls: 1 s hold at niveau 1, car and ball re-sent with zero velocity every tick.
    h.command("skip");
    Control idle;
    w.tick(idle);  // placement
    int ticks = 0;
    while (h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Ready && ticks < 400) {
        const Actions a = w.tick(idle);
        if (h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Ready) {
            CHECK(a.setCar.has_value() && a.setBall.has_value());
            if (a.setBall) CHECK(a.setBall->vel == Vec3{});
        } else {
            CHECK(a.setBall.has_value() && length(a.setBall->vel) > 300.f);  // released with its velocity
            CHECK(!a.freezeCar && !a.setCar);  // the player's inputs act at once
        }
        ++ticks;
    }
    CHECK(ticks >= 118 && ticks <= 122);
    // Niveau 5: 0.5 s; niveau 8: no hold, placed and released on the same tick.
    CHECK_NEAR(freezeSeconds(levelOfPlayer(5)), 0.5f, 1e-6f);
    CHECK_NEAR(freezeSeconds(levelOfPlayer(8)), 0.f, 1e-6f);
    Harness g;
    World wg(g);
    startModule(g, {"roll_in"}, wg, levelOfPlayer(8));
    wg.passReady();
    g.command("skip");
    const Actions place = wg.tick(idle);
    CHECK(place.setCar && place.setBall && length(place.setBall->vel) > 300.f);
    CHECK(g.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch);
}

TEST(ft_high_hanging_ball_waits_for_the_jump_under_it) {
    Harness h;
    World w(h);
    startModule(h, {"high"}, w);
    w.passReady();
    CHECK(h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch);
    const Vec3 hang = w.ball.pos;
    CHECK_NEAR(hang.z, kHangZ, 1e-3f);
    // A jump far from the ball does not release it; the ball stays still (setBall with zero velocity every tick).
    int held = 0, released = 0;
    for (int i = 0; i < 60; ++i) {
        Control c;
        c.active = true;
        c.jump = i < 20;
        const Actions a = w.tick(c);
        if (a.setBall) {
            if (a.setBall->vel == Vec3{}) ++held;
            else ++released;
        }
    }
    CHECK(held == 60 && released == 0);
    CHECK(distance(w.ball.pos, hang) < 1.f);
    // Drive under it, stop with the ball over the front of the roof, jump: one release with vz 280, then the touch.
    float scoreSeen = -1.f;
    for (int i = 0; i < 900; ++i) {
        const Vec3 target = hang - w.car.basis.fwd * (kOffset.x + 50.f);  // ball 60 ahead of the origin
        Control c = goTo(w, Vec3{target.x, target.y, 17.f}, hang, 15.f);
        const bool under = ballOverFrontRoof(w.car, w.ball.pos);
        c.jump = under && std::fabs(w.speed) < 50.f;
        const Actions a = w.tick(c);
        if (a.setBall && length(a.setBall->vel) > 1.f) {
            ++released;
            CHECK_NEAR(a.setBall->vel.z, kHangJumpVz, 1e-3f);
            CHECK(length2D(a.setBall->vel) < 1e-3f);
        }
        if (w.touches > 0) break;
    }
    CHECK(released == 1);
    CHECK(w.touches == 1);
    // No setBall after the touch: the hit is never erased.
    for (int i = 0; i < 150; ++i) {
        const Actions a = w.tick(Control{});
        CHECK(!a.setBall.has_value() || h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::WaitTouch);
        if (h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::Pause) break;
    }
    const auto st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK_MSG(st.lastSuccess, st.lastOutcome + " / " + st.lastTip);
    (void)scoreSeen;
}

TEST(ft_high_hanging_ball_without_jump_is_released_and_fails) {
    Harness h;
    World w(h);
    startModule(h, {"high"}, w);
    w.passReady();
    const Vec3 hang = w.ball.pos;
    for (int i = 0; i < 1200 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
        const Vec3 target = hang - w.car.basis.fwd * (kOffset.x + 50.f);
        w.tick(goTo(w, Vec3{target.x, target.y, 17.f}, hang, 15.f));  // never jumps
    }
    for (int i = 0; i < 200 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause; ++i) w.tick(Control{});
    const auto st = h.mod.drillStatus();
    CHECK(w.touches >= 1);  // released after 1.5 s under it, it fell on the roof
    CHECK(!st.lastSuccess);
}

TEST(ft_mix_only_draws_unlocked_drills) {
    // Pure draw: niveau 1 everywhere except the two Débutant drills at niveau 4.
    std::array<float, kDrillKinds> w{};
    for (int i = 0; i < kDrillKinds; ++i) {
        const auto t = static_cast<DrillType>(i);
        const float level = drillTier(t) == Tier::Beginner ? levelOfPlayer(4) : 0.f;
        w[static_cast<size_t>(i)] = mixUnlocked(t, level, 0) ? mixWeight(0.5f) : 0.f;
    }
    Rng rng(41);
    DrillType prev = DrillType::RollIn;
    for (int k = 0; k < 1000; ++k) {
        const DrillType t = pickMixType(rng, prev, w);
        CHECK(t == DrillType::RollIn || t == DrillType::RollAway);
        CHECK(t != prev);
        prev = t;
    }
    // Lob reaches niveau 4: it shows up within 50 draws; 5 successes unlock too.
    CHECK(mixUnlocked(DrillType::Lob, levelOfPlayer(4), 0));
    CHECK(!mixUnlocked(DrillType::Lob, levelOfPlayer(3), 4));
    CHECK(mixUnlocked(DrillType::Lob, levelOfPlayer(1), 5));
    w[static_cast<size_t>(DrillType::Lob)] = mixWeight(0.5f);
    bool lob = false;
    for (int k = 0; k < 50 && !lob; ++k) lob = (prev = pickMixType(rng, prev, w)) == DrillType::Lob;
    CHECK(lob);
    // The drills the player succeeds least come more often.
    CHECK(mixWeight(0.2f) > mixWeight(0.8f));
    // Module: a new player's Mix only has the Débutant drills; the next ball's name shows during its hold.
    Harness h;
    World wd(h);
    CHECK(h.mod.drillLevel(DrillType::RollIn).mixUnlocked && !h.mod.drillLevel(DrillType::High).mixUnlocked);
    startModule(h, {"mix"}, wd);
    wd.tick(Control{});  // placement
    const DrawList dl = h.draw();
    bool named = false;
    for (const auto& p : dl.panels)
        for (const auto& l : p.lines)
            if (!l.detail && contains(l.text, std::string("Prochaine balle : ") + drillName(h.mod.drillStatus().current)))
                named = true;
    CHECK(named);
    CHECK(h.mod.drillStatus().nextBall == drillName(h.mod.drillStatus().current));
}

TEST(ft_roll_in_scripted_beginner_passes_niveau_1) {
    // Stays in front of the ball, then backs off a little at the contact: the touch is soft.
    int passes = 0;
    for (int seed = 0; seed < 5; ++seed) {
        Harness h;
        World w(h);
        h.set("rlt_ft_drill_seed", std::to_string(100 + seed));
        startModule(h, {"roll_in"}, w);
        const auto st = playRep(w, [](World& wd, float) {
            Control c;
            c.active = true;
            const float d = length2D(wd.ball.pos - hitboxCenter(wd.car));
            c.speed = d < 400.f ? -150.f : 0.f;
            return c;
        });
        for (int i = 0; i < 200 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause; ++i) w.tick(Control{});
        if (h.mod.drillStatus().lastSuccess) ++passes;
        CHECK(w.touches >= 1);
        (void)st;
    }
    CHECK(passes == 5);
}

// ==== Parcours complets ==========================================================================================
namespace {

std::string lastTouchText(const Harness& h) {
    const auto& l = h.mod.lastTouch();
    if (!l.valid) return "no graded touch";
    return std::string(touchTypeLabel(l.type)) + " rel " + std::to_string(static_cast<int>(l.relSpeed)) + " contact " +
           std::to_string(static_cast<int>(l.contactErrDeg));
}

FirstTouchModule::DrillView runFull(const std::string& id, float level, const Script& script, uint64_t seed = 5,
                                    std::string* touch = nullptr) {
    Harness h;
    World w(h);
    h.set("rlt_ft_drill_seed", std::to_string(seed));
    startModule(h, {id, "full"}, w, level, true);
    playRep(w, script, 16.f);
    for (int i = 0; i < 300 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause &&
                    h.mod.drillStatus().active;
         ++i)
        w.tick(script(w, 99.f));
    if (touch) *touch = lastTouchText(h);
    return h.mod.drillStatus();
}

} // namespace

TEST(ft_full_roll_in_renvoi_adverse) {
    std::string touch;
    const auto ok = runFull("roll_in", 0.f, [](World& w, float) {
        Control c;
        c.active = true;
        c.steer = true;
        c.steerTo = w.ball.pos;
        const float d = length2D(w.ball.pos - hitboxCenter(w.car));
        c.speed = d < 380.f ? -200.f : 0.f;
        return c;
    }, 5, &touch);
    CHECK_MSG(ok.lastSuccess, ok.lastOutcome + " / " + ok.lastTip + " / " + touch);
    CHECK(ok.reps == 1);
    // Driving in at speed: the "Ralentis" step is the one missing, with its tip.
    const auto bad = runFull("roll_in", 0.f, [](World& w, float) {
        Control c;
        c.active = true;
        c.steer = true;
        c.steerTo = w.ball.pos;
        c.speed = 900.f;
        return c;
    });
    CHECK(!bad.lastSuccess);
    CHECK(contains(bad.lastOutcome, "Ralentis"));
    CHECK(contains(bad.lastTip, "Ralentis"));
}

TEST(ft_full_roll_away_recupere_un_degagement) {
    auto matched = std::make_shared<float>(0.f);
    const auto ok = runFull("roll_away", 0.f, [matched](World& w, float) {
        Control c;
        c.active = true;
        const Vec3 dir = normalize(flat(w.ball.vel), w.car.basis.fwd);
        const Vec3 behind = w.ball.pos - dir * 330.f;
        const float gap = dot(flat(w.ball.pos - hitboxCenter(w.car)), dir);
        c.steer = true;
        c.steerTo = gap > 600.f ? behind : w.ball.pos;
        const float vb = length2D(w.ball.vel);
        if (gap > 500.f) {
            *matched = 0.f;
            c.speed = 2300.f;
            c.boost = gap > 900.f;
        } else if (*matched < 0.8f) {
            *matched += kTickDt;
            c.speed = vb + std::clamp((gap - 330.f) * 2.f, -100.f, 100.f);
        } else {
            c.speed = vb + 120.f;  // closes in gently
        }
        return c;
    });
    CHECK_MSG(ok.lastSuccess, ok.lastOutcome + " / " + ok.lastTip);
}

TEST(ft_full_lob_adverse) {
    for (float level : {0.f, 0.5f}) {
        Harness h;
        World w(h);
        h.set("rlt_ft_drill_seed", "9");
        startModule(h, {"lob", "full"}, w, level, true);
        w.passReady();
        const float go = static_cast<float>(w.frame) / kTickRate;
        Vec3 target;
        bool haveTarget = false;
        for (int i = 0; i < 16 * 120 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
            const float t = static_cast<float>(w.frame) / kTickRate - go;
            if (!haveTarget) {
                // The scripted player reads the circle: the predicted point where the ball comes down after the
                // bounce, from the ball's state at the release.
                const BallTrack tr = predictBall(w.ball.pos, w.ball.vel, w.ball.angVel, 6.f);
                for (size_t k = 1; k < tr.points.size() && !haveTarget; ++k)
                    if (!tr.floorBounceTimes.empty() && static_cast<float>(k) * 2.f / kTickRate > tr.floorBounceTimes[0] &&
                        tr.points[k - 1].z > 170.f && tr.points[k].z <= 170.f) {
                        target = tr.points[k];
                        haveTarget = true;
                    }
            }
            if (t < kReact) {
                w.tick(Control{});
                continue;
            }
            const Vec3 toCar = normalize(flat(Vec3{0.f, 0.f, 0.f} - w.ball.vel), Vec3{0.f, -1.f, 0.f});
            Control c = goTo(w, target + toCar * 60.f, w.ball.pos, 40.f, true);
            // After the bounce: back off with the ball to soften the touch.
            const float dh = length2D(w.ball.pos - hitboxCenter(w.car));
            if (w.ball.vel.z < 0.f && w.ball.pos.z < 300.f && dh < 350.f && h.mod.drillStatus().steps.size() > 0)
                c.speed = -std::min(250.f, length2D(w.ball.vel));
            w.tick(c);
        }
        for (int i = 0; i < 200 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause; ++i) w.tick(Control{});
        const auto st = h.mod.drillStatus();
        std::string steps;
        for (const auto& l : st.steps) steps += l + " | ";
        CHECK_MSG(st.lastSuccess, std::to_string(level) + ": " + st.lastOutcome + " / " + st.lastTip + " / phase " +
                                      std::to_string(static_cast<int>(st.phase)) + " / " + steps + " rel " +
                                      std::to_string(w.firstTouchRel) + " vz " + std::to_string(w.firstTouchBallVz) +
                                      " touches " + std::to_string(w.touches));
    }
}

TEST(ft_full_lob_touch_before_the_bounce_fails_on_the_bounce_step) {
    Harness h;
    World w(h);
    startModule(h, {"lob", "full"}, w, 0.f, true);
    w.passReady();
    // Drives straight at the ball to hit it in the air.
    for (int i = 0; i < 12 * 120 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
        Control c;
        c.active = true;
        c.steer = true;
        c.steerTo = w.ball.pos;
        c.speed = 1400.f;
        c.jump = length2D(w.ball.pos - w.car.pos) < 500.f;
        w.tick(c);
    }
    const auto st = h.mod.drillStatus();
    CHECK(!st.lastSuccess);
    CHECK(!st.lastTip.empty());
}

TEST(ft_full_steps_labels_and_never_silent_timeout) {
    // Labels numbered like the HUD, "Garde-la" from niveau 4.
    const auto l1 = fullLabels(DrillType::RollIn, 1);
    const auto l4 = fullLabels(DrillType::RollIn, 4);
    CHECK(l1.size() == 3 && l4.size() == 4);
    if (!l1.empty()) CHECK(l1.front() == "1/3 Aligne-toi sur la balle");
    if (l4.size() == 4) CHECK(l4.back() == "4/4 Garde-la près de toi");
    for (DrillType t : {DrillType::RollAway, DrillType::Pass, DrillType::Lob, DrillType::Wall, DrillType::High})
        for (int n : {1, 4, 10})
            for (const auto& s : fullLabels(t, n)) {
                CHECK(noEmDash(s));
                CHECK(s.find("\xE2\x80\x93") == std::string::npos);
            }
    // Doing nothing at all: the rep is ignored, not failed. Moving without reaching the steps: a failed rep with the
    // missing step's tip (fix 5).
    Harness h;
    World w(h);
    startModule(h, {"wall", "full"}, w, 0.f, true);
    w.passReady();
    for (int i = 0; i < 15 * 120 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
        Control c;
        c.active = true;
        c.speed = 300.f;  // drives straight ahead, never looks at the wall
        w.tick(c);
    }
    const auto st = h.mod.drillStatus();
    CHECK(st.phase == FirstTouchModule::DrillPhase::Pause);
    CHECK(!st.lastSuccess);
    CHECK(contains(st.lastOutcome, "Il manquait : "));
    CHECK(!st.lastTip.empty());
    CHECK(h.mod.drillLevel(DrillType::Wall).fullAttempts == 2);  // the stored attempt + this one
}

TEST(ft_full_high_lob_haut_adverse) {
    for (float level : {0.f, 1.f}) {
        Harness h;
        World w(h);
        h.set("rlt_ft_drill_seed", "13");
        startModule(h, {"high", "full"}, w, level, true);
        w.passReady();
        const float go = static_cast<float>(w.frame) / kTickRate;
        const BallTrack tr = predictBall(w.ball.pos, w.ball.vel, w.ball.angVel, 6.f);
        // Where it rises highest after its bounce.
        Vec3 apex;
        float best = -1.f;
        const float t0 = tr.floorBounceTimes.empty() ? 0.f : tr.floorBounceTimes[0];
        const float t1 = tr.floorBounceTimes.size() > 1 ? tr.floorBounceTimes[1] : 99.f;
        for (size_t k = 0; k < tr.points.size(); ++k) {
            const float t = static_cast<float>(k) * 2.f / kTickRate;
            if (t > t0 && t < t1 && tr.points[k].z > best) {
                best = tr.points[k].z;
                apex = tr.points[k];
            }
        }
        bool bounced = false;
        float jumpAt = -1.f;
        float prevVz = w.ball.vel.z;
        for (int i = 0; i < 14 * 120 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
            const float t = static_cast<float>(w.frame) / kTickRate - go;
            if (!bounced && prevVz < -20.f && w.ball.vel.z > 0.f) {
                bounced = true;
                jumpAt = t + 0.2f;  // just after the bounce: the car's jump peaks when the ball does
            }
            prevVz = w.ball.vel.z;
            if (t < kReact) {
                w.tick(Control{});
                continue;
            }
            const Vec3 spot = apex - w.car.basis.fwd * kOffset.x;
            Control c = goTo(w, Vec3{spot.x, spot.y, 17.f}, w.ball.pos, 30.f);
            c.jump = jumpAt >= 0.f && t >= jumpAt && t < jumpAt + 1.0f;
            w.tick(c);
        }
        for (int i = 0; i < 300 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause; ++i) w.tick(Control{});
        const auto st = h.mod.drillStatus();
        CHECK_MSG(st.lastSuccess, std::to_string(level) + ": " + st.lastOutcome + " / " + st.lastTip + " / rel " +
                                      std::to_string(w.firstTouchRel) + " ball vz " + std::to_string(w.firstTouchBallVz) +
                                      " car vz " + std::to_string(w.firstTouchCarVz) + " local x " +
                                      std::to_string(w.firstTouchLocal.x) + " z " + std::to_string(w.firstTouchLocal.z));
    }
}

namespace {

// The scripted player reads the circle: the predicted point where the ball comes down through touch height after at
// least `minBounces` floor bounces, slower than `maxVz` (same rule as the module's target).
Vec3 descentTarget(const World& w, int minBounces, float maxVz) {
    const BallTrack tr = predictBall(w.ball.pos, w.ball.vel, w.ball.angVel, 6.f);
    const float dt = 2.f / kTickRate;
    for (size_t k = 1; k < tr.points.size(); ++k) {
        int bounces = 0;
        for (float bt : tr.floorBounceTimes)
            if (bt <= static_cast<float>(k) * dt) ++bounces;
        if (bounces < minBounces) continue;
        const float vz = (tr.points[k - 1].z - tr.points[k].z) / dt;
        if (tr.points[k - 1].z > 170.f && tr.points[k].z <= 170.f && (maxVz <= 0.f || vz <= maxVz)) return tr.points[k];
    }
    return tr.points.back();
}

} // namespace

TEST(ft_full_pass_passe_d_un_coequipier) {
    struct State {
        bool init = false;
        Vec3 meet, dir;
        int stage = 0;
    };
    auto st = std::make_shared<State>();
    std::string touch;
    const auto ok = runFull("pass", 0.f, [st](World& w, float) {
        if (!st->init) {
            st->init = true;
            st->meet = w.car.pos + w.car.basis.fwd * 1800.f;
            st->dir = normalize(flat(w.ball.vel), Vec3{1.f, 0.f, 0.f});
        }
        const Vec3 w1 = st->meet - st->dir * 400.f;
        const Vec3 w2 = st->meet + st->dir * 300.f;
        Control c;
        c.active = true;
        const Vec3 hc = hitboxCenter(w.car);
        const float behind = dot(flat(hc - w.ball.pos), st->dir);  // > 0: the ball is behind the car on its path
        if (st->stage == 0) {
            c = goTo(w, w1, w2, 80.f);
            if (length2D(w1 - hc) < 120.f) st->stage = 1;
        } else if (st->stage == 1) {
            c = goTo(w, w2, w2 + st->dir * 500.f, 60.f);
            if (length2D(w2 - hc) < 100.f && behind < 700.f) st->stage = 2;
        } else {
            c.steer = true;
            c.steerTo = hc + st->dir * 500.f;
            c.speed = behind < 600.f ? 0.6f * length2D(w.ball.vel) : 0.f;  // runs with it, the ball catches up
        }
        return c;
    }, 5, &touch);
    CHECK_MSG(ok.lastSuccess, ok.lastOutcome + " / " + ok.lastTip + " / " + touch);
}

TEST(ft_full_wall_balle_qui_sort_du_mur) {
    for (uint64_t seed : {5ull, 6ull}) {
        Harness h;
        World w(h);
        h.set("rlt_ft_drill_seed", std::to_string(seed));
        startModule(h, {"wall", "full"}, w, 0.f, true);
        w.passReady();
        const float go = static_cast<float>(w.frame) / kTickRate;
        const Vec3 target = descentTarget(w, 1, 420.f);
        for (int i = 0; i < 16 * 120 && h.mod.drillStatus().phase == FirstTouchModule::DrillPhase::WaitTouch; ++i) {
            const float t = static_cast<float>(w.frame) / kTickRate - go;
            if (t < kReact) {
                w.tick(Control{});
                continue;
            }
            Control c = goTo(w, target, w.ball.pos, 60.f, true);
            const float dh = length2D(w.ball.pos - hitboxCenter(w.car));
            if (w.ball.vel.z < 0.f && w.ball.pos.z < 320.f && dh < 400.f && length2D(target - hitboxCenter(w.car)) < 200.f)
                c.speed = -std::min(300.f, length2D(w.ball.vel));  // goes with the ball
            w.tick(c);
        }
        for (int i = 0; i < 200 && h.mod.drillStatus().phase != FirstTouchModule::DrillPhase::Pause; ++i) w.tick(Control{});
        const auto s = h.mod.drillStatus();
        CHECK_MSG(s.lastSuccess, s.lastOutcome + " / " + s.lastTip + " / rel " + std::to_string(w.firstTouchRel));
    }
}

TEST(ft_full_match_sequence_five_balls_without_teleport) {
    Harness h;
    World w(h);
    h.set("rlt_ft_drill_seed", "21");
    startModule(h, {"mix", "full"}, w, 0.f, true);
    CHECK(contains(h.host.lastName, "séquence de match"));
    int carPlacements = 0;
    auto prevPhase = h.mod.drillStatus().phase;
    float liveSince = 0.f;
    for (int i = 0; i < 80 * 120 && h.mod.drillLevel(DrillType::Mix).fullAttempts < 2; ++i) {
        const auto st = h.mod.drillStatus();
        if (st.phase == FirstTouchModule::DrillPhase::WaitTouch && prevPhase != st.phase) liveSince = 0.f;
        prevPhase = st.phase;
        liveSince += kTickDt;
        Control c;
        if (st.phase == FirstTouchModule::DrillPhase::WaitTouch && liveSince > kReact) {
            c.active = true;
            c.steer = true;
            c.steerTo = w.ball.pos;
            const float d = length2D(w.ball.pos - hitboxCenter(w.car));
            if (st.current == DrillType::RollIn) {
                c.speed = d < 380.f ? -150.f : 0.f;
            } else {  // roulante qui s'éloigne: catch up, then match its speed
                const float vb = length2D(w.ball.vel);
                c.speed = d > 600.f ? vb + 600.f : vb + 80.f;
            }
        }
        const Actions a = w.tick(c);
        if (a.setCar) ++carPlacements;
    }
    CHECK(carPlacements == 0);  // never teleported
    CHECK(h.mod.drillLevel(DrillType::Mix).fullAttempts == 2);  // the stored attempt + one whole sequence
    CHECK_MSG(h.mod.drillLevel(DrillType::Mix).fullSuccesses == 1, h.mod.drillStatus().lastOutcome);
}

TEST(ft_reset_level_command_and_full_levels_are_saved) {
    Harness h;
    setDrillLevel(h.mod, DrillType::Lob, 0.6f);
    Store st;
    st.setFloat("firsttouch.full.lob.level", 0.4f);
    st.setInt("firsttouch.full.lob.attempts", 3);
    h.mod.load(st);
    CHECK_NEAR(h.mod.drillLevel(DrillType::Lob).fullLevel, 0.4f, 1e-6f);
    Store out;
    h.mod.save(out);
    CHECK_NEAR(out.getFloat("firsttouch.full.lob.level", -1.f), 0.4f, 1e-6f);
    CHECK(out.getInt("firsttouch.full.mix.attempts", -1) == 0);
    const Actions a = h.command("reset_level", {"lob"});
    CHECK(a.requestSave);
    CHECK(!a.toasts.empty() && contains(a.toasts.front().text, "niveau 1"));
    CHECK_NEAR(h.mod.drillLevel(DrillType::Lob).level, 0.f, 1e-6f);
    CHECK_NEAR(h.mod.drillLevel(DrillType::Lob).fullLevel, 0.f, 1e-6f);
    CHECK(h.mod.drillLevel(DrillType::Lob).fullAttempts == 0);
    CHECK(!h.command("reset_level", {"bogus"}).toasts.empty());
    // "drill <id> full" starts the parcours complet.
    h.tick(makeSnap(100, groundCar(Vec3{0.f, 0.f, kCarZ}, {}), makeBall(Vec3{0.f, 1500.f, kBallZ}, {})));
    h.command("drill", {"lob", "full"});
    CHECK(h.mod.drillStatus().active && h.mod.drillStatus().full);
    CHECK(contains(h.host.lastName, "parcours complet"));
}

TEST(ft_roll_away_hud_shows_the_speed_bar_near_the_ball) {
    Harness h;
    World w(h);
    startModule(h, {"roll_away"}, w);
    w.passReady();
    // Close behind the ball at about its speed: the main line carries a green bar.
    bool green = false;
    for (int i = 0; i < 400 && !green; ++i) {
        Control c;
        c.active = true;
        c.steer = true;
        c.steerTo = w.ball.pos;
        const float d = length2D(w.ball.pos - hitboxCenter(w.car));
        const float vb = length2D(w.ball.vel);
        c.speed = d > 500.f ? vb + 400.f : vb;
        w.tick(c);
        const DrawList dl = h.draw();
        for (const auto& p : dl.panels)
            for (const auto& l : p.lines)
                if (!l.detail && l.bar >= 0.f && contains(l.text, "/3 ") && l.barColor.g > 200.f && l.barColor.r < 100.f)
                    green = true;
    }
    CHECK(green);
}
