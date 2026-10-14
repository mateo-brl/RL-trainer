// ShootingModule end to end: settings schema, commands, activity gating, drill flows (flick, targets, power),
// robustness (pause, teleport, context change, invalid car), drawing budget and persistence.

#include <cmath>

#include "core/common/Units.h"
#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

// Ticks the harness with the world until the drill is live (setup + countdown).
void runToLive(Harness& h, World& w, int maxTicks = 900) {
    // The first attempt of a drill waits for the throttle (released once, then pressed), then counts down. The
    // throttle is let go after the press: with the ball balanced on a stopped car, GO waits for that.
    for (int i = 0; i < maxTicks && h.mod.phase() != ShootPhase::Live; ++i) {
        w.in.throttle = i >= 2 && i < 10 ? 1.f : 0.f;
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
    w.in.throttle = 0.f;
}

void runTicks(Harness& h, World& w, int n) {
    for (int i = 0; i < n; ++i) {
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
}

float yawDegOf(const Rot& r) { return static_cast<float>(r.yaw) * 360.f / 65536.f; }

// Scripted flick from the current world pose (stationary car with the ball on the roof).
void playFlick(Harness& h, World& w, FlickScript s) {
    s.start = w.car.pos;
    s.yawDeg = yawDegOf(w.car.rot);
    s.startFrame = w.frame;
    s.team = w.car.team;
    const ScriptRun run = buildFlick(s);
    for (size_t i = 0; i < run.snaps.size(); ++i) {
        for (const auto& t : run.touches)
            if (t.first == i) h.touch(t.second);
        h.tick(run.snaps[i]);
    }
    w.frame = run.snaps.back().frame + 1;
    w.car = run.snaps.back().car;
    w.ball = run.snaps.back().ball;
}

// Touch at the current world state that sends the ball to `target` (world point on the goal plane) in `t` s.
void shootAt(Harness& h, World& w, const Vec3& target, float t) {
    const Snapshot pre = w.snap();
    Vec3 v = (target - w.ball.pos) / t;
    v.z += 0.5f * 650.f * t;
    w.ball.vel = v;
    w.ballRides = false;
    Snapshot post = w.snap();
    h.touch(touchOf(pre, post, post));
}

ViewContext viewFor(const World& w) {
    ViewContext v;
    v.valid = true;
    v.camPos = w.car.pos + Vec3{0.f, -300.f, 150.f};
    v.camBasis = basisFromRot(rotDeg(-5.f, 90.f, 0.f));
    v.fovDeg = 110.f;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.carValid = true;
    v.carVisualPos = w.car.pos;
    v.carVisualBasis = w.car.basis;
    v.ballValid = true;
    v.ballVisualPos = w.ball.pos;
    v.ballVel = w.ball.vel;
    return v;
}

bool containsEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

} // namespace

// ---- Settings schema --------------------------------------------------------------------------------------
TEST(shooting_schema_defaults_parse_and_roundtrip) {
    SettingsSchema schema;
    ShootingModule::describeSettings(schema);
    Settings s;
    schema.applyDefaults(s);
    const auto defs = schema.forModule("shooting");
    CHECK(defs.size() >= 30);
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_sh_", 0) == 0, d->cvar);
        CHECK_MSG(!d->label.empty() && !d->tooltip.empty() && !d->section.empty(), d->cvar);
        CHECK_MSG(schema.apply(s, d->cvar, d->defaultValue), d->cvar);
        CHECK_MSG(d->read(s) == d->defaultValue, d->cvar + " read " + d->read(s) + " vs " + d->defaultValue);
        CHECK_MSG(!containsEmDash(d->label) && !containsEmDash(d->tooltip) && !containsEmDash(d->section), d->cvar);
        for (const auto& c : d->choices) CHECK(!containsEmDash(c));
        if (d->advanced) CHECK_MSG(d->section == "Avancé", d->cvar);
    }
    const auto sections = schema.sections("shooting");
    CHECK_EQ(sections.size(), static_cast<size_t>(6));
    CHECK(sections.front() == "Exercices");
    // Struct defaults equal schema defaults.
    const ShootingConfig def;
    CHECK(s.shooting.enabled == def.enabled);
    CHECK_NEAR(s.shooting.hudX, 0.01f, 1e-6f);
    CHECK_NEAR(s.shooting.hudY, 0.12f, 1e-6f);
    CHECK_EQ(s.shooting.targetZone, kZoneRandom);
    CHECK_NEAR(s.shooting.countdown, def.countdown, 1e-6f);
    CHECK_NEAR(s.shooting.mustyPitchDeg, def.mustyPitchDeg, 1e-6f);
    // Choices match the enums.
    CHECK_EQ(schema.find("rlt_sh_flick_type")->choices.size(), static_cast<size_t>(1 + kRequestableFlickCount));
    CHECK_EQ(schema.find("rlt_sh_target_zone")->choices.size(), static_cast<size_t>(kZoneCount + 1));
    CHECK_EQ(schema.find("rlt_sh_target_setup")->choices.size(), static_cast<size_t>(kTargetSetupCount));
    CHECK_EQ(schema.find("rlt_sh_unit")->choices.size(), static_cast<size_t>(4));
    // Bad values rejected, out of range values clamped.
    CHECK(!schema.apply(s, "rlt_sh_hud_x", "abc"));
    CHECK(schema.apply(s, "rlt_sh_hud_x", "5"));
    CHECK_NEAR(s.shooting.hudX, 1.f, 1e-6f);
    CHECK(schema.apply(s, "rlt_sh_target_zone", "42"));
    CHECK_EQ(s.shooting.targetZone, kZoneRandom);
}

