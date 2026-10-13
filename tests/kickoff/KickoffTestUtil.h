#pragma once
// Test helpers for the kickoff module:
//  - FakeHost: ActivityHost that grants/denies the activity like the engine;
//  - Harness: feeds Snapshots with the engine's ordering (history push, touches resolved two ticks later and
//    dispatched before onTick, world control filtered for non-owners);
//  - simulate(): 1D kickoff simulator along the kickoff line (port of the research reference, constants from
//    RocketSim: throttle 1600 -> 160 uu/s^2 at 0 -> 1400 uu/s, boost 991.7 ground / 1058.3 air, 33.3 boost/s).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/kickoff/KickoffModule.h"

namespace kot {

using namespace rlt;

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
    // Another module takes the activity (or the engine stops it).
    void steal() {
        Module* old = owner;
        owner = nullptr;
        if (old) old->onActivityStopped();
    }
};

inline Snapshot makeSnap(int64_t frame) {
    Snapshot s;
    s.frame = frame;
    s.time = static_cast<float>(frame) * kTickDt;
    s.car.valid = true;
    s.car.onGround = true;
    s.car.wheelContacts = 4;
    s.car.boost = kKoSpawnBoost;
    s.ball.valid = true;
    s.ball.pos = {0.f, 0.f, kKoBallRestZ};
    return s;
}

// Car resting on a kickoff spawn (as the placement would leave it), ball at rest on the centre spot.
inline Snapshot restingSnap(int64_t frame, KickoffSpawn spawn, KickoffTeam team, bool preHold = true) {
    Snapshot s = makeSnap(frame);
    const KickoffPose pose = kickoffPose(spawn, team, kKoCarRestZ);
    s.car.pos = pose.pos;
    s.car.rot = pose.rot;
    s.car.basis = basisFromRot(pose.rot);
    s.car.team = static_cast<int>(team);
    if (preHold) {
        s.in.throttle = 1.f;
        s.in.boost = true;
    }
    return s;
}

struct Harness {
    KickoffModule mod;
    FakeHost host;
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{600};
    std::vector<int64_t> pendingTouches;
    bool hasSnapshot = false;
    Snapshot latest;
    std::vector<Actions> log;  // filtered actions of every tick (index = call order)

    Harness() {
        describeGeneralSettings(schema);
        KickoffModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.mapName = "Stadium_P";
        game.worldControlAllowed = true;
        mod.attach(&host);
        mod.onSettingsChanged(settings);
    }

    void set(const std::string& cvar, const std::string& value) {
        schema.apply(settings, cvar, value);
        mod.onSettingsChanged(settings);
    }

    TickContext ctx() const {
        return TickContext{
            latest, history, settings, game, profile, hasSnapshot, host.ownsActivity(&mod), game.worldControlAllowed};
    }

    void filter(Actions& a) const {
        if (!game.worldControlAllowed || !host.ownsActivity(&mod)) a.clearWorldControl();
    }

    // One physics tick. `touchThisStep`: the local car touches the ball during the physics step after `sn`.
    Actions tick(const Snapshot& sn, bool touchThisStep = false) {
        history.push(sn);
        latest = sn;
        hasSnapshot = true;
        Actions all;
        // Touches resolved now (pre, post1, post2 available), before onTick, like Engine::resolveTouches.
        std::vector<int64_t> still;
        for (int64_t f : pendingTouches) {
            const Snapshot* pre = history.atOrBefore(f);
            const Snapshot* post1 = history.after(f);
            const Snapshot* post2 = post1 ? history.after(post1->frame) : nullptr;
            if (!post2) {
                still.push_back(f);
                continue;
            }
            TouchEvent t;
            t.frame = f;
            t.pre = *pre;
            t.post1 = *post1;
            t.post = *post2;
            Actions a;
            mod.onTouch(t, ctx(), a);
            filter(a);
            all.merge(std::move(a));
        }
        pendingTouches.swap(still);
        Actions a;
        mod.onTick(ctx(), a);
        filter(a);
        all.merge(std::move(a));
        if (touchThisStep) pendingTouches.push_back(sn.frame);
        log.push_back(all);
        return all;
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "kickoff";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        filter(a);
        return a;
    }

    Actions event(GameEventKind kind) {
        GameEvent ev;
        ev.kind = kind;
        ev.frame = latest.frame;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        filter(a);
        return a;
    }

    // Ticks resting snapshots (pre-held inputs) until the next frame would be the GO frame. Returns that frame.
    int64_t runCountdown(int64_t& frame, bool preHold = true) {
        for (int guard = 0; guard < 2000; ++guard) {
            const KickoffPhase ph = mod.phase();
            if (ph == KickoffPhase::Countdown && frame + 1 >= mod.goFrame()) break;
            if (ph != KickoffPhase::Placing && ph != KickoffPhase::Countdown) break;
            tick(restingSnap(++frame, mod.currentSpawn(), mod.currentTeam(), preHold));
        }
        return frame + 1;
    }
};

