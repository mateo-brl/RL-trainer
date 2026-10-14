#pragma once
// Module-level test rig: drives MechanicsModule like the engine does. The fake host reproduces Engine's rules:
// world control (placements, inputs, game speed) routed only from the activity owner and only where world control is
// allowed (checked after the callback, as Engine::route does), the previous owner's onActivityStopped on a handover,
// the context order of Engine::setContext (world control recomputed, activity stopped, THEN ContextChanged),
// TickContext::otherActivityRunning, and the engine's game speed restore whenever an activity ends (gameSpeed).
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "tests/TestFramework.h"
#include "tests/mechanics/MiniSim.h"

#include "core/common/Settings.h"
#include "core/modules/mechanics/MechanicsModule.h"
#include "core/modules/mechanics/Tips.h"

namespace rlt_test_mech {

using namespace rlt;
using namespace rlt::mech;

struct FakeHost : ActivityHost {
    bool allow = true;              // false: refuses every request (e.g. world control not allowed)
    const bool* worldControl = nullptr;  // like Engine::requestActivity: refused when world control is off
    Module* owner = nullptr;
    int requests = 0, releases = 0, stops = 0;
    std::string name;
    float gameSpeed = 1.f;      // speed the game runs at (routed speeds and engine restores)
    int engineRestores = 0;     // Engine::markWorldRestore / flushWorldRestore: speed put back to 1 by the engine
    // Engine: the activity ended (release, stop, handover) while the speed was modified: back to 1.
    void activityEnded() {
        if (std::fabs(gameSpeed - 1.f) > 1e-3f) {
            gameSpeed = 1.f;
            ++engineRestores;
        }
    }
    bool requestActivity(Module* m, const std::string& n) override {
        ++requests;
        if (!allow || !m || (worldControl && !*worldControl)) return false;
        if (owner == m) {
            name = n;
            return true;
        }
        if (owner) {  // Engine: the previous owner is stopped first (owner cleared before the callback)
            Module* old = owner;
            owner = nullptr;
            name.clear();
            activityEnded();
            old->onActivityStopped();
        }
        owner = m;
        name = n;
        return true;
    }
    void releaseActivity(Module* m) override {
        ++releases;
        if (owner == m) {
            owner = nullptr;
            name.clear();
            activityEnded();
        }
    }
    bool ownsActivity(const Module* m) const override { return m && owner == m; }
    // Engine::stopActivity (UI stop button, map unloaded, world control lost).
    void stopActivity() {
        if (!owner) return;
        ++stops;
        Module* old = owner;
        owner = nullptr;
        name.clear();
        activityEnded();
        old->onActivityStopped();
    }
};

// Another module that can take the activity (handover) and route its own world control.
struct OtherModule : Module {
    const char* id() const override { return "kickoff"; }
    const char* title() const override { return "Kickoff"; }
    bool start(const std::string& n) { return requestActivity(n); }
    void stop() { releaseActivity(); }
};

// Drives the module like the engine does: tick context, routing filter on world control (checked after the
// callback, as Engine::route does), placements applied to the MiniSim, a simple ball (gravity, floor bounce, pushed
// on a touch) and touches resolved with the car hitbox (OBB, the player's hitbox from the profile).
struct Rig {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    FakeHost host;
    MechanicsModule mod;
    MiniSim sim;
    int64_t frame = 1000;
    Snapshot last;
    bool hasSnap = false;
    bool frozen = false;
    bool ballAtCentre = false;   // the drill ball (game axes, centre spot) for touches
    Vec3 ballGame{1e5f, 1e5f, 92.75f};
    Vec3 ballVel;                // game axes
    bool ballPhysics = true;     // gravity and floor bounce between placements
    bool touched = false;
    int touchFrame = -1;         // CarState::lastBallTouchFrame of the next snapshots
    float touchTime = -1.f;      // game time of the first touch since the last ball placement
    float touchBallZ = 0.f;
    float closestGap = 1e9f;     // closest hitbox to ball surface distance since the last ball placement
    Actions raw, routed;
    std::vector<AttemptResult> results;
    std::vector<ToastMessage> toasts;
    std::vector<ChatMessage> chat;
    int setCars = 0, speedChanges = 0;
    float lastSpeed = 1.f;
    std::vector<float> speeds;  // every routed game speed, in order (what the game received)