// ---- Commands and gating ----------------------------------------------------------------------------------
TEST(shooting_gating_refused_without_world_control) {
    Harness h;
    h.game.worldControlAllowed = false;
    h.command("flick", {"musty"});
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK_EQ(h.host.requests, 0); // not even asked: the context forbids it
    CHECK(!h.toasts.empty() && h.toasts.back().kind == ToastKind::Warning);
    World w;
    const Actions a = h.tick(w.snap());
    CHECK(!a.setBall && !a.setCar && !a.freezeCar);

    Harness g;
    g.host.allow = false; // the engine refuses (another reason)
    g.command("targets");
    CHECK(g.mod.mode() == ShootMode::None);
    CHECK_EQ(g.host.requests, 1);
    CHECK(!g.toasts.empty());
}

TEST(shooting_gating_activity_names_stop_and_steal) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick");
    CHECK(h.mod.mode() == ShootMode::Flick);
    CHECK(h.host.lastName == "Flicks");
    CHECK(h.host.ownsActivity(&h.mod));
    h.command("targets", {"tl"});
    CHECK(h.host.lastName == "Cibles");
    CHECK(h.mod.mode() == ShootMode::Targets);
    h.command("power");
    CHECK(h.host.lastName == "Puissance");
    h.command("stop");
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK_EQ(h.host.releases, 1);
    CHECK(!h.host.ownsActivity(&h.mod));

    // Another module takes the activity: everything stops, no more world control.
    h.command("flick");
    runTicks(h, w, 5);
    h.host.steal();
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK(h.mod.phase() == ShootPhase::Idle);
    const Actions a = h.tick(w.snap());
    CHECK(!a.setBall && !a.setCar && !a.freezeCar && !a.overrideInputs);

    // Disabling the module releases the activity.
    h.command("flick");
    CHECK(h.host.ownsActivity(&h.mod));
    h.set("rlt_sh_enabled", "0");
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK(!h.host.ownsActivity(&h.mod));
    h.command("flick");
    CHECK(h.mod.mode() == ShootMode::None);
}

TEST(shooting_commands_arguments_and_errors) {
    Harness h;
    h.command("flick", {"ceiling"});
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK(!h.toasts.empty());
    h.toasts.clear();
    h.command("targets", {"nowhere"});
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK(!h.toasts.empty());
    h.toasts.clear();
    h.command("dance");
    CHECK(!h.toasts.empty());

    h.command("targets", {"center", "pass"});
    CHECK(h.mod.mode() == ShootMode::Targets);
    CHECK_EQ(h.mod.status().zoneSelection, 4);
    CHECK_EQ(h.mod.status().setupSelection, 3);
    h.command("setup", {"roof"});
    CHECK_EQ(h.mod.status().setupSelection, 4);
    h.command("zone", {"br"});
    CHECK_EQ(h.mod.status().zoneSelection, 3);
    h.command("flick", {"breezi"});
    CHECK(h.mod.mode() == ShootMode::Flick);
    CHECK_EQ(h.mod.status().requestedFlick, static_cast<int>(FlickType::Breezi));
    h.command("flick", {"any"});
    CHECK_EQ(h.mod.status().requestedFlick, -1);
    h.command("next");
    CHECK(h.mod.phase() == ShootPhase::Setup);
    const Actions r = h.command("reset_stats", {"session"});
    CHECK(r.requestSave);
}