// ---- Synthetic kickoff simulator --------------------------------------------------------------------------------
struct Script {
    enum Kind { BoostOnly, Speedflip, FrontFlip, DoubleFlip, Wavedash, Fake, LateSpeedflip } kind = Speedflip;
    float jumpAt = 0.45f;        // first jump (s after GO)
    int dodgeGapTicks = 13;      // first jump press -> dodge press
    int cancelTicks = 3;         // dodge -> stick back
    float sideDodge = -0.55f;    // roll during the dodge (diagonal speedflip)
    float reaction = 0.f;        // s before pressing anything
    float lateral = 0.f;         // constant lateral offset of the line (uu)
    float contactLateral = 5.f;  // ball centre offset in the car frame at contact
    float ballExitScale = 1.4f;  // ball speed after the touch relative to the car speed
};

struct SimOut {
    std::vector<Snapshot> snaps;
    int touchIndex = -1;  // the touch happens during the physics step after snaps[touchIndex]
};

inline float throttleAcc(float v) {
    v = std::fabs(v);
    if (v < 1400.f) return 1600.f * (1.f - 0.9f * v / 1400.f);
    if (v < 1410.f) return 160.f * (1410.f - v) / 10.f;
    return 0.f;
}

inline int ticksOf(float s) { return static_cast<int>(std::lround(s * kTickRate)); }

inline SimOut simulate(KickoffSpawn spawn, KickoffTeam team, const Script& sc, int64_t goFrame, float maxT = 4.f) {
    SimOut out;
    const KickoffPath path = kickoffPath(spawn, team);
    const std::vector<KickoffPadOnPath> pads = kickoffPadsOnPath(path);
    const Rot rot = rotFromDirection(path.dir);
    const Basis basis = basisFromRot(rot);
    size_t nextPad = 0;
    float s = 0.f, v = 0.f, boost = kKoSpawnBoost, z = kKoCarRestZ, vz = 0.f;
    bool onGround = true, touched = false;
    const int jumpTick = ticksOf(sc.jumpAt);
    const int dodgeTick = jumpTick + sc.dodgeGapTicks;
    float airUntil = -1.f;
    int secondJumpTick = -1, secondDodgeTick = -1;
    Vec3 ballVel;
    Vec3 ballPos{0.f, 0.f, kKoBallRestZ};
    const bool sf = sc.kind == Script::Speedflip || sc.kind == Script::LateSpeedflip;
    const bool flips =
        sf || sc.kind == Script::FrontFlip || sc.kind == Script::DoubleFlip || sc.kind == Script::Wavedash;
    for (int i = 0; i <= ticksOf(maxT); ++i) {
        const float t = static_cast<float>(i) * kTickDt;
        Inputs in;
        const bool active = t >= sc.reaction && sc.kind != Script::Fake;
        if (sc.kind == Script::Fake) in.throttle = (t > 0.2f && t < 0.6f) ? 0.4f : 0.f;
        if (active) {
            in.throttle = 1.f;
            in.boost = true;
        }
        auto press = [&](int tick, int dur) { return i >= tick && i < tick + dur; };
        if (flips && active) {
            if (press(jumpTick, std::min(8, sc.dodgeGapTicks - 2))) in.jump = true;
            if (press(dodgeTick, 10)) {
                in.jump = true;
                in.pitch = -1.f;
                if (sf) in.roll = sc.sideDodge;
            }
            if (sf && i >= dodgeTick + sc.cancelTicks && i < dodgeTick + 60) in.pitch = 1.f;
            if (sc.kind == Script::FrontFlip || sc.kind == Script::DoubleFlip)
                in.boost = in.boost && !(i >= dodgeTick && i < dodgeTick + 70);
            if (sc.kind == Script::DoubleFlip && secondJumpTick >= 0) {
                if (press(secondJumpTick, 6)) in.jump = true;
                if (press(secondDodgeTick, 8)) {
                    in.jump = true;
                    in.pitch = -1.f;
                }
            }
        }
        // State BEFORE this tick's physics (what the SetVehicleInput pre-hook snapshot sees).
        Snapshot sn = makeSnap(goFrame + i);
        Vec3 pos = path.pointAt(s) + path.right * sc.lateral;
        pos.z = z;
        sn.car.pos = pos;
        sn.car.vel = path.dir * v + Vec3{0.f, 0.f, vz};
        sn.car.rot = rot;
        sn.car.basis = basis;
        sn.car.onGround = onGround;
        sn.car.wheelContacts = onGround ? 4 : 0;
        sn.car.boost = boost;
        sn.car.team = static_cast<int>(team);
        sn.in = in;
        sn.ball.pos = ballPos;
        sn.ball.vel = ballVel;
        if (!touched && s >= path.length && sc.kind != Script::Fake) {
            touched = true;
            out.touchIndex = i;
            // Car centre line `contactLateral` uu left of the ball centre: ball is to the car's right by that much.
            sn.car.pos = sn.car.pos - path.right * sc.contactLateral;
            ballVel = path.dir * (sc.ballExitScale * v);
        }
        out.snaps.push_back(sn);
        // Physics of this tick.
        if (touched) {
            ballPos = ballPos + ballVel * kTickDt;
            v *= 0.98f;
        }
        const bool boosting = in.boost && boost > 0.f;
        if (onGround) {
            const float a =
                (in.throttle > 0.f || boosting ? throttleAcc(v) * std::max(in.throttle, boosting ? 1.f : 0.f) : 0.f) +
                (boosting ? 2975.f / 3.f : 0.f);
            v += a * kTickDt;
        } else if (boosting) {
            v += (3175.f / 3.f) * kTickDt;
        }
        if (boosting) boost = std::max(0.f, boost - kKoBoostPerSecond * kTickDt);
        v = std::min(v, 2300.f);
        s += v * kTickDt;
        while (nextPad < pads.size() && s >= pads[nextPad].along) {
            boost = std::min(100.f, boost + pads[nextPad].amount);
            ++nextPad;
        }
        if (flips && active) {
            if (i == jumpTick) {
                onGround = false;
                vz = 300.f;
                airUntil = t + 9.f;
            }
            if (i == dodgeTick) {
                if (sc.kind == Script::Wavedash) {
                    v += 500.f;
                    airUntil = t + 0.05f;
                } else {
                    v += sf ? 520.f : 500.f;
                    airUntil = t + (sf ? 0.80f : 0.95f);
                }
            }
            if (!onGround) {
                z = std::max(kKoCarRestZ, z + vz * kTickDt);
                vz -= 650.f * kTickDt;
            }
            if (!onGround && t >= airUntil) {
                onGround = true;
                z = kKoCarRestZ;
                vz = 0.f;
                if (sc.kind == Script::DoubleFlip && secondJumpTick < 0) {
                    const float remain = (path.length - s) / std::max(v, 1.f);
                    const int at = i + std::max(2, ticksOf(remain - 0.40f));
                    secondJumpTick = at;
                    secondDodgeTick = at + 10;
                }
            }
            if (sc.kind == Script::DoubleFlip && i == secondJumpTick) {
                onGround = false;
                vz = 250.f;
                airUntil = t + 0.6f;
            }
            if (sc.kind == Script::DoubleFlip && i == secondDodgeTick) v += 500.f;
        }
        if (touched && i - out.touchIndex > 200) break;
    }
    return out;
}

