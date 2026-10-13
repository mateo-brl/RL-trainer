#pragma once
// Test helpers of the shooting module: fake activity host, a harness that drives the module like the engine
// does, a tiny world that applies placements, and a scripted flick generator (snapshots + touch events).

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "core/common/History.h"
#include "core/common/Module.h"
#include "core/common/Settings.h"
#include "core/modules/shooting/ShootingModule.h"
#include "tests/TestFramework.h"

namespace shtest {

using namespace rlt;

class FakeHost : public ActivityHost {
public:
    bool allow = true;
    Module* owner = nullptr;
    int requests = 0;
    int releases = 0;
    std::string lastName;

    bool requestActivity(Module* m, const std::string& name) override {
        ++requests;
        lastName = name;
        if (!allow) return false;
        if (owner && owner != m) owner->onActivityStopped();
        owner = m;
        return true;
    }
    void releaseActivity(Module* m) override {
        ++releases;
        if (owner == m) owner = nullptr;
    }
    bool ownsActivity(const Module* m) const override { return owner == m; }
    // Another module takes the activity (or the engine stops it).
    void steal() {
        if (!owner) return;
        Module* old = owner;
        owner = nullptr;
        old->onActivityStopped();
    }
};

inline Rot rotDeg(float pitch, float yaw, float roll) {
    auto u = [](float d) { return static_cast<int>(std::lround(d * 65536.f / 360.f)); };
    return Rot{u(pitch), u(yaw), u(roll)};
}

inline CarState carAt(Vec3 pos, Rot rot = Rot{}, int team = 0) {
    CarState c;
    c.valid = true;
    c.pos = pos;
    c.rot = rot;
    c.basis = basisFromRot(rot);
    c.onGround = true;
    c.wheelContacts = 4;
    c.hasFlip = true;
    c.team = team;
    return c;
}

inline BallState ballAt(Vec3 pos, Vec3 vel = Vec3{}) {
    BallState b;
    b.valid = true;
    b.pos = pos;
    b.vel = vel;
    return b;
}

inline Snapshot snapOf(int64_t frame, const CarState& car, const BallState& ball, const Inputs& in = Inputs{}) {
    Snapshot s;
    s.frame = frame;
    s.time = static_cast<float>(frame) / kTickRate;
    s.car = car;
    s.ball = ball;
    s.in = in;
    return s;
}

inline TouchEvent touchOf(const Snapshot& pre, const Snapshot& post1, const Snapshot& post, bool newContact = true) {
    TouchEvent t;
    t.frame = pre.frame;
    t.pre = pre;
    t.post1 = post1;
    t.post = post;
    t.newContact = newContact;
    return t;
}

inline bool hasTip(const AttemptResult& r, const std::string& code) {
    for (const auto& t : r.tips)
        if (t.code == code) return true;
    return false;
}

inline float metricOf(const AttemptResult& r, const std::string& key, float def = -9999.f) {
    for (const auto& m : r.metrics)
        if (m.key == key) return m.value;
    return def;
}

// Drives the module like the engine (fake host, settings from the real schema, history).
struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    FakeHost host;
    ShootingModule mod;
    Snapshot snap;
    bool has = false;
    std::vector<AttemptResult> results;
    std::vector<ToastMessage> toasts;
    std::vector<ChatMessage> chat;

    Harness() {
        describeGeneralSettings(schema);
        ShootingModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.worldControlAllowed = true;
        mod.attach(&host);
        mod.onSettingsChanged(settings);
        mod.setSeed(1234);
        snap.frame = 1000;
    }
    bool set(const std::string& cvar, const std::string& value) {
        const bool ok = schema.apply(settings, cvar, value);
        mod.onSettingsChanged(settings);
        return ok;
    }
    TickContext ctx() {
        return TickContext{
            snap, history, settings, game, profile, has, host.ownsActivity(&mod), game.worldControlAllowed};
    }
    void collect(const Actions& a) {
        for (const auto& r : a.results) results.push_back(r);
        for (const auto& t : a.toasts) toasts.push_back(t);
        for (const auto& c : a.chat) chat.push_back(c);
    }
    Actions tick(const Snapshot& s) {
        snap = s;
        history.push(s);
        has = true;
        Actions a;
        mod.onTick(ctx(), a);
        collect(a);
        return a;
    }
    Actions touch(const TouchEvent& t) {
        Actions a;
        mod.onTouch(t, ctx(), a);
        collect(a);
        return a;
    }
    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "shooting";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        collect(a);
        return a;
    }
    Actions event(GameEventKind k) {
        GameEvent e;
        e.kind = k;
        e.frame = snap.frame;
        Actions a;
        mod.onGameEvent(e, ctx(), a);
        collect(a);
        return a;
    }
    DrawList draw(const ViewContext& view) {
        DrawContext dc{settings, game, profile, snap, has, host.ownsActivity(&mod), 1.f};
        DrawList out;
        mod.draw(view, dc, out);
        return out;
    }
};

