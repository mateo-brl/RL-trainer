#pragma once
// Synthetic 120 Hz tick sequences for the ball mechanics tests (port of the research reference Sim).
//
// Touch semantics follow the engine: a touch is marked on the tick sampled BEFORE the hit (TouchEvent::pre) and the
// new ball velocity is visible from the next tick on. `hit(...)` does exactly that.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "core/common/Context.h"
#include "core/common/History.h"
#include "core/common/Module.h"
#include "core/common/Settings.h"
#include "core/modules/ballmech/BallMechModule.h"
#include "core/modules/ballmech/Detectors.h"
#include "core/modules/ballmech/Primitives.h"

namespace bmtest {

using namespace rlt;

constexpr float DT = kTickDt;
constexpr float PI = kPi;
constexpr float G = bm::arena::kGravityZ;

struct SimTick {
    Snapshot s;
    bool touched = false;
    bool newContact = true;
    bool sdkReset = false;
};

// Unreal FRotationMatrix basis from yaw / pitch / roll in radians (same formula as basisFromRot).
inline void orient(CarState& c, float yaw, float pitch, float roll) {
    const float CY = std::cos(yaw), SY = std::sin(yaw), CP = std::cos(pitch), SP = std::sin(pitch);
    const float CR = std::cos(roll), SR = std::sin(roll);
    c.basis.fwd = {CP * CY, CP * SY, SP};
    c.basis.right = {SR * SP * CY - CR * SY, SR * SP * SY + CR * CY, -SR * CP};
    c.basis.up = {-(CR * SP * CY + SR * SY), CY * SR - CR * SP * SY, CR * CP};
    c.rot = {static_cast<int>(std::lround(pitch * kRadToUnr)), static_cast<int>(std::lround(yaw * kRadToUnr)),
             static_cast<int>(std::lround(roll * kRadToUnr))};
}
inline void setAir(CarState& c) {
    c.onGround = false;
    c.wheelContacts = 0;
}
inline void setGround(CarState& c) {
    c.onGround = true;
    c.wheelContacts = 4;
}

struct Sim {
    Snapshot s;
    std::vector<SimTick> out;
    bool carGravity = false, ballGravity = false;
    bool attached = false;
    Vec3 attachOffset;
    int dodgeTicksLeft = 0;
    bool touchNow = false, sdkNow = false;
    bool pendingVel = false;
    Vec3 nextBallVel;
    int64_t lastTouchFrame = -1000;

    Sim() {
        s.frame = 1000;
        s.time = static_cast<float>(s.frame) / kTickRate;
        s.car.valid = true;
        s.ball.valid = true;
        s.car.hasFlip = true;
        s.car.boost = 33.f;
        s.car.pos = {0.f, 0.f, bm::arena::kCarRestZ};
        orient(s.car, 0.f, 0.f, 0.f);
        setGround(s.car);
    }