// ---- Flick drill ------------------------------------------------------------------------------------------
TEST(shooting_flick_drill_spawn_countdown_and_release) {
    Harness h;
    h.set("rlt_sh_flick_start", "1"); // driving towards the goal
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    // Setup tick: car and ball placed, held still; the first attempt waits for the throttle.
    w.in.throttle = 1.f;  // still held from before
    const Actions setup = h.tick(w.snap());
    CHECK(setup.freezeCar);
    CHECK(setup.setCar && setup.setBall);
    CHECK(h.mod.phase() == ShootPhase::Countdown);
    if (!setup.setCar || !setup.setBall) return;
    const CarPlacement car = *setup.setCar;
    CHECK(car.boost && std::fabs(*car.boost - 100.f) < 1e-3f);
    CHECK_NEAR(length(car.vel), 0.f, 1e-3f);
    // Ball exactly above the roof with the 2 uu gap.
    const Basis b = basisFromRot(car.rot);
    const Vec3 local = b.toLocal(setup.setBall->pos - car.pos);
    const Hitbox hb = hitboxOf(w.car);
    CHECK_NEAR(local.x, hb.offset.x, 1e-2f);
    CHECK_NEAR(local.y, 0.f, 1e-2f);
    CHECK_NEAR(local.z, hb.roofZ() + 91.25f + 2.f, 1e-2f);
    // Facing the orange goal (blue team attacks +Y).
    CHECK(b.fwd.y > 0.9f);
    w.apply(setup);
    w.advance();
    // Waiting: held every tick until the throttle is pressed (a throttle held from before does not count).
    for (int i = 0; i < 240; ++i) {
        w.in.throttle = 1.f;
        const Actions a = h.tick(w.snap());
        CHECK(a.freezeCar && a.setCar && a.setBall);
        CHECK(!a.countdownMessage);
        w.apply(a);
        w.advance();
    }
    CHECK(h.mod.phase() == ShootPhase::Countdown);
    CHECK(h.mod.status().readyLine.find("accélérer") != std::string::npos);
    w.in.throttle = 0.f;
    h.tick(w.snap());
    w.advance();
    w.in.throttle = 1.f;
    // Countdown: held every tick, inputs frozen, native "3", "2", "1" then GO at the release.
    int countdownMsgs = 0;
    bool released = false;
    Actions rel;
    for (int i = 0; i < 500 && !released; ++i) {
        const Actions a = h.tick(w.snap());
        if (a.countdownMessage) ++countdownMsgs;
        if (h.mod.phase() == ShootPhase::Live) {
            released = true;
            rel = a;
        } else {
            CHECK(a.freezeCar);
            CHECK(a.setCar && a.setBall);
        }
        w.apply(a);
        w.advance();
    }
    CHECK(released);
    CHECK_EQ(countdownMsgs, 3);
    CHECK(rel.goMessage);
    w.in.throttle = 0.f;
    CHECK(rel.setCar && rel.setBall);
    if (rel.setCar && rel.setBall) {
        CHECK(length(rel.setCar->vel) > 299.f); // driving start: 300 uu/s on the first moving levels
        CHECK_NEAR(distance(rel.setCar->vel, rel.setBall->vel), 0.f, 1e-3f); // the ball moves with the car
        CHECK(!rel.freezeCar);
    }
}

TEST(shooting_stationary_flick_waits_for_the_throttle_release) {
    // Ball balanced on a stopped car: the player keeps the throttle held from his press through the countdown (the
    // kickoff habit). GO waits until he lets go (HUD "Lâche l'accélérateur pour partir"), then comes on that tick.
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    for (int i = 0; i < 8 * 120; ++i) {
        w.in.throttle = i < 2 ? 0.f : 1.f;
        const Actions a = h.tick(w.snap());
        CHECK(h.mod.phase() != ShootPhase::Live);
        CHECK(a.freezeCar);
        w.apply(a);
        w.advance();
    }
    CHECK(h.mod.phase() == ShootPhase::Countdown);
    CHECK_EQ(h.mod.status().readyLine, std::string("Lâche l'accélérateur pour partir"));
    w.in.throttle = 0.f;
    const Actions go = h.tick(w.snap());
    CHECK(h.mod.phase() == ShootPhase::Live);
    CHECK(go.setCar && go.setBall && !go.freezeCar);
    // A moving start (the car drives at GO, the ball rides with it) is not held back.
    Harness g;
    World v;
    g.set("rlt_sh_flick_start", "1");
    g.tick(v.snap());
    g.command("flick", {"front"});
    int live = -1;
    for (int i = 0; i < 8 * 120 && live < 0; ++i) {
        v.in.throttle = i < 2 ? 0.f : 1.f;
        const Actions a = g.tick(v.snap());
        v.apply(a);
        v.advance();
        if (g.mod.phase() == ShootPhase::Live) live = i;
    }
    CHECK(live >= 0 && live <= 2 + static_cast<int>(3.1f * kTickRate));
}

