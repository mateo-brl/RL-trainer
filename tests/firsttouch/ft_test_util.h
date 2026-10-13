#pragma once
// Shared helpers for the first-touch tests: synthetic car/ball states, a fake ActivityHost and a harness
// reproducing the engine's dispatch order (onTouch then onTick in the same physics tick).

#include <string>
#include <vector>

#include "core/common/Settings.h"
#include "core/modules/firsttouch/FirstTouchModule.h"
#include "tests/TestFramework.h"

namespace fttest {

using namespace rlt;

constexpr float kCarZ = 17.01f;
constexpr float kBallZ = 93.15f;
// Octane hitbox, ball resting on the ground touching the top-front edge of the nose (physics.md 6.3).
constexpr float kNoseContactX = 156.15f;

inline CarState groundCar(Vec3 pos, Vec3 vel, float yawDeg = 0.f) {
    CarState c;
    c.valid = true;
    c.pos = pos;
    c.vel = vel;
    c.rot = rotFromYawDeg(yawDeg);
    c.basis = basisFromRot(c.rot);
    c.onGround = true;
    c.wheelContacts = 4;
    c.boost = 50.f;
    c.hasFlip = true;
    return c;
}

inline CarState airCar(Vec3 pos, Vec3 vel, Rot rot = {}) {
    CarState c = groundCar(pos, vel);
    c.rot = rot;
    c.basis = basisFromRot(rot);
    c.onGround = false;
    c.wheelContacts = 0;
    return c;
}

inline BallState makeBall(Vec3 pos, Vec3 vel) {
    BallState b;
    b.valid = true;
    b.pos = pos;
    b.vel = vel;
    return b;
}

inline Snapshot makeSnap(int64_t frame, const CarState& car, const BallState& ball) {
    Snapshot s;
    s.frame = frame;
    s.time = static_cast<float>(frame) / kTickRate;
    s.car = car;
    s.ball = ball;
    return s;
}

// Linear motion of car and ball over `ticks` ticks (optional gravity on the ball).
inline Snapshot advance(const Snapshot& s, int ticks = 1, bool ballGravity = false) {
    Snapshot n = s;
    for (int i = 0; i < ticks; ++i) {
        n.frame += 1;
        n.car.pos += n.car.vel * kTickDt;
        if (ballGravity) n.ball.vel.z -= 650.f * kTickDt;
        n.ball.pos += n.ball.vel * kTickDt;
        if (n.ball.pos.z < kBallZ) {
            n.ball.pos.z = kBallZ;
            if (n.ball.vel.z < 0.f) n.ball.vel.z = -0.6f * n.ball.vel.z;
        }
    }
    n.time = static_cast<float>(n.frame) / kTickRate;
    return n;
}

inline PlayerProfile gamepadProfile() {
    PlayerProfile p;
    p.valid = true;
    p.usingGamepad = true;
    p.metric = true;
    p.gamepadBindings = {{"Jump", "XboxTypeS_A"}, {"Boost", "XboxTypeS_B"}, {"Handbrake", "XboxTypeS_X"}};
    return p;
}

class FakeHost : public ActivityHost {
public:
    bool allow = true;
    Module* owner = nullptr;
    int requests = 0;
    int releases = 0;
    std::string lastName;

    bool requestActivity(Module* m, const std::string& name) override {
        ++requests;
        if (!allow || !m) return false;
        if (owner && owner != m) owner->onActivityStopped();
        owner = m;
        lastName = name;
        return true;
    }
    void releaseActivity(Module* m) override {
        if (owner == m) {
            owner = nullptr;
            ++releases;
        }
    }
    bool ownsActivity(const Module* m) const override { return m && owner == m; }
};

struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    FirstTouchModule mod;
    FakeHost host;
    Snapshot latest;
    bool hasSnap = false;
    Actions all;  // everything emitted since construction

    Harness() {
        FirstTouchModule::describeSettings(schema);
        schema.applyDefaults(settings);
        mod.attach(&host);
        mod.onSettingsChanged(settings);
        game.mode = GameMode::Freeplay;
        game.mapName = "Park_P";
        game.worldControlAllowed = true;
        profile = gamepadProfile();
        mod.onProfileChanged(profile);
        Actions a;
        GameEvent ev;
        ev.kind = GameEventKind::ContextChanged;
        mod.onGameEvent(ev, ctx(), a);
    }

    void set(const std::string& cvar, const std::string& value) {
        CHECK_MSG(schema.apply(settings, cvar, value), cvar);
        mod.onSettingsChanged(settings);
    }

    TickContext ctx() {
        return TickContext{latest, history, settings, game, profile, hasSnap, host.ownsActivity(&mod), game.worldControlAllowed};
    }

    Actions keep(Actions a) {
        Actions copy = a;
        all.merge(std::move(copy));
        return a;
    }