    void step(int n, const std::function<void(Snapshot&, int)>& f = nullptr) {
        for (int i = 0; i < n; ++i) {
            s.frame += 1;
            s.time = static_cast<float>(s.frame) / kTickRate;
            if (dodgeTicksLeft > 0 && --dodgeTicksLeft == 0) s.car.dodging = false;
            if (pendingVel) {
                s.ball.vel = nextBallVel;
                pendingVel = false;
            }
            if (carGravity) s.car.vel.z += G * DT;
            s.car.pos += s.car.vel * DT;
            if (attached) {
                s.ball.pos = s.car.pos + attachOffset;
                s.ball.vel = s.car.vel;
            } else {
                if (ballGravity) s.ball.vel.z += G * DT;
                s.ball.pos += s.ball.vel * DT;
            }
            if (f) f(s, i);
            SimTick t;
            t.s = s;
            t.touched = touchNow;
            t.sdkReset = sdkNow;
            if (touchNow) {
                t.newContact = s.frame - lastTouchFrame >= 6;
                lastTouchFrame = s.frame;
            }
            out.push_back(t);
            touchNow = false;
            sdkNow = false;
        }
    }
    // Touch on the next tick (sampled before the hit); the ball has `vel` from the tick after.
    void hit(const Vec3& vel, const std::function<void(Snapshot&)>& f = nullptr) {
        step(1, [&](Snapshot& x, int) {
            if (f) f(x);
            touchNow = true;
        });
        pendingVel = true;
        nextBallVel = vel;
    }
    // Airborne dodge: jump edge + stick, `dodging` for 0.65 s (FLIP_TORQUE_TIME).
    void dodge(float fwd, float side) {
        step(1, [&](Snapshot& x, int) { x.in.jump = false; });
        step(1, [&](Snapshot& x, int) {
            x.in.jump = true;
            x.in.dodgeForward = fwd;
            x.in.dodgeStrafe = side;
            x.in.pitch = -fwd;
            x.in.yaw = side;
            x.car.dodging = true;
            x.car.hasFlip = false;
            x.car.dodgeDir = {fwd, side, 0.f};
        });
        dodgeTicksLeft = 78;
        step(1, [&](Snapshot& x, int) {
            x.in.jump = false;
            x.in.dodgeForward = x.in.dodgeStrafe = x.in.pitch = x.in.yaw = 0.f;
        });
    }
};

// ---- Frames and detector runs ---------------------------------------------------------------------------------
inline std::vector<bm::Frame> buildFrames(const std::vector<SimTick>& ticks, bool standardArena = true) {
    std::vector<bm::Frame> frames;
    frames.reserve(ticks.size());
    bm::FrameBuilder b;
    bm::BuildOptions o;
    o.standardArena = standardArena;
    for (size_t i = 0; i < ticks.size(); ++i) {
        const Snapshot* prev = i > 0 ? &ticks[i - 1].s : nullptr;
        const Snapshot* n1 = i + 1 < ticks.size() ? &ticks[i + 1].s : nullptr;
        const Snapshot* n2 = i + 2 < ticks.size() ? &ticks[i + 2].s : nullptr;
        bm::TouchMark m;
        m.frame = ticks[i].s.frame;
        m.newContact = ticks[i].newContact;
        frames.push_back(b.build(ticks[i].s, prev, n1, n2, ticks[i].touched ? &m : nullptr, ticks[i].sdkReset, false, o));
    }
    return frames;
}

inline bm::DetectParams refParams() {
    bm::DetectParams p;
    p.pinchSpeed = 4000.f;
    p.aerialMinSpeed = 1200.f;
    p.powerMinSpeed = 1200.f;  // research reference threshold
    return p;
}

inline std::vector<bm::MechGrade> run(bm::Detector& d, const std::vector<SimTick>& ticks,
                                      const bm::DetectParams& p = refParams(), bool standardArena = true) {
    d.reset();
    std::vector<bm::MechGrade> gs;
    const auto frames = buildFrames(ticks, standardArena);
    for (size_t i = 1; i < frames.size(); ++i)
        if (auto g = d.update(frames[i - 1], frames[i], p)) gs.push_back(*g);
    return gs;
}

// ---- Module harness --------------------------------------------------------------------------------------------
class FakeHost : public ActivityHost {
public:
    bool allow = true;
    Module* owner = nullptr;
    int requests = 0, releases = 0;
    std::string lastName;
    bool requestActivity(Module* m, const std::string& name) override {
        ++requests;
        if (!allow) return false;
        if (owner && owner != m) {
            Module* old = owner;
            owner = nullptr;
            old->onActivityStopped();
        }
        owner = m;
        lastName = name;
        return true;
    }
    void releaseActivity(Module* m) override {
        ++releases;
        if (owner == m) owner = nullptr;
    }
    bool ownsActivity(const Module* m) const override { return owner == m; }
};

// Stand-in for another module taking the activity.
class OtherModule : public Module {
public:
    const char* id() const override { return "other"; }
    const char* title() const override { return "Autre"; }
    int stopped = 0;
    void onActivityStopped() override { ++stopped; }
    bool take() { return requestActivity("autre exercice"); }
};

struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    FakeHost host;
    BallMechModule mod;
    Snapshot latest;
    bool hasSnap = false;
    std::vector<AttemptResult> results;
    std::vector<GameEvent> events;
    std::vector<ChatMessage> chat;
    std::vector<ToastMessage> toasts;
    std::vector<Actions> worldActions;  // world-control part of every tick (after the engine filter)
    Actions last;
    struct PendingTouch {
        int64_t frame;
        bool newContact;
    };
    std::vector<PendingTouch> pending;

    Harness() {
        describeGeneralSettings(schema);
        BallMechModule::describeSettings(schema);
        schema.applyDefaults(settings);
        mod.onSettingsChanged(settings);
        mod.attach(&host);
        game.mode = GameMode::Freeplay;
        game.worldControlAllowed = true;
    }

    TickContext ctx() const {
        return TickContext{latest, history, settings, game, profile, hasSnap, host.ownsActivity(&mod),
                           game.worldControlAllowed};
    }