TEST(shooting_flick_drill_full_attempt) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"front"});
    runToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    w.ballRides = true;
    runTicks(h, w, 5);
    // Contacts of the carried ball are not shots.
    h.touch(touchOf(w.snap(), w.snap(), w.snap(), true));
    CHECK(!h.mod.status().hasLastShot);
    FlickScript s;
    s.launch = {2200.f, 0.f, 350.f};
    playFlick(h, w, s);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (h.results.empty()) return;
    const AttemptResult& r = h.results.front();
    CHECK(r.module == "shooting");
    CHECK(r.skill == "shoot.flick.front");
    CHECK(r.hasRecordValue);
    CHECK(metricOf(r, "power") > 80.f);
    CHECK(h.mod.phase() == ShootPhase::Result);
    CHECK_EQ(h.mod.stats().flickAll[static_cast<size_t>(FlickType::Front)].count, 1);
    CHECK_EQ(h.mod.flickStaircase().attempts(), 1);
    CHECK(h.mod.status().hasLastShot);
    CHECK(h.mod.status().lastFlick.find("Flick avant") == 0);
    // The impact marker follows the flicked ball during the result.
    CHECK(h.mod.hasPrediction());
    // After the result hold the next attempt is prepared automatically.
    runTicks(h, w, 200);
    CHECK(h.mod.phase() == ShootPhase::Countdown || h.mod.phase() == ShootPhase::Live);
}

TEST(shooting_flick_drill_wrong_type_and_lost_ball) {
    Harness h;
    Store st;  // niveau 4: the requested type counts (niveaux 1 and 2 accept any flick)
    st.setFloat("shooting.stair.flick.level", 1.f / 3.f);
    st.setInt("shooting.stair.flick.attempts", 1);
    h.mod.load(st);
    World w;
    h.tick(w.snap());
    h.command("flick", {"musty"});
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 5);
    playFlick(h, w, FlickScript{}); // a front flick, not a musty
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(h.results[0].score <= 55.f);
        CHECK(hasTip(h.results[0], "shooting.flick_musty_nose"));
    }
    // Next attempt: the ball rolls off the roof and falls.
    runTicks(h, w, 200);
    runToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    w.ballRides = false;
    w.ball.vel = {700.f, 0.f, 0.f};
    runTicks(h, w, 150);
    CHECK_EQ(h.results.size(), static_cast<size_t>(2));
    if (h.results.size() == 2) {
        CHECK(h.results[1].skill == "shoot.flick.musty");
        CHECK(!h.results[1].success);
        CHECK(h.results[1].tips.front().code == "shooting.flick_lost_ball");
    }
}

TEST(shooting_flick_drill_timeout_without_result) {
    Harness h;
    h.set("rlt_sh_attempt_timeout", "3");
    World w;
    h.tick(w.snap());
    h.command("flick");
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 380);
    CHECK(h.results.empty());
    CHECK(h.mod.phase() == ShootPhase::Countdown);
    CHECK(h.mod.status().message.find("Temps") == 0);
}