// Minimal world: applies the module placements and moves car and ball (ball optionally riding on the roof).
struct World {
    CarState car;
    BallState ball;
    Inputs in;
    int64_t frame = 2000;
    bool ballRides = false;

    World() {
        car = carAt({0.f, -1000.f, 17.f}, rotDeg(0.f, 90.f, 0.f));
        ball = ballAt({0.f, 0.f, 93.15f});
    }
    void apply(const Actions& a) {
        if (a.setCar) {
            car.pos = a.setCar->pos;
            car.rot = a.setCar->rot;
            car.basis = basisFromRot(car.rot);
            car.vel = a.setCar->vel;
            car.angVel = a.setCar->angVel;
            car.onGround = true;
            car.wheelContacts = 4;
        }
        if (a.setBall) {
            ball.pos = a.setBall->pos;
            ball.vel = a.setBall->vel;
            ball.angVel = a.setBall->angVel;
        }
    }
    Snapshot snap() const { return snapOf(frame, car, ball, in); }
    void advance() {
        ++frame;
        car.pos += car.vel * kTickDt;
        if (ballRides) {
            const shoot::Hitbox hb = shoot::hitboxOf(car);
            ball.pos = shoot::roofSpawn(car.pos, car.basis, hb, ball.radius, 0.f);
            ball.vel = car.vel;
            return;
        }
        ball.vel = ball.vel * (1.f - 0.03f * kTickDt);
        ball.vel.z -= 650.f * kTickDt;
        ball.pos += ball.vel * kTickDt;
        if (ball.pos.z < 93.15f) {
            ball.pos.z = 93.15f;
            ball.vel.z = ball.vel.z < -15.f ? -0.6f * ball.vel.z : 0.f;
        }
    }
};

// Scripted flick: carry on the roof, jump, air phase (rotation), dodge, touch, flight.
struct FlickScript {
    Vec3 start{0.f, -2000.f, 17.f};
    float yawDeg = 90.f; // heading at the jump (90 = +Y, blue attacking)
    int64_t startFrame = 5000;
    float carSpeed = 0.f; // uu/s along the heading
    float carryTime = 0.4f;
    float delay = 0.2f; // jump -> dodge
    float yawTurnDeg = 0.f; // reached at the dodge
    float pitchDeg = 0.f; // reached at the dodge
    float rollDeg = 0.f; // reached at the dodge
    bool airRollInput = false;
    Vec3 dodgeDir{1.f, 0.f, 0.f}; // car-local (x fwd, y right)
    bool engineDodgeDir = true; // false: the engine gives no direction (stick fallback)
    Vec3 launch{1800.f, 0.f, 600.f}; // ball velocity added by the flick, heading frame (x heading, y right, z up)
    bool contact = true;
    bool deliverTouch = true;
    bool loseBallBeforeDodge = false;
    float offX = 10.f, offY = 0.f; // ball offset on the roof during the carry
    int team = 0;
    int tail = 50;
};

struct ScriptRun {
    std::vector<Snapshot> snaps;
    std::vector<std::pair<size_t, TouchEvent>> touches; // delivered just before snaps[first]
    size_t jumpIndex = 0, dodgeIndex = 0;
};

