#pragma once
// Shared helpers of the courses tests: a fake activity host, a harness that drives the module like the engine
// (settings from the schema, snapshots, placements applied as the game would) and synthetic paths.

#include <cmath>
#include <string>
#include <vector>

#include "core/common/Settings.h"
#include "core/modules/courses/CoursesModule.h"

namespace ct {

using namespace rlt;

class FakeHost : public ActivityHost {
public:
    bool allow = true;
    Module* owner = nullptr;
    std::string name;
    int requests = 0, releases = 0;

    bool requestActivity(Module* m, const std::string& n) override {
        ++requests;
        if (!allow) return false;
        if (owner && owner != m) owner->onActivityStopped();
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
};

struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{16};
    Snapshot snap;
    FakeHost host;
    CoursesModule mod;
    int64_t frame = 10000;
    std::vector<Actions> log;  // every Actions produced by tick() (bounded by the caller)
    bool keepLog = false;

    Harness() {
        describeGeneralSettings(schema);
        CoursesModule::describeSettings(schema);
        schema.applyDefaults(settings);
        mod.onSettingsChanged(settings);
        mod.attach(&host);
        game.mode = GameMode::Freeplay;
        game.mapName = "Park_P";
        game.worldControlAllowed = true;
        game.localDate = "2026-10-13";
        snap.frame = frame;
        snap.time = static_cast<float>(frame) / 120.f;
        snap.car.valid = true;
        snap.car.pos = {0.f, 0.f, 17.f};
        snap.car.onGround = true;
        snap.car.wheelContacts = 4;
        snap.car.boost = 100.f;
        snap.ball.valid = true;
        snap.ball.pos = {0.f, 0.f, 93.15f};
    }

    bool set(const std::string& cvar, const std::string& value) {
        const bool ok = schema.apply(settings, cvar, value);
        mod.onSettingsChanged(settings);
        return ok;
    }

    TickContext ctx() {
        return TickContext{snap, history, settings, game, profile, true, host.ownsActivity(&mod), game.worldControlAllowed};
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "courses";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        return a;
    }

    Actions event(GameEventKind kind, const std::string& detail = "") {
        GameEvent ev;
        ev.kind = kind;
        ev.frame = frame;
        ev.detail = detail;
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        return a;
    }

    // Applies world-control actions as the game would (only when the module owns the activity).
    void apply(const Actions& a) {
        if (!host.ownsActivity(&mod)) return;
        if (a.setCar) {
            snap.car.pos = a.setCar->pos;
            snap.car.vel = a.setCar->vel;
            snap.car.rot = a.setCar->rot;
            snap.car.basis = basisFromRot(a.setCar->rot);
            snap.car.angVel = a.setCar->angVel;
            if (a.setCar->boost) snap.car.boost = *a.setCar->boost;
        }
        if (a.setBall) {
            snap.ball.pos = a.setBall->pos;
            snap.ball.vel = a.setBall->vel;
        }
        if (a.setBoost) snap.car.boost = *a.setBoost;
    }

    // One physics tick with the current snapshot (frame advanced first).
    Actions tick(bool applyActions = true) {
        ++frame;
        snap.frame = frame;
        snap.time = static_cast<float>(frame) / 120.f;
        history.push(snap);
        Actions a;
        mod.onTick(ctx(), a);
        if (applyActions) apply(a);
        if (keepLog) log.push_back(a);
        return a;
    }

    // Ticks through Setup and Countdown until the run starts. Returns the number of ticks used.
    int runCountdown(int maxTicks = 2000) {
        int n = 0;
        while (mod.phase() != CoursesPhase::Running && n < maxTicks) {
            tick();
            ++n;
            // Physics of the release tick: the car leaves the start with the placement velocity.
        }
        return n;
    }

    // Puts the car hitbox centre at p (current basis), moving with velocity v.
    void carAt(const Vec3& p, const Vec3& v) {
        snap.car.pos = p - snap.car.basis.toWorld(snap.car.hitboxOffset);
        snap.car.vel = v;
    }
    Vec3 carCentre() const { return hitboxCenter(snap.car); }

    // Moves the object (car hitbox centre, or ball when `ball`) along a polyline at constant speed,
    // one tick per step, until the module leaves the Running phase or the path ends.
    // Returns the number of ticks.
    int followPath(const std::vector<Vec3>& pts, float speedUus, bool ball, int maxTicks = 20000) {
        if (pts.size() < 2) return 0;
        const float step = speedUus / 120.f;
        size_t seg = 0;
        Vec3 pos = pts[0];
        int ticks = 0;
        while (ticks < maxTicks && mod.phase() == CoursesPhase::Running) {
            float left = step;
            Vec3 dir{0.f, 0.f, 0.f};
            while (left > 0.f && seg + 1 < pts.size()) {
                const Vec3 to = pts[seg + 1] - pos;
                const float d = length(to);
                if (d < 1e-4f) {
                    ++seg;
                    continue;
                }
                dir = to / d;
                if (d <= left) {
                    pos = pts[seg + 1];
                    left -= d;
                    ++seg;
                } else {
                    pos = pos + dir * left;
                    left = 0.f;
                }
            }
            if (seg + 1 >= pts.size() && left > 0.f) break;
            if (ball) {
                snap.ball.pos = pos;
                snap.ball.vel = dir * speedUus;
                // The car follows under the ball (kept valid, far enough not to matter).
                carAt(pos - Vec3{0.f, 0.f, 150.f}, dir * speedUus);
            } else {
                carAt(pos, dir * speedUus);
            }
            tick();
            ++ticks;
        }
        return ticks;
    }

    // Path through the centre of every ring of the current course, ending 400 uu after the last one.
    std::vector<Vec3> ringPath(const Vec3& from) const {
        std::vector<Vec3> pts{from};
        for (const auto& r : mod.course().rings) pts.push_back(courses::toF(r.center));
        const auto& last = mod.course().rings.back();
        pts.push_back(courses::toF(last.center + last.normal * 400.0));
        return pts;
    }
};

inline bool hasChat(const Actions& a, const std::string& needle) {
    for (const auto& c : a.chat)
        if (c.text.find(needle) != std::string::npos) return true;
    return false;
}

inline bool hasTip(const AttemptResult& r, const std::string& code) {
    for (const auto& t : r.tips)
        if (t.code == code) return true;
    return false;
}

inline const Metric* findMetric(const AttemptResult& r, const std::string& key) {
    for (const auto& m : r.metrics)
        if (m.key == key) return &m;
    return nullptr;
}

} // namespace ct