// ---- Targets drill ----------------------------------------------------------------------------------------
TEST(shooting_targets_hit_then_miss_moves_the_staircase) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("targets", {"tl", "rolling"});
    runToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    CHECK(h.mod.currentZone() == Zone::TopLeft);
    CHECK(h.mod.currentSetup() == TargetSetup::Rolling);
    const float level0 = h.mod.targetStaircase(TargetSetup::Rolling).level();
    const GoalFrame g = h.mod.goal();
    const ZoneRect tl = zoneRect(Zone::TopLeft, level0);
    shootAt(h, w, g.toWorld(tl.u, tl.z), 1.1f);
    CHECK(h.mod.hasPrediction());
    // The early prediction already points into the zone.
    CHECK(tl.contains(h.mod.prediction()));
    runTicks(h, w, 200);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (h.results.empty()) return;
    const AttemptResult& hit = h.results[0];
    CHECK(hit.skill == "shoot.target.tl");
    CHECK(hit.success);
    CHECK(metricOf(hit, "accuracy") >= 85.f);
    CHECK_EQ(h.mod.stats().zoneSession[0].hits, 1);
    CHECK_EQ(h.mod.stats().streak, 1);
    const float level1 = h.mod.targetStaircase(TargetSetup::Rolling).level();
    CHECK_NEAR(level1, level0 + 0.03f, 1e-4f);

    // Second attempt: wide to the right.
    runToLive(h, w);
    const ZoneRect tl1 = zoneRect(Zone::TopLeft, level1);
    shootAt(h, w, g.toWorld(tl1.u + 1100.f, 300.f), 1.1f);
    runTicks(h, w, 200);
    CHECK_EQ(h.results.size(), static_cast<size_t>(2));
    if (h.results.size() < 2) return;
    CHECK(!h.results[1].success);
    CHECK(h.results[1].tips.front().code == "shooting.target_right");
    CHECK(h.mod.targetStaircase(TargetSetup::Rolling).level() < level1);
    CHECK_EQ(h.mod.stats().streak, 0);
    CHECK_EQ(h.mod.stats().zoneSession[0].attempts, 2);
}

TEST(shooting_targets_orange_team_goal_event_and_untouched) {
    Harness h;
    World w;
    w.car.team = 1;
    w.car = carAt({0.f, 1000.f, 17.f}, rotDeg(0.f, -90.f, 0.f), 1);
    h.tick(w.snap());
    h.command("targets", {"br", "bouncing"});
    runToLive(h, w);
    CHECK(h.mod.goal().sign < 0.f);
    const ZoneRect br = zoneRect(Zone::LowRight, h.mod.targetStaircase(TargetSetup::Bouncing).level());
    const Vec3 target = h.mod.goal().toWorld(br.u, br.z);
    CHECK(target.x > 0.f); // orange attacks -Y: the attacker's right is +X
    shootAt(h, w, target, 0.9f);
    runTicks(h, w, 20);
    h.event(GameEventKind::Goal); // the game reports the goal before our crossing check
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) CHECK(h.results[0].skill == "shoot.target.br");

    // Untouched ball: no result, a new attempt after the timeout.
    h.set("rlt_sh_attempt_timeout", "3");
    runTicks(h, w, 200);
    runToLive(h, w);
    const size_t before = h.results.size();
    runTicks(h, w, 380);
    CHECK_EQ(h.results.size(), before);
    CHECK(h.mod.phase() == ShootPhase::Countdown);
    CHECK(h.mod.status().message.find("Balle non touch") == 0);
}

TEST(shooting_targets_own_goal_event_is_ignored) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("targets", {"tl", "rolling"});
    runToLive(h, w);
    // Shot towards our own goal (blue defends -Y): no prediction on the attacked goal.
    shootAt(h, w, Vec3{0.f, -5120.f, 300.f}, 1.5f);
    runTicks(h, w, 3);
    CHECK(!h.mod.hasPrediction());
    h.event(GameEventKind::Goal);
    CHECK(h.results.empty());
    CHECK(h.mod.phase() == ShootPhase::Live);
    // The ball moves away from the attacked goal: the attempt ends as a short shot.
    runTicks(h, w, 120);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) {
        CHECK(!h.results[0].success);
        CHECK(h.results[0].tips.front().code == "shooting.target_short");
    }
}

TEST(shooting_targets_timeout_never_cuts_a_shot_in_flight) {
    Harness h;
    h.set("rlt_sh_attempt_timeout", "3");
    World w;
    h.tick(w.snap());
    h.command("targets", {"center", "rolling"});
    runToLive(h, w);
    runTicks(h, w, 340); // 2.8 s
    const ZoneRect c = zoneRect(Zone::Center, h.mod.targetStaircase(TargetSetup::Rolling).level());
    shootAt(h, w, h.mod.goal().toWorld(c.u, c.z), 1.5f);
    runTicks(h, w, 240);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) CHECK(h.results[0].skill == "shoot.target.center");
}