inline ScriptRun buildFlick(const FlickScript& f) {
    ScriptRun run;
    const shoot::Hitbox hb;
    const int carry = static_cast<int>(std::lround(f.carryTime * kTickRate));
    const int delay = std::max(1, static_cast<int>(std::lround(f.delay * kTickRate)));
    const int J = carry, D = carry + delay;
    const int total = D + f.tail;
    run.jumpIndex = static_cast<size_t>(J);
    run.dodgeIndex = static_cast<size_t>(D);
    const Basis b0 = basisFromRot(rotDeg(0.f, f.yawDeg, 0.f));
    const Vec3 launchWorld = b0.fwd * f.launch.x + b0.right * f.launch.y + Vec3{0.f, 0.f, f.launch.z};
    Vec3 pos = f.start;
    Vec3 vel = b0.fwd * f.carSpeed;
    float vz = 0.f;
    bool released = false, lostDrift = false;
    Vec3 ballPos, ballVel;
    for (int i = 0; i < total; ++i) {
        Snapshot s;
        s.frame = f.startFrame + i;
        s.time = static_cast<float>(s.frame) / kTickRate;
        CarState& c = s.car;
        c.valid = true;
        c.team = f.team;
        c.hitboxExtent = hb.extent;
        c.hitboxOffset = hb.offset;
        const bool air = i >= J;
        const float frac = air ? std::min(1.f, static_cast<float>(i - J) / static_cast<float>(delay)) : 0.f;
        c.rot = rotDeg(f.pitchDeg * frac, f.yawDeg + f.yawTurnDeg * frac, f.rollDeg * frac);
        c.basis = basisFromRot(c.rot);
        if (i == J) vz = 300.f;
        if (air && i > J) vz -= 650.f * kTickDt;
        c.pos = pos;
        c.vel = vel + Vec3{0.f, 0.f, vz};
        if (air && i <= D && f.rollDeg != 0.f) c.angVel = c.basis.fwd * (f.rollDeg * kDegToRad / f.delay);
        c.onGround = !air;
        c.wheelContacts = air ? 0 : 4;
        c.jumped = air;
        c.hasFlip = i < D;
        c.dodging = i >= D;
        if (i >= D && f.engineDodgeDir) c.dodgeDir = f.dodgeDir;
        Inputs& in = s.in;
        in.jump = (i >= J && i < J + 3) || (i >= D && i < D + 3);
        if (air && i < D && f.airRollInput) in.roll = 1.f;
        if (i >= D && i < D + 3) {
            in.pitch = -f.dodgeDir.x;
            in.yaw = f.dodgeDir.y;
        }
        // Ball
        if (!released) {
            ballPos = shoot::roofSpawn(c.pos, c.basis, hb, 91.25f, 0.f) + c.basis.fwd * f.offX + c.basis.right * f.offY;
            ballVel = c.vel;
            if (f.loseBallBeforeDodge && i >= J + 3) {
                lostDrift = true;
                released = true;
                ballVel = c.vel + Vec3{1500.f, 0.f, 0.f};
            }
        }
        if (!released && f.contact && i == D + 3) {
            released = true;
            ballVel = c.vel + launchWorld;
        }
        if (!released && !f.contact && i == D + 1) {
            released = true;
            vel += c.basis.fwd * 500.f; // the car flips away from the ball
        }
        if (released && (i > D + 3 || !f.contact || lostDrift)) {
            ballVel.z -= 650.f * kTickDt;
            ballPos += ballVel * kTickDt;
        }
        s.ball.valid = true;
        s.ball.pos = ballPos;
        s.ball.vel = ballVel;
        run.snaps.push_back(s);
        pos += c.vel * kTickDt;
        if (pos.z < 17.f) pos.z = 17.f;
    }
    if (f.contact && f.deliverTouch && D + 4 < total) {
        const size_t d = static_cast<size_t>(D);
        run.touches.push_back({d + 4, touchOf(run.snaps[d + 2], run.snaps[d + 3], run.snaps[d + 4], false)});
    }
    return run;
}

// Feeds a scripted flick to an analyzer; returns the reports.
inline std::vector<shoot::FlickReport> runAnalyzer(shoot::FlickAnalyzer& a, const ScriptRun& run) {
    std::vector<shoot::FlickReport> out;
    for (size_t i = 0; i < run.snaps.size(); ++i) {
        for (const auto& t : run.touches)
            if (t.first == i) a.onTouch(t.second);
        if (auto r = a.update(run.snaps[i])) out.push_back(*r);
    }
    return out;
}

// Ball flight by an independent explicit integrator (drag + gravity, no bounce), for the shot tests.
inline std::vector<BallState> ballistic(Vec3 p, Vec3 v, int ticks) {
    std::vector<BallState> out;
    for (int i = 0; i < ticks; ++i) {
        out.push_back(ballAt(p, v));
        v = v * (1.f - 0.03f * kTickDt);
        v.z -= 650.f * kTickDt;
        p += v * kTickDt;
    }
    return out;
}

} // namespace shtest
