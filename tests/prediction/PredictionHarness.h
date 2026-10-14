#pragma once
// Test harness for PredictionModule: owns every callback argument (snapshot, history, settings, context,
// profile), a fake ActivityHost, and advances a synthetic ball with the same physics as the module so that a
// free ball never deviates from its prediction.

#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/prediction/BallPhysics.h"
#include "core/modules/prediction/PredictionModule.h"

namespace rlt_test_pr {

using namespace rlt;

class FakeHost : public ActivityHost {
public:
    int requests = 0;
    int releases = 0;
    bool requestActivity(Module*, const std::string&) override {
        ++requests;
        return true;
    }
    void releaseActivity(Module*) override { ++releases; }
    bool ownsActivity(const Module*) const override { return false; }
};

inline bool hasWorldControl(const Actions& a) {
    return a.setBall || a.setCar || a.setBoost || a.overrideInputs || a.freezeCar || a.setGameSpeed ||
           a.countdownMessage || a.goMessage;
}

struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    Snapshot snap;
    bool hasSnapshot = true;
    FakeHost host;
    PredictionModule mod;
    int worldControlActions = 0;

    Harness() {
        PredictionModule::describeSettings(schema);
        schema.applyDefaults(settings);
        // The module is off by default: these tests drive it switched on, like a player who turned it on, whose
        // saved settings were already moved (the one-time move has its own test, pr_old_default_migrated_once).
        schema.apply(settings, "rlt_pr_enabled", "1");
        Store migrated;
        migrated.setInt("prediction.defaults_v2", 1);
        mod.load(migrated);
        game.mode = GameMode::Freeplay;
        game.worldControlAllowed = true;
        mod.attach(&host);
        mod.onSettingsChanged(settings);
        snap.frame = 1000;
        snap.time = static_cast<float>(snap.frame) / kTickRate;
        snap.ball.valid = true;
        snap.car.valid = true;
        snap.car.pos = {0.f, -2000.f, 17.f};
        snap.car.basis = basisFromRot(rotFromYawDeg(90.f));  // facing +Y
        snap.car.onGround = true;
        snap.car.wheelContacts = 4;
        snap.car.boost = 33.f;
        snap.car.team = 0;
    }

    bool set(const std::string& cvar, const std::string& value) {
        const bool ok = schema.apply(settings, cvar, value);
        mod.onSettingsChanged(settings);
        return ok;
    }

    TickContext tc() const {
        return TickContext{snap, history, settings, game, profile, hasSnapshot, false, game.worldControlAllowed};
    }

    Actions tick() {
        history.push(snap);
        Actions a;
        mod.onTick(tc(), a);
        if (hasWorldControl(a)) ++worldControlActions;
        return a;
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "prediction";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, tc(), a);
        if (hasWorldControl(a)) ++worldControlActions;
        return a;
    }

    Actions event(GameEventKind kind) {
        GameEvent ev;
        ev.kind = kind;
        ev.frame = snap.frame;
        Actions a;
        mod.onGameEvent(ev, tc(), a);
        return a;
    }

    ViewContext view() const {
        ViewContext v;
        v.valid = true;
        v.camPos = snap.ball.pos + Vec3{0.f, -1500.f, 400.f};
        v.camBasis = basisFromRot(rotFromDirection(snap.ball.pos - v.camPos));
        v.carValid = snap.car.valid;
        v.carVisualPos = snap.car.pos;
        v.ballValid = snap.ball.valid;
        v.ballVisualPos = snap.ball.pos;
        v.ballVel = snap.ball.vel;
        return v;
    }

    DrawList draw() { return drawWith(view()); }

    DrawList drawWith(const ViewContext& v) {
        DrawContext dc{settings, game, profile, snap, hasSnapshot, false, 1.f};
        DrawList d;
        mod.draw(v, dc, d);
        return d;
    }

    void setBall(const Vec3& pos, const Vec3& vel = {}, const Vec3& angVel = {}) {
        snap.ball.pos = pos;
        snap.ball.vel = vel;
        snap.ball.angVel = angVel;
    }

    // Advances the snapshot by one physics tick, moving the ball with the module's own physics.
    void advance(pr::ArenaMode mode = pr::ArenaMode::Soccar) {
        pr::SimBall b;
        b.pos = snap.ball.pos;
        b.vel = snap.ball.vel;
        b.angVel = snap.ball.angVel;
        pr::stepBall(b, pr::SimParams::forRadius(snap.ball.radius), mode);
        snap.ball.pos = b.pos;
        snap.ball.vel = b.vel;
        snap.ball.angVel = b.angVel;
        ++snap.frame;
        snap.time = static_cast<float>(snap.frame) / kTickRate;
    }

    // n ticks of a free ball.
    void run(int n, pr::ArenaMode mode = pr::ArenaMode::Soccar) {
        for (int i = 0; i < n; ++i) {
            advance(mode);
            tick();
        }
    }
};

inline bool anyText(const DrawList& d, const std::string& needle) {
    for (const auto& t : d.texts3D)
        if (t.text.find(needle) != std::string::npos) return true;
    for (const auto& t : d.texts2D)
        if (t.text.find(needle) != std::string::npos) return true;
    for (const auto& p : d.panels) {
        if (p.title.find(needle) != std::string::npos) return true;
        for (const auto& l : p.lines)
            if (l.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

inline bool sameRgb(const Color& a, const Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

} // namespace rlt_test_pr
