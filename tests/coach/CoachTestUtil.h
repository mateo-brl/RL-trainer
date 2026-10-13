#pragma once
// Helpers shared by the coach tests: a fake engine environment (contexts built from plain members),
// result builders and a fake ActivityHost.

#include <cmath>
#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/coach/CoachModule.h"
#include "tests/TestFramework.h"

namespace coachtest {

using namespace rlt;

struct Env {
    Snapshot snap;
    TickHistory history{8};
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    bool hasSnapshot = true;

    Env() {
        SettingsSchema schema;
        CoachModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.mapName = "Park_P";
        game.localDate = "2026-10-13";
        game.worldControlAllowed = true;
        snap.frame = 12000;
        snap.time = static_cast<float>(snap.frame) / kTickRate;
        snap.car.valid = true;
        snap.ball.valid = true;
    }

    TickContext tick() const { return TickContext{snap, history, settings, game, profile, hasSnapshot, false, true}; }
    DrawContext drawCtx() const { return DrawContext{settings, game, profile, snap, hasSnapshot, false, 1.f}; }
    ViewContext view() const {
        ViewContext v;
        v.valid = true;
        v.screenW = 1920.f;
        v.screenH = 1080.f;
        return v;
    }
    // Advances the physics clock (no tick callback).
    void advance(float seconds) {
        snap.frame += static_cast<int64_t>(std::lround(seconds * kTickRate));
        snap.time = static_cast<float>(snap.frame) / kTickRate;
    }
};

// Module wired to the environment settings.
inline void setup(CoachModule& m, const Env& env) { m.onSettingsChanged(env.settings); }

struct R {
    AttemptResult r;
    R(const std::string& skill, float score, bool success) {
        r.module = "test";
        r.skill = skill;
        r.title = "";
        r.score = score;
        r.success = success;
        r.verdict = verdictFromScore(score);
    }
    R& title(const std::string& t) {
        r.title = t;
        return *this;
    }
    R& module(const std::string& m) {
        r.module = m;
        return *this;
    }
    R& tip(const std::string& code, const std::string& text, float weight = 1.f) {
        r.tips.push_back(Tip{code, text, weight});
        return *this;
    }
    R& metric(const std::string& key, const std::string& label, float value, const std::string& unit, float ideal,
              bool hasIdeal = true) {
        Metric m;
        m.key = key;
        m.label = label;
        m.value = value;
        m.unit = unit;
        m.ideal = ideal;
        m.hasIdeal = hasIdeal;
        r.metrics.push_back(m);
        return *this;
    }
    R& record(float value, bool lowerIsBetter) {
        r.valueForRecord = value;
        r.lowerIsBetter = lowerIsBetter;
        r.hasRecordValue = true;
        return *this;
    }
    R& verdict(Verdict v) {
        r.verdict = v;
        return *this;
    }
    operator const AttemptResult&() const { return r; }
};

inline Actions send(CoachModule& m, const Env& env, const AttemptResult& r) {
    Actions a;
    m.onResult(r, env.tick(), a);
    return a;
}

inline Actions event(CoachModule& m, const Env& env, GameEventKind kind) {
    GameEvent ev;
    ev.kind = kind;
    Actions a;
    m.onGameEvent(ev, env.tick(), a);
    return a;
}

inline Actions command(CoachModule& m, const Env& env, const std::string& name, std::vector<std::string> args = {}) {
    Command c;
    c.module = "coach";
    c.name = name;
    c.args = std::move(args);
    Actions a;
    m.onCommand(c, env.tick(), a);
    return a;
}

inline bool chatContains(const Actions& a, const std::string& needle) {
    for (const auto& c : a.chat)
        if (c.text.find(needle) != std::string::npos) return true;
    return false;
}

inline bool toastContains(const Actions& a, const std::string& needle) {
    for (const auto& t : a.toasts)
        if (t.title.find(needle) != std::string::npos || t.text.find(needle) != std::string::npos) return true;
    return false;
}

inline bool hasWorldControl(const Actions& a) {
    return a.setBall || a.setCar || a.setBoost || a.overrideInputs || a.freezeCar || a.setGameSpeed ||
           a.countdownMessage || a.goMessage || !a.consoleCommands.empty();
}

class FakeHost : public ActivityHost {
public:
    bool allow = true;
    int requests = 0;
    int releases = 0;
    const Module* owner = nullptr;
    bool requestActivity(Module* m, const std::string& name) override {
        (void)name;
        ++requests;
        if (!allow) return false;
        owner = m;
        return true;
    }
    void releaseActivity(Module* m) override {
        ++releases;
        if (owner == m) owner = nullptr;
    }
    bool ownsActivity(const Module* m) const override { return owner == m; }
};

} // namespace coachtest