TEST(shooting_targets_roof_setup_flick_annotates_result) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("targets", {"center", "roof"});
    runToLive(h, w);
    CHECK(h.mod.currentSetup() == TargetSetup::Roof);
    CHECK(h.mod.spawn().ballOnRoof);
    w.ballRides = true;
    runTicks(h, w, 5);
    FlickScript s;
    s.launch = {2300.f, 0.f, 250.f};
    playFlick(h, w, s);
    // The shot is tracked after the flick: keep the ball flying until it resolves.
    w.ballRides = false;
    runTicks(h, w, 400);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (!h.results.empty()) {
        CHECK(h.results[0].skill == "shoot.target.center");
        CHECK(h.results[0].title.find("Flick avant") != std::string::npos);
    }
}

// ---- Power drill ------------------------------------------------------------------------------------------
TEST(shooting_power_drill_records) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("power");
    runToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    // The ball rolls towards the player.
    CHECK(h.mod.spawn().ballReleaseVel.y < 0.f);
    auto shoot = [&](float speed) {
        w.car.dodging = true; // flip into the ball
        runTicks(h, w, 6);
        const Snapshot pre = w.snap();
        w.ball.vel = {0.f, speed, 200.f};
        const Snapshot post = w.snap();
        h.touch(touchOf(pre, post, post));
        w.car.dodging = false;
        runTicks(h, w, 40);
    };
    shoot(3200.f);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    if (h.results.empty()) return;
    CHECK(h.results[0].skill == "shoot.power");
    CHECK(h.results[0].success);
    CHECK_NEAR(h.results[0].valueForRecord, length(Vec3{0.f, 3200.f, 200.f}) * kUusToKph, 0.1f);
    CHECK_NEAR(metricOf(h.results[0], "flip"), 1.f, 1e-6f);
    CHECK(h.toasts.empty()); // the first shot is not announced as a record
    runTicks(h, w, 200);
    runToLive(h, w);
    shoot(3600.f);
    CHECK_EQ(h.results.size(), static_cast<size_t>(2));
    CHECK(!h.toasts.empty() && h.toasts.back().title == "Record !");
    CHECK_NEAR(h.mod.stats().powerAll.best, length(Vec3{0.f, 3600.f, 200.f}), 0.5f);
    CHECK_EQ(h.mod.stats().powerSession.count, 2);
}

TEST(shooting_power_drill_missed_ball_new_attempt) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("power");
    runToLive(h, w);
    // The player does not move: the ball rolls past.
    runTicks(h, w, 900);
    CHECK(h.results.empty());
    CHECK(h.mod.mode() == ShootMode::Power);
}

// ---- Robustness ---------------------------------------------------------------------------------------------
TEST(shooting_robust_pause_teleport_reset_context) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("targets", {"tl", "rolling"});
    runToLive(h, w);
    // Paused: nothing moves, nothing is emitted.
    for (int i = 0; i < 50; ++i) {
        Snapshot s = w.snap();
        s.paused = true;
        const Actions a = h.tick(s);
        CHECK(a.empty());
        ++w.frame;
    }
    CHECK(h.mod.phase() == ShootPhase::Live);
    // Teleport during the attempt (another plugin, map script): new attempt, no result.
    w.car.pos += Vec3{1500.f, 0.f, 0.f};
    runTicks(h, w, 2);
    CHECK(h.results.empty());
    CHECK(h.mod.phase() == ShootPhase::Countdown || h.mod.phase() == ShootPhase::Setup);
    runToLive(h, w);
    h.event(GameEventKind::ResetShot);
    CHECK(h.mod.phase() == ShootPhase::Setup);
    // Map change keeping world control: new attempt. Losing world control: stop and release.
    runToLive(h, w);
    h.event(GameEventKind::ContextChanged);
    CHECK(h.mod.phase() == ShootPhase::Setup);
    h.game.worldControlAllowed = false;
    h.event(GameEventKind::ContextChanged);
    CHECK(h.mod.mode() == ShootMode::None);
    CHECK(!h.host.ownsActivity(&h.mod));
    h.game.worldControlAllowed = true;
    h.command("flick");
    h.event(GameEventKind::MapUnloaded);
    CHECK(h.mod.mode() == ShootMode::None);
}