    Actions tick(const Snapshot& s) {
        history.push(s);
        latest = s;
        hasSnap = true;
        Actions a;
        mod.onTick(ctx(), a);
        return keep(std::move(a));
    }

    // Engine order: the touch is resolved at the tick of `t.post`, before onTick of that tick.
    Actions tickWithTouch(const TouchEvent& t) {
        history.push(t.post);
        latest = t.post;
        hasSnap = true;
        Actions a;
        mod.onTouch(t, ctx(), a);
        mod.onTick(ctx(), a);
        return keep(std::move(a));
    }

    Actions event(GameEventKind kind, int64_t frame = 0, const std::string& detail = "") {
        GameEvent ev;
        ev.kind = kind;
        ev.frame = frame;
        ev.detail = detail;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        return keep(std::move(a));
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "firsttouch";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        return keep(std::move(a));
    }

    DrawList draw(float w = 1920.f, float h = 1080.f) {
        ViewContext v;
        v.valid = true;
        v.screenW = w;
        v.screenH = h;
        v.camPos = Vec3{-1000.f, 0.f, 300.f};
        v.ballValid = latest.ball.valid;
        v.ballVisualPos = latest.ball.pos;
        v.carValid = latest.car.valid;
        v.carVisualPos = latest.car.pos;
        DrawContext dc{settings, game, profile, latest, hasSnap, host.ownsActivity(&mod), h / 1080.f};
        DrawList out;
        mod.draw(v, dc, out);
        return out;
    }
};

// One touch scenario: pre-touch states, post-touch velocities, then `after` ticks of linear motion.
struct Scenario {
    CarState car;          // pre (touch tick)
    BallState ball;        // pre
    Vec3 postCarVel;
    Vec3 postBallVel;
    bool newContact = true;
    int ticksSince = 1 << 30;
    bool hasHit = false;
    Vec3 hitLocation, hitNormal;
    int64_t frame = 1000;  // pre frame
    bool ballGravity = false;
    float steerAfter = 0.f;
};

inline TouchEvent buildTouch(const Scenario& sc) {
    TouchEvent t;
    t.frame = sc.frame;
    t.pre = makeSnap(sc.frame, sc.car, sc.ball);
    Snapshot p1 = advance(t.pre, 1, sc.ballGravity);
    p1.car.vel = sc.postCarVel;
    p1.ball.vel = sc.postBallVel;
    t.post1 = p1;
    t.post = advance(p1, 1, sc.ballGravity);
    t.hasHitInfo = sc.hasHit;
    t.hitLocation = sc.hitLocation;
    t.hitNormal = sc.hitNormal;
    t.newContact = sc.newContact;
    t.ticksSincePrevTouch = sc.ticksSince;
    return t;
}

// Feeds 4 approach ticks, the touch, then `after` ticks. Returns everything emitted.
inline Actions runScenario(Harness& h, const Scenario& sc, int after = 130) {
    Actions acc;
    const Snapshot pre = makeSnap(sc.frame, sc.car, sc.ball);
    for (int k = 4; k >= 1; --k) {
        Snapshot s = pre;
        s.frame = sc.frame - k;
        s.time = static_cast<float>(s.frame) / kTickRate;
        s.car.pos -= sc.car.vel * (kTickDt * static_cast<float>(k));
        s.ball.pos -= sc.ball.vel * (kTickDt * static_cast<float>(k));
        acc.merge(h.tick(s));
    }
    acc.merge(h.tick(pre));
    const TouchEvent t = buildTouch(sc);
    acc.merge(h.tick(t.post1));
    acc.merge(h.tickWithTouch(t));
    Snapshot s = t.post;
    for (int i = 0; i < after; ++i) {
        s = advance(s, 1, sc.ballGravity);
        s.in.steer = sc.steerAfter;
        acc.merge(h.tick(s));
    }
    return acc;
}

// Ground pickup: car driving +x at `carSpeed`, ball rolling +x at `ballSpeed`, touching the nose.
// After the touch the ball keeps `ballAfter` and the car `carAfter` (defaults: soft touch).
inline Scenario groundPickup(float carSpeed, float ballSpeed, float carAfter, float ballAfter, int64_t frame = 1000) {
    Scenario sc;
    sc.frame = frame;
    sc.car = groundCar(Vec3{0.f, 0.f, kCarZ}, Vec3{carSpeed, 0.f, 0.f});
    sc.ball = makeBall(Vec3{kNoseContactX, 0.f, kBallZ}, Vec3{ballSpeed, 0.f, 0.f});
    sc.postCarVel = Vec3{carAfter, 0.f, 0.f};
    sc.postBallVel = Vec3{ballAfter, 0.f, 0.f};
    return sc;
}

inline bool contains(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

inline bool hasTip(const std::vector<Tip>& tips, const std::string& code) {
    for (const auto& t : tips)
        if (t.code == code) return true;
    return false;
}

inline bool noEmDash(const std::string& s) { return s.find("\xE2\x80\x94") == std::string::npos; }

} // namespace fttest