    void absorb(Actions& a) {
        if (!game.worldControlAllowed || !host.ownsActivity(&mod)) a.clearWorldControl();
        for (auto& r : a.results) results.push_back(r);
        for (auto& e : a.events) events.push_back(e);
        for (auto& c : a.chat) chat.push_back(c);
        for (auto& t : a.toasts) toasts.push_back(t);
        last = a;
    }

    void set(const std::string& cvar, const std::string& value) {
        schema.apply(settings, cvar, value);
        mod.onSettingsChanged(settings);
    }

    void tick(const SimTick& t) {
        latest = t.s;
        history.push(t.s);
        hasSnap = true;
        if (t.touched) pending.push_back({t.s.frame, t.newContact});
        // The engine resolves a touch when two newer snapshots exist (TouchEvent pre / post1 / post).
        if (history.size() >= 3) {
            const Snapshot& pre = history.back(2);
            for (auto it = pending.begin(); it != pending.end();) {
                if (it->frame == pre.frame) {
                    TouchEvent te;
                    te.frame = pre.frame;
                    te.pre = pre;
                    te.post1 = history.back(1);
                    te.post = history.back(0);
                    te.newContact = it->newContact;
                    Actions a;
                    mod.onTouch(te, ctx(), a);
                    absorb(a);
                    it = pending.erase(it);
                } else if (it->frame < pre.frame) {
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (t.sdkReset) event(GameEventKind::FlipReset, "sdk", t.s.frame);
        Actions a;
        mod.onTick(ctx(), a);
        absorb(a);
        worldActions.push_back(last);
    }

    void run(const std::vector<SimTick>& ticks) {
        for (const auto& t : ticks) tick(t);
    }

    // Keeps the last state running for `n` more ticks (lets delayed processing and held grades flush).
    void idle(int n) {
        SimTick t;
        t.s = latest;
        for (int i = 0; i < n; ++i) {
            t.s.frame += 1;
            t.s.time = static_cast<float>(t.s.frame) / kTickRate;
            t.s.car.pos += t.s.car.vel * DT;
            t.s.ball.pos += t.s.ball.vel * DT;
            t.s.in = Inputs{};
            tick(t);
        }
    }

    void event(GameEventKind kind, const std::string& detail = "", int64_t frame = -1) {
        GameEvent ev;
        ev.kind = kind;
        ev.detail = detail;
        ev.frame = frame >= 0 ? frame : latest.frame;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        absorb(a);
    }

    void command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "ballmech";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        absorb(a);
    }

    int countSkill(const std::string& skill) const {
        return static_cast<int>(std::count_if(results.begin(), results.end(),
                                              [&](const AttemptResult& r) { return r.skill == skill; }));
    }
    int countEvents(const std::string& detail) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const GameEvent& e) {
            return e.kind == GameEventKind::FlipReset && e.detail == detail;
        }));
    }
};

inline bool hasMetric(const AttemptResult& r, const std::string& key) {
    return std::any_of(r.metrics.begin(), r.metrics.end(), [&](const Metric& m) { return m.key == key; });
}
inline float metric(const AttemptResult& r, const std::string& key, float def = -1e9f) {
    for (const auto& m : r.metrics)
        if (m.key == key) return m.value;
    return def;
}
inline bool hasTip(const AttemptResult& r, const std::string& code) {
    return std::any_of(r.tips.begin(), r.tips.end(), [&](const Tip& t) { return t.code == code; });
}

// =============================================================================================================
// Scenario builders (ported from the research reference, adapted: no teleports where the module runs them)
// =============================================================================================================
enum class FR { Good, Wasted, NoReset, FastMissed };
std::vector<SimTick> flipResetScenario(FR kind, bool sdk = true);
std::vector<SimTick> flipResetChain();
std::vector<SimTick> ceilingScenario(bool good);
enum class FK { F45, F90, Musty, Breezi, FrontDelayed, Reverse, LostBall, OverRotated45, MustyNoseLevel };
std::vector<SimTick> flickScenario(FK kind);
enum class DTK { Good, Landed, LateNoSecond };
std::vector<SimTick> doubleTapScenario(DTK kind);
enum class AD { GroundCarry, WallBump, Short };
std::vector<SimTick> airDribbleScenario(AD kind);
enum class AE { AirRoll, Redirect, OffCenter };
std::vector<SimTick> aerialScenario(AE kind, int team = 0);
enum class PI_ { Ground, Kuxir, Weak };
std::vector<SimTick> pinchScenario(PI_ kind);
enum class PS { FlipShot, SlideShot, WeakSide };
std::vector<SimTick> powerShotScenario(PS kind, int team = 0);

} // namespace bmtest