TEST(shooting_robust_invalid_car_and_ball) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick");
    Snapshot s = w.snap();
    s.car.valid = false;
    s.ball.valid = false;
    const Actions a = h.tick(s);
    CHECK(!a.setCar && !a.setBall);
    CHECK(h.mod.phase() == ShootPhase::Setup); // waits for a car
    runToLive(h, w);
    CHECK(h.mod.phase() == ShootPhase::Live);
    for (int i = 0; i < 30; ++i) {
        Snapshot t = w.snap();
        t.ball.valid = false;
        h.tick(t);
        ++w.frame;
    }
    CHECK(h.results.empty());
}

TEST(shooting_free_analysis_and_shooting_maps) {
    Harness h;
    World w;
    h.tick(w.snap());
    // Off by default: a shot shows nothing.
    w.ball = ballAt({0.f, 2000.f, 300.f});
    shootAt(h, w, Vec3{0.f, 5120.f, 300.f}, 1.f);
    CHECK(!h.mod.hasPrediction());
    CHECK(h.mod.status().hasLastShot);
    // Enabled: predicted impact, never a graded result.
    h.set("rlt_sh_free_analysis", "1");
    w.ball = ballAt({0.f, 2000.f, 300.f});
    runTicks(h, w, 2);
    shootAt(h, w, Vec3{0.f, 5120.f, 300.f}, 1.f);
    CHECK(h.mod.hasPrediction());
    runTicks(h, w, 150);
    CHECK(h.results.empty());
    // Shooting workshop map (analysis only): on even without the option.
    h.set("rlt_sh_free_analysis", "0");
    h.game.category = MapCategory::Shooting;
    h.game.mode = GameMode::Workshop;
    w.ball = ballAt({0.f, 2000.f, 300.f});
    runTicks(h, w, 2);
    shootAt(h, w, Vec3{0.f, 5120.f, 300.f}, 1.f);
    CHECK(h.mod.hasPrediction());
}

// ---- Drawing ----------------------------------------------------------------------------------------------
TEST(shooting_draw_budget_panel_and_modes) {
    Harness h;
    World w;
    h.tick(w.snap());
    // Idle, panel only during drills by default.
    DrawList idle = h.draw(viewFor(w));
    CHECK(idle.panels.empty() && idle.lines3D.empty());
    h.set("rlt_sh_hud_mode", "1");
    CHECK_EQ(h.draw(viewFor(w)).panels.size(), static_cast<size_t>(1));
    h.set("rlt_sh_hud_mode", "0");

    h.command("targets", {"tl", "rolling"});
    runToLive(h, w);
    DrawList live = h.draw(viewFor(w));
    CHECK_EQ(live.panels.size(), static_cast<size_t>(1));
    if (!live.panels.empty()) {
        const DrawPanel& p = live.panels[0];
        CHECK_NEAR(p.pos.x, 0.01f * 1920.f, 1e-3f);
        CHECK_NEAR(p.pos.y, 0.12f * 1080.f, 1e-3f);
        CHECK(p.title.find("Cibles") != std::string::npos);
        CHECK(p.lines.size() >= 3 && p.lines.size() <= 12);
        bool hasLevelBar = false;
        for (const auto& l : p.lines) {
            CHECK(!containsEmDash(l.text));
            if (l.bar >= 0.f) hasLevelBar = true;
        }
        CHECK(hasLevelBar);
    }
    // 5 zones: 4 lines each + 4 more for the highlighted one.
    CHECK_EQ(live.lines3D.size(), static_cast<size_t>(24));
    h.set("rlt_sh_show_zones", "0");
    CHECK_EQ(h.draw(viewFor(w)).lines3D.size(), static_cast<size_t>(8));
    h.set("rlt_sh_show_zones", "1");
    // Shot in flight: the predicted impact marker is added.
    const GoalFrame g = h.mod.goal();
    shootAt(h, w, g.toWorld(0.f, 300.f), 1.2f);
    runTicks(h, w, 3);
    DrawList flying = h.draw(viewFor(w));
    CHECK(flying.lines3D.size() >= 24 + 20);
    bool predictionLine = false;
    for (const auto& l : flying.panels.front().lines)
        if (l.text.find("Impact prévu") == 0) predictionLine = true;
    CHECK(predictionLine);
    CHECK(flying.lines3D.size() < 1500);
    for (const auto& l : flying.lines3D) {
        CHECK(std::isfinite(l.a.x) && std::isfinite(l.b.y) && std::isfinite(l.a.z));
        CHECK(std::fabs(std::fabs(l.a.y) - 5120.f) < 20.f); // everything lies on the goal plane
    }
    h.set("rlt_sh_hud_mode", "2");
    CHECK(h.draw(viewFor(w)).panels.empty());
}