    Rig() {
        MechanicsModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.mapName = "Park_P";
        game.worldControlAllowed = true;
        host.worldControl = &game.worldControlAllowed;
        mod.attach(&host);
        mod.onSettingsChanged(settings);
        mod.onProfileChanged(profile);
        last = sim.snap(frame, {});
        last.ball.pos = ballGame;
        GameEvent ev;
        ev.kind = GameEventKind::ContextChanged;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        route(a);
    }
    TickContext ctx() const {
        const bool owns = host.ownsActivity(&mod);
        return TickContext{last, history, settings, game, profile, hasSnap, owns, game.worldControlAllowed,
                           host.owner != nullptr && !owns};
    }
    Actions route(Actions a) {
        if (!game.worldControlAllowed || !host.ownsActivity(&mod)) a.clearWorldControl();
        for (auto& r : a.results) results.push_back(r);
        for (auto& t : a.toasts) toasts.push_back(t);
        for (auto& c : a.chat) chat.push_back(c);
        if (a.setCar) ++setCars;
        if (a.setGameSpeed) {
            ++speedChanges;
            lastSpeed = *a.setGameSpeed;
            speeds.push_back(*a.setGameSpeed);
            host.gameSpeed = *a.setGameSpeed;
        }
        return a;
    }
    // Engine::setContext: world control recomputed and the activity stopped BEFORE ContextChanged is dispatched.
    Actions setContext(GameMode mode, const std::string& map, bool worldControl) {
        game.mode = mode;
        game.mapName = map;
        game.worldControlAllowed = worldControl;
        if (!worldControl) host.stopActivity();
        return event(GameEventKind::ContextChanged);
    }
    // Engine::gameEvent(MapUnloaded): history cleared, activity stopped, then the event.
    Actions mapUnloaded() {
        hasSnap = false;
        host.stopActivity();
        return event(GameEventKind::MapUnloaded);
    }
    void set(const std::string& cvar, const std::string& value) {
        CHECK_MSG(schema.apply(settings, cvar, value), cvar);
        mod.onSettingsChanged(settings);
    }
    void setProfile(const PlayerProfile& p) {
        profile = p;
        mod.onProfileChanged(profile);
    }
    // Distance between the car hitbox and the ball surface in the last snapshot (negative: overlapping).
    float hitboxGapNow() const {
        return mech::hitboxGap(last.car, ballGame, profile.hitboxExtent * 0.5f, profile.hitboxOffset);
    }
    void stepBall() {
        if (!ballPhysics) return;
        ballVel.z += k::kGravityZ * k::kDt;
        ballGame += ballVel * k::kDt;
        if (ballGame.z < 92.75f) {
            ballGame.z = 92.75f;
            ballVel.z = ballVel.z < -60.f ? -ballVel.z * 0.6f : 0.f;
        }
    }
    Actions tick(const Inputs& playerIn, bool paused = false) {
        sim.tick(frozen ? Inputs{} : playerIn);
        if (frozen) {
            sim.vel = {};
            sim.w = {};
        }
        if (!paused) stepBall();
        last = sim.snap(frame++, playerIn);
        last.ball.pos = ballGame;
        last.ball.vel = ballVel;
        last.car.lastBallTouchFrame = touchFrame;
        last.paused = paused;
        history.push(last);
        hasSnap = true;
        Actions a;
        mod.onTick(ctx(), a);
        raw = a;
        routed = route(a);
        frozen = routed.freezeCar;
        if (routed.setCar) sim.place(*routed.setCar);
        if (routed.setBall) {
            ballGame = routed.setBall->pos;
            ballVel = routed.setBall->vel;
            touched = false;
            touchTime = -1.f;
            closestGap = 1e9f;
        }
        // The snapshot predates a placement routed this tick: no touch test against the old car position.
        if (!routed.setCar) {
            const float gap = hitboxGapNow();
            closestGap = std::min(closestGap, gap);
            if (!touched && gap <= 0.f) {
                touched = true;
                touchFrame = static_cast<int>(last.frame);
                if (touchTime < 0.f) {
                    touchTime = last.time;
                    touchBallZ = ballGame.z;
                }
                // The ball leaves with the car's speed plus a push away from the car.
                ballVel = last.car.vel * 1.1f + normalize(ballGame - last.car.pos) * 400.f;
                TouchEvent t;
                t.frame = last.frame;
                t.pre = last;
                t.post1 = last;
                t.post = last;
                Actions ta;
                mod.onTouch(t, ctx(), ta);
                route(ta);
            }
        }
        return routed;
    }
    // Feeds a hand-made snapshot (kinematic sequences the MiniSim cannot make: walls, suspension bounce) instead of
    // the simulated car. Frame and time follow the rig's clock.
    Actions feed(Snapshot s) {
        s.frame = frame++;
        s.time = static_cast<float>(s.frame) / 120.f;
        s.car.valid = true;
        s.ball.valid = true;
        s.ball.pos = ballGame;
        last = s;
        history.push(last);
        hasSnap = true;
        Actions a;
        mod.onTick(ctx(), a);
        raw = a;
        routed = route(a);
        return routed;
    }
    // Starts the drill of `id` (optionally at a saved `level`, or its "parcours complet") and plays the get-ready
    // phase like a player: throttle released once, then pressed (with `hold`, e.g. boost for a kickoff) until GO.
    // Returns true when the attempt runs.
    bool startDrill(const std::string& id, float level = -1.f, bool full = false, Inputs hold = Inputs{}) {
        if (level >= 0.f) {
            Store st;
            const std::string key = std::string(full ? "mechanics.full." : "mechanics.level.") + id;
            st.setFloat(key + ".level", level);
            st.setInt(key + ".attempts", 1);  // a played level
            mod.load(st);
        }
        std::vector<std::string> args{id};
        if (full) args.push_back("full");
        command("drill", args);
        return waitGo(hold);
    }
    // Plays a get-ready phase (first attempt or later) until GO; the throttle starts it when the drill waits for it.
    bool waitGo(Inputs hold = Inputs{}) {
        hold.throttle = 0.f;
        tick(hold);  // placement
        tick(hold);  // throttle seen released
        hold.throttle = 1.f;
        for (int i = 0; i < 6 * 120; ++i) {
            if (mod.status().phase == "Essai en cours") return true;
            tick(hold);
        }
        return mod.status().phase == "Essai en cours";
    }
    void run(int n, const Script& s) {
        for (int i = 0; i < n; ++i) tick(s(i, sim));
    }
    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "mechanics";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        raw = a;
        routed = route(a);
        return routed;
    }
    Actions event(GameEventKind k) {
        GameEvent ev;
        ev.kind = k;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        return route(a);
    }
    const AttemptResult* result(const std::string& skill) const {
        for (const auto& r : results)
            if (r.skill == skill) return &r;
        return nullptr;
    }
    int count(const std::string& skill) const {
        int n = 0;
        for (const auto& r : results) n += r.skill == skill ? 1 : 0;
        return n;
    }
    bool toastContains(const std::string& part) const {
        for (const auto& t : toasts)
            if (t.title.find(part) != std::string::npos || t.text.find(part) != std::string::npos) return true;
        return false;
    }
};