// Runs the recorder alone over a simulated attempt (touch delivered with pre = touch snapshot, post = 2 ticks later).
inline KickoffMetrics runRecorder(KickoffSpawn sp, KickoffTeam tm, const Script& sc) {
    KickoffRecorder rec;
    const int64_t go = 1000;
    rec.begin(sp, tm, go);
    const SimOut sim = simulate(sp, tm, sc, go);
    for (size_t i = 0; i < sim.snaps.size(); ++i) {
        if (sim.touchIndex >= 0 && static_cast<int>(i) == sim.touchIndex) {
            const size_t post = std::min(sim.snaps.size() - 1, i + 2);
            rec.touch(sim.snaps[i], sim.snaps[post]);
            break;
        }
        rec.feed(sim.snaps[i]);
    }
    return rec.finish();
}

// Feeds a simulated attempt to the harness from the GO frame until the next rep starts (or the stream ends).
inline void playAttempt(Harness& h, const SimOut& sim, int64_t& frame, std::vector<Actions>* acts = nullptr) {
    for (size_t i = 0; i < sim.snaps.size(); ++i) {
        const Snapshot& sn = sim.snaps[i];
        frame = sn.frame;
        Actions a = h.tick(sn, static_cast<int>(i) == sim.touchIndex);
        if (acts) acts->push_back(a);
        const KickoffPhase ph = h.mod.phase();
        if (ph == KickoffPhase::Idle || ph == KickoffPhase::Placing || ph == KickoffPhase::Countdown) break;
    }
}

inline bool hasEmDash(const std::string& s) { return s.find("\xE2\x80\x94") != std::string::npos; }

} // namespace kot