TEST(shooting_draw_flick_roof_guide_and_replay) {
    Harness h;
    World w;
    h.tick(w.snap());
    h.command("flick", {"musty"});
    runToLive(h, w);
    w.ballRides = true;
    runTicks(h, w, 3);
    const DrawList d = h.draw(viewFor(w));
    // Goal mouth outline (4) + roof guide circle (14), occluded by the ball.
    CHECK(d.lines3D.size() >= 18);
    bool occluded = false;
    for (const auto& l : d.lines3D)
        if (l.occluder.enabled) occluded = true;
    CHECK(occluded);
    CHECK(d.lines3D.size() < 1500);
    // The HUD reminds the requested flick with the player's buttons.
    CHECK(!d.panels.empty());
    bool cue = false;
    if (!d.panels.empty())
        for (const auto& l : d.panels[0].lines)
            if (l.text.find("nez en bas") != std::string::npos) cue = true;
    CHECK(cue);

    // Replay: no ticks, drawn from the view only when the analysis option is on.
    Harness r;
    r.game.mode = GameMode::Replay;
    ViewContext v = viewFor(w);
    v.ballVisualPos = {0.f, 3000.f, 300.f};
    v.ballVel = {0.f, 2500.f, 200.f};
    CHECK(r.draw(v).empty());
    r.set("rlt_sh_free_analysis", "1");
    const DrawList rep = r.draw(v);
    CHECK(rep.lines3D.size() >= 20);
    CHECK(rep.panels.empty());
    v.ballVel = {0.f, 100.f, 0.f}; // slow ball: nothing
    CHECK(r.draw(v).empty());
}

// ---- Results feedback and persistence ---------------------------------------------------------------------
TEST(shooting_chat_and_save_load_roundtrip) {
    Harness h;
    h.set("rlt_sh_chat_results", "1");
    World w;
    h.tick(w.snap());
    h.command("targets", {"tl", "rolling"});
    runToLive(h, w);
    const GoalFrame g = h.mod.goal();
    const ZoneRect tl = zoneRect(Zone::TopLeft, h.mod.targetStaircase(TargetSetup::Rolling).level());
    shootAt(h, w, g.toWorld(tl.u, tl.z), 1.1f);
    runTicks(h, w, 200);
    CHECK_EQ(h.results.size(), static_cast<size_t>(1));
    CHECK_EQ(h.chat.size(), static_cast<size_t>(1));
    if (!h.chat.empty()) CHECK(h.chat[0].text.find("Cible lucarne gauche") == 0);

    Store store;
    h.mod.save(store);
    CHECK(!store.empty());
    for (const auto& kv : store.raw()) CHECK_MSG(kv.first.rfind("shooting.", 0) == 0, kv.first);
    Harness other;
    other.mod.load(store);
    CHECK_EQ(other.mod.stats().zoneAll[0].attempts, h.mod.stats().zoneAll[0].attempts);
    CHECK_EQ(other.mod.stats().zoneAll[0].hits, 1);
    CHECK_NEAR(other.mod.targetStaircase(TargetSetup::Rolling).level(),
               h.mod.targetStaircase(TargetSetup::Rolling).level(), 1e-4f);
    CHECK_EQ(other.mod.targetStaircase(TargetSetup::Rolling).attempts(), 1);
    CHECK_EQ(other.mod.stats().zonePrev[0].attempts, 1);

    other.command("reset_stats");
    CHECK_EQ(other.mod.stats().zoneAll[0].attempts, 0);
    CHECK_NEAR(other.mod.targetStaircase(TargetSetup::Rolling).level(), kShootStartLevel, 1e-6f);
}

TEST(shooting_fixed_level_when_not_adaptive) {
    Harness h;
    h.set("rlt_sh_target_adaptive", "0");
    h.set("rlt_sh_target_level", "0.8");
    CHECK_NEAR(h.mod.targetLevel(TargetSetup::Pass), 0.8f, 1e-6f);
    h.set("rlt_sh_flick_adaptive", "0");
    CHECK_NEAR(h.mod.flickLevel(), 0.3f, 1e-6f);
    const ShootingStatus st = h.mod.status();
    CHECK_NEAR(st.setupLevels[static_cast<size_t>(TargetSetup::Roof)], 0.8f, 1e-6f);
}
