#pragma once
// Helpers shared by the maps module tests: synthetic contexts, snapshots, Kismet variables, fake host.

#include <string>
#include <vector>

#include "core/common/Module.h"
#include "core/modules/maps/MapsModule.h"

namespace maps_test {

using namespace rlt;

inline KismetVar kInt(const std::string& name, int v) {
    KismetVar k;
    k.name = name;
    k.type = KismetVar::Type::Int;
    k.i = v;
    return k;
}

inline KismetVar kBool(const std::string& name, bool v) {
    KismetVar k;
    k.name = name;
    k.type = KismetVar::Type::Bool;
    k.b = v;
    return k;
}

inline KismetVar kStr(const std::string& name, const std::string& v) {
    KismetVar k;
    k.name = name;
    k.type = KismetVar::Type::String;
    k.s = v;
    return k;
}

inline KismetVar kFloat(const std::string& name, float v) {
    KismetVar k;
    k.name = name;
    k.type = KismetVar::Type::Float;
    k.f = v;
    return k;
}

// Leth's Giant Rings variable set.
inline std::vector<KismetVar> lethVars(bool timing, int level, int deaths = 0, int seconds = 0) {
    return {kInt("Deaths", deaths), kInt("Level", level), kInt("Seconds", seconds), kBool("Timing", timing),
            kStr("bmlog", "")};
}

inline Snapshot carSnap(float time, Vec3 pos, Vec3 vel, bool valid = true) {
    Snapshot s;
    s.frame = static_cast<int64_t>(time * 120.f + 0.5f);
    s.time = time;
    s.car.valid = valid;
    s.car.pos = pos;
    s.car.vel = vel;
    s.ball.valid = true;
    return s;
}

class FakeHost : public ActivityHost {
public:
    bool requestActivity(Module* m, const std::string& name) override {
        (void)m;
        (void)name;
        ++requests;
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
};

// Drives a MapsModule like the engine does.
struct Harness {
    Settings settings;
    GameContext game;
    PlayerProfile profile;
    TickHistory history{8};
    Snapshot snap;
    bool hasSnapshot = false;
    FakeHost host;
    MapsModule mod;
    Actions all;  // every action emitted so far

    Harness() {
        mod.attach(&host);
        mod.onSettingsChanged(settings);
    }

    TickContext ctx() const {
        return TickContext{snap, history, settings, game, profile, hasSnapshot, false, game.worldControlAllowed};
    }

    void applySettings() { mod.onSettingsChanged(settings); }

    Actions setContext(GameMode mode, bool custom, const std::string& mapName, const std::string& workshopId = "",
                       const std::string& title = "", MapCategory cat = MapCategory::Standard) {
        // Like Engine::setContext: a map change drops the tick history, so the module gets no snapshot.
        if (mode != game.mode || mapName != game.mapName) hasSnapshot = false;
        game.mode = mode;
        game.isCustomMap = custom;
        game.mapName = mapName;
        game.workshopId = workshopId;
        game.mapTitle = title;
        game.category = cat;
        game.localDate = "2026-10-13";
        game.worldControlAllowed = mode == GameMode::Freeplay;
        GameEvent ev;
        ev.kind = GameEventKind::ContextChanged;
        ev.detail = mapName;
        return event(ev);
    }

    Actions event(const GameEvent& ev) {
        Actions a;
        mod.onGameEvent(ev, ctx(), a);
        keep(a);
        return a;
    }

    Actions event(GameEventKind kind) {
        GameEvent ev;
        ev.kind = kind;
        return event(ev);
    }

    Actions tick(float time, Vec3 pos = {}, Vec3 vel = {}, bool valid = true, bool paused = false) {
        snap = carSnap(time, pos, vel, valid);
        snap.paused = paused;
        history.push(snap);
        hasSnapshot = true;
        Actions a;
        mod.onTick(ctx(), a);
        keep(a);
        return a;
    }

    Actions kismet(const std::vector<KismetVar>& vars) {
        Actions a;
        mod.onKismet(vars, ctx(), a);
        keep(a);
        return a;
    }

    // Tick then Kismet at the same game time (as the glue does).
    Actions step(float time, const std::vector<KismetVar>& vars, Vec3 pos = {}, Vec3 vel = {}) {
        Actions a = tick(time, pos, vel);
        Actions b;
        mod.onKismet(vars, ctx(), b);
        keep(b);
        a.merge(std::move(b));
        return a;
    }

    Actions command(const std::string& name, std::vector<std::string> args = {}) {
        Command c;
        c.module = "maps";
        c.name = name;
        c.args = std::move(args);
        Actions a;
        mod.onCommand(c, ctx(), a);
        keep(a);
        return a;
    }

    Actions touch(bool newContact = true) {
        TouchEvent t;
        t.newContact = newContact;
        t.pre = snap;
        t.post = snap;
        Actions a;
        mod.onTouch(t, ctx(), a);
        keep(a);
        return a;
    }

    DrawList draw(float screenW = 1920.f, float screenH = 1080.f) {
        ViewContext v;
        v.valid = true;
        v.screenW = screenW;
        v.screenH = screenH;
        DrawContext d{settings, game, profile, snap, hasSnapshot, false, screenH / 1080.f};
        DrawList out;
        mod.draw(v, d, out);
        return out;
    }

    void keep(const Actions& a) {
        Actions copy = a;
        all.merge(std::move(copy));
    }
};

// A full Leth Giant Rings run: Timing false -> true at t0, Level 1..20 every `levelTime`, then 20 -> 1.
// Returns the actions of the final sample.
inline Actions lethRun(Harness& h, float t0, float levelTime, std::vector<float> slowLevels = {}) {
    float t = t0;
    h.step(t, lethVars(false, 1));
    t += 0.1f;
    h.step(t, lethVars(true, 1));  // run start
    for (int lvl = 2; lvl <= 20; ++lvl) {
        float dt = levelTime;
        if (static_cast<size_t>(lvl - 2) < slowLevels.size()) dt += slowLevels[static_cast<size_t>(lvl - 2)];
        t += dt;
        h.step(t, lethVars(true, lvl));
    }
    t += levelTime;
    return h.step(t, lethVars(true, 1));  // course completed: back to level 1
}

inline bool containsText(const DrawPanel& p, const std::string& needle) {
    if (p.title.find(needle) != std::string::npos) return true;
    for (const auto& l : p.lines)
        if (l.text.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace maps_test