struct WdS {
    int phase = 0, dodgeT = -1, jumpT = -1;
};

inline Script groundWavedash(int startTick = 10) {
    auto S = std::make_shared<WdS>();
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (S->phase == 0 && t >= startTick) {
            S->phase = 1;
            S->jumpT = t;
        }
        if (S->phase == 1) {
            in.jump = t - S->jumpT < 3;
            if (t - S->jumpT >= 3) {
                in.pitch = pitchToNose(c, 20.f);
                if (c.vel.z < 0.f && c.rearWheelZ() < 6.f) {
                    S->phase = 2;
                    S->dodgeT = t;
                }
            }
        }
        if (S->phase == 2) {
            in.jump = t - S->dodgeT < 3;
            in.pitch = t - S->dodgeT < 3 ? -1.f : 0.f;
        }
        return in;
    };
}

// Dash at the landing of a drop (wavedash drill): no first jump, tilt then dodge near the floor (the rear wheels
// less than 6 uu above it on the next tick, whatever the fall speed).
inline Script dropWavedash() {
    auto S = std::make_shared<WdS>();
    return [=](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (S->phase == 0 && c.wheelCount() == 0) {
            in.pitch = pitchToNose(c, 20.f);
            if (c.vel.z < 0.f && c.rearWheelZ() + c.vel.z * k::kDt < 6.f) {
                S->phase = 1;
                S->dodgeT = 0;
            }
        }
        if (S->phase == 1 && S->dodgeT < 3) {
            in.jump = true;
            in.pitch = -1.f;
            S->dodgeT++;
        }
        return in;
    };
}

// Fast aerial toward the rig's ball: `reaction` ticks after the start, jump held `hold` ticks with boost and the stick
// pulled, double jump `gap` ticks after the release, then boost and aim at where the ball will be (lead).
inline Script fastAerialAt(const Rig* rig, int reaction, int hold, int gap) {
    return [=](int t, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        const int J = reaction, DJ = J + hold + gap;
        in.boost = t >= J;
        if (t >= J && t < J + hold) {
            in.jump = true;
            in.pitch = 1.f;
        }
        if (t >= J + hold && t < DJ) in.pitch = 1.f;
        if (t >= DJ && t < DJ + 2) in.jump = true;
        if (t >= DJ + 2) {
            const Vec3 ball = mirrorY(rig->ballGame), bv = mirrorY(rig->ballVel);
            float tau = 0.3f;
            Vec3 q = ball;
            for (int k = 0; k < 3; ++k) {
                q = ball + bv * tau;
                q.z += 0.5f * k::kGravityZ * tau * tau;
                tau = length(q - c.pos) / 1400.f;
            }
            const Vec3 to = normalize(q - c.pos);
            const Vec3 e = cross(c.fwd(), to) + cross(c.up(), Vec3{0.f, 0.f, 1.f}) * 0.2f;
            const Vec3 acc = e * 16.f - c.w * 4.5f;
            in.pitch = clampf(dot(acc, c.right()) / 12.46f, -1.f, 1.f);
            in.yaw = clampf(dot(acc, -c.up()) / 9.11f, -1.f, 1.f);
            in.roll = clampf(dot(acc, c.fwd()) / 38.34f, -1.f, 1.f);
        }
        return in;
    };
}

inline Script idle() {
    return [](int, MiniSim&) {
        Inputs in;
        return in;
    };
}

inline bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

} // namespace rlt_test_mech
