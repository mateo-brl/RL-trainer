#pragma once
// Module-level test rig: drives MechanicsModule like the engine does. The fake host reproduces Engine's rules:
// world control (placements, inputs, game speed) routed only from the activity owner and only where world control is
// allowed (checked after the callback, as Engine::route does), the previous owner's onActivityStopped on a handover,
// and the context order of Engine::setContext (world control recomputed, activity stopped, THEN ContextChanged).
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
// callback, as Engine::route does), placements applied to the MiniSim, touches near the ball.
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
    bool touched = false;
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
        return TickContext{last, history, settings, game, profile, hasSnap, host.ownsActivity(&mod), game.worldControlAllowed};
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
    Actions tick(const Inputs& playerIn, bool paused = false) {
        sim.tick(frozen ? Inputs{} : playerIn);
        if (frozen) {
            sim.vel = {};
            sim.w = {};
        }
        last = sim.snap(frame++, playerIn);
        last.ball.pos = ballGame;
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
            touched = false;
        }
        // The snapshot predates a placement routed this tick: no touch test against the old car position.
        if (!routed.setCar && !touched && length(last.car.pos - ballGame) < 200.f) {
            touched = true;
            TouchEvent t;
            t.frame = last.frame;
            t.pre = last;
            t.post1 = last;
            t.post = last;
            Actions ta;
            mod.onTouch(t, ctx(), ta);
            route(ta);
        }
        return routed;
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

// Dash at the landing of a drop (wavedash drill): no first jump, tilt then dodge near the floor.
inline Script dropWavedash() {
    auto S = std::make_shared<WdS>();
    return [=](int, MiniSim& c) {
        Inputs in;
        in.throttle = 1.f;
        if (S->phase == 0 && c.wheelCount() == 0) {
            in.pitch = pitchToNose(c, 20.f);
            if (c.vel.z < 0.f && c.rearWheelZ() < 6.f) {
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

inline Script idle() {
    return [](int, MiniSim&) {
        Inputs in;
        return in;
    };
}

inline bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

} // namespace rlt_test_mech
