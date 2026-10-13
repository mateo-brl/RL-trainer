#pragma once
// Shared fixtures of the overlay tests: a module wired to a fake activity host, real schema defaults, synthetic
// snapshots and views.

#include <cmath>
#include <string>
#include <vector>

#include "core/common/History.h"
#include "core/common/Settings.h"
#include "core/modules/overlay/OverlayModule.h"
#include "tests/TestFramework.h"

namespace ovtest {

using namespace rlt;

// Records every activity request: the overlay module must never ask for world control.
class FakeHost : public ActivityHost {
public:
    bool requestActivity(Module* m, const std::string& name) override {
        (void)m;
        ++requests;
        lastName = name;
        return allow;
    }
    void releaseActivity(Module* m) override {
        (void)m;
        ++releases;
    }
    bool ownsActivity(const Module* m) const override {
        (void)m;
        return false;
    }
    bool allow = true;
    int requests = 0;
    int releases = 0;
    std::string lastName;
};

inline bool hasWorldControl(const Actions& a) {
    return a.setBall || a.setCar || a.setBoost || a.overrideInputs || a.freezeCar || a.setGameSpeed ||
           a.countdownMessage || a.goMessage || !a.consoleCommands.empty();
}

struct Harness {
    SettingsSchema schema;
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{32};
    Snapshot snap;
    bool hasSnapshot = true;
    FakeHost host;
    OverlayModule mod;
    bool worldControlSeen = false;

    Harness() {
        describeGeneralSettings(schema);
        OverlayModule::describeSettings(schema);
        schema.applyDefaults(settings);
        game.mode = GameMode::Freeplay;
        game.mapName = "Park_P";
        game.localDate = "2026-10-13";
        game.worldControlAllowed = true;
        profile.valid = true;
        profile.metric = true;
        snap.car.valid = true;
        snap.ball.valid = true;
        snap.car.pos = {0.f, 0.f, 17.f};
        snap.ball.pos = {0.f, 500.f, 93.15f};
        mod.attach(&host);
        mod.onSettingsChanged(settings);
    }

    void set(const std::string& cvar, const std::string& value) {
        CHECK_MSG(schema.apply(settings, cvar, value), cvar);
        mod.onSettingsChanged(settings);
    }

    TickContext tc() const { return TickContext{snap, history, settings, game, profile, hasSnapshot, false, false}; }
    DrawContext dc(float hudScale = 1.f) const {
        return DrawContext{settings, game, profile, snap, hasSnapshot, false, hudScale};
    }

    void note(const Actions& a) { worldControlSeen = worldControlSeen || hasWorldControl(a); }

    // One physics tick: frame + 1 (or + `frames`), time = frame / 120.
    Actions tick(int64_t frames = 1) {
        snap.frame += frames;
        snap.time = static_cast<float>(snap.frame) / 120.f;
        history.push(snap);
        Actions a;
        mod.onTick(tc(), a);
        note(a);
        return a;
    }
    void ticks(int n) {
        for (int i = 0; i < n; ++i) tick();
    }

    Actions event(GameEventKind kind, const std::string& detail = "") {
        GameEvent ev;
        ev.kind = kind;
        ev.frame = snap.frame;
        ev.detail = detail;
        Actions a;
        mod.onGameEvent(ev, tc(), a);
        note(a);
        return a;
    }

    // Changes the game context then dispatches ContextChanged, like Engine::setContext.
    Actions changeContext(GameMode mode, const std::string& map) {
        game.mode = mode;
        game.mapName = map;
        return event(GameEventKind::ContextChanged, map);
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "overlay";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, tc(), a);
        note(a);
        return a;
    }

    Actions touch(const Snapshot& pre, bool newContact = true) {
        TouchEvent t;
        t.frame = pre.frame;
        t.pre = pre;
        t.post1 = pre;
        t.post = pre;
        t.newContact = newContact;
        Actions a;
        mod.onTouch(t, tc(), a);
        note(a);
        return a;
    }

    DrawList draw(const ViewContext& view, float hudScale = 1.f) {
        DrawList dl;
        mod.draw(view, dc(hudScale), dl);
        return dl;
    }
};

// A 1080p view looking at the field, ball and car valid at the given places.
inline ViewContext makeView(const Vec3& ballPos, const Vec3& ballVel = {}, const Vec3& carPos = {0.f, 0.f, 17.f},
                            const Vec3& carVel = {}) {
    ViewContext v;
    v.valid = true;
    v.camPos = {0.f, -1500.f, 300.f};
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.ballValid = true;
    v.ballVisualPos = ballPos;
    v.ballVel = ballVel;
    v.ballRadius = 91.25f;
    v.carValid = true;
    v.carVisualPos = carPos;
    v.carVel = carVel;
    return v;
}

inline const DrawText2D* findText(const DrawList& dl, const std::string& prefix) {
    for (const auto& t : dl.texts2D)
        if (t.text.rfind(prefix, 0) == 0) return &t;
    return nullptr;
}

inline bool sameColor(const Color& a, const Color& b) {
    return std::fabs(a.r - b.r) < 0.6f && std::fabs(a.g - b.g) < 0.6f && std::fabs(a.b - b.b) < 0.6f &&
           std::fabs(a.a - b.a) < 0.6f;
}

} // namespace ovtest
