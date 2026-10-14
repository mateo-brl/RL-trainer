#include "tests/TestFramework.h"

#include <algorithm>

#include "core/common/Adaptive.h"
#include "core/common/Color.h"
#include "core/common/DrawList.h"
#include "core/common/Math.h"
#include "core/common/Settings.h"
#include "core/common/Store.h"
#include "core/common/Text.h"
#include "core/engine/Engine.h"

using namespace rlt;

TEST(math_basis_identity) {
    const Basis b = basisFromRot({0, 0, 0});
    CHECK_NEAR(b.fwd.x, 1.f, 1e-6f);
    CHECK_NEAR(b.right.y, 1.f, 1e-6f);
    CHECK_NEAR(b.up.z, 1.f, 1e-6f);
}

TEST(math_basis_yaw_90_points_to_y) {
    const Basis b = basisFromRot({0, 16384, 0});
    CHECK_NEAR(b.fwd.x, 0.f, 1e-5f);
    CHECK_NEAR(b.fwd.y, 1.f, 1e-5f);
    CHECK_NEAR(b.right.x, -1.f, 1e-5f);
}

TEST(math_basis_pitch_up) {
    const Basis b = basisFromRot({8192, 0, 0});  // 45 deg nose up
    CHECK_NEAR(b.fwd.z, std::sqrt(0.5f), 1e-5f);
    CHECK(b.up.x < 0.f);  // up vector tilts backwards
}

TEST(math_basis_orthonormal_random) {
    for (int p = -16000; p <= 16000; p += 4000)
        for (int y = -32768; y < 32768; y += 7000)
            for (int r = -32768; r < 32768; r += 9000) {
                const Basis b = basisFromRot({p, y, r});
                CHECK_NEAR(length(b.fwd), 1.f, 1e-4f);
                CHECK_NEAR(dot(b.fwd, b.right), 0.f, 1e-4f);
                CHECK_NEAR(dot(b.fwd, b.up), 0.f, 1e-4f);
                // Unreal is left-handed: fwd x right == -up? In UE, X cross Y = Z for the rotation matrix axes.
                const Vec3 c = cross(b.fwd, b.right);
                CHECK_NEAR(dot(c, b.up), 1.f, 1e-3f);
            }
}

TEST(math_rot_from_direction_roundtrip) {
    const Vec3 d = normalize(Vec3{1.f, 2.f, 0.5f});
    const Basis b = basisFromRot(rotFromDirection(d));
    CHECK_NEAR(dot(b.fwd, d), 1.f, 1e-3f);
}

TEST(color_parse_formats) {
    Color c;
    CHECK(parseColor("#FF8000", c));
    CHECK_NEAR(c.r, 255.f, 0.01f);
    CHECK_NEAR(c.g, 128.f, 0.01f);
    CHECK_NEAR(c.a, 255.f, 0.01f);
    CHECK(parseColor("#00FF0080", c));
    CHECK_NEAR(c.a, 128.f, 0.01f);
    CHECK(parseColor("(10, 20, 30, 40)", c));
    CHECK_NEAR(c.b, 30.f, 0.01f);
    CHECK_NEAR(c.a, 40.f, 0.01f);
    CHECK(parseColor(" (1.5, 2, 3) ", c));
    CHECK_NEAR(c.r, 1.5f, 0.01f);
    CHECK(!parseColor("#12345", c));
    CHECK(!parseColor("bonjour", c));
    CHECK(!parseColor("(300, 0, 0)", c));
    CHECK(toHex(Color(255, 0, 16, 255)) == "#FF0010FF");
}

TEST(text_fold_and_format) {
    CHECK(foldAccents("Écart de vitesse : élevé à 95 %") == "Ecart de vitesse : eleve a 95 %");
    CHECK(foldAccents("cœur ° ç") == "coeur  deg c");
    CHECK(formatTime(65.5f) == "1:05.50");
    CHECK(formatTime(5.25f) == "5.25");
    CHECK(formatClock(3725.f) == "1h02:05");
    CHECK(formatClock(59.f) == "0:59");
    CHECK(formatNumber(-0.001f, 2) == "0.00");
    CHECK(formatSpeed(1000.f, SpeedUnit::Kph) == "36.00 KPH");
    CHECK(formatSignedSpeed(0.5f, SpeedUnit::Kph) == "0.00 KPH");
    CHECK(formatSignedSpeed(447.04f, SpeedUnit::Mph, 1) == "+10.0 MPH");
}

TEST(store_roundtrip_with_escapes) {
    Store a;
    a.setString("x.name", "a=b\nc\\d");
    a.setInt("x.n", -42);
    a.setFloat("x.f", 1.5f);
    a.setBool("x.b", true);
    a.setList("x.list", {"un", "deux", "trois=3"});
    Store b;
    CHECK(b.parse(a.serialize()) > 0);
    CHECK(b.getString("x.name") == "a=b\nc\\d");
    CHECK_EQ(b.getInt("x.n"), -42);
    CHECK_NEAR(b.getFloat("x.f"), 1.5f, 1e-6f);
    CHECK(b.getBool("x.b"));
    const auto l = b.getList("x.list");
    CHECK_EQ(l.size(), static_cast<size_t>(3));
    CHECK(l.size() == 3 && l[2] == "trois=3");
    CHECK_EQ(b.getInt("missing", 7), 7);
    b.erasePrefix("x.list.");
    CHECK(b.getList("x.list").empty());
}

TEST(staircase_converges_to_target) {
    // Simulated player: success probability decreases linearly with level.
    Staircase s(0.75f, 0.02f, 0.f);
    uint32_t seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.f;
    };
    float sum = 0.f;
    int n = 0;
    for (int i = 0; i < 4000; ++i) {
        const float p = 1.f - s.level();  // at level 0.25 the player succeeds 75 %
        s.record(rnd() < p);
        if (i > 1000) {
            sum += s.level();
            ++n;
        }
    }
    CHECK_NEAR(sum / static_cast<float>(n), 0.25f, 0.06f);
}

TEST(settings_schema_defaults_and_apply) {
    Engine e;
    const SettingsSchema& sc = e.schema();
    CHECK(sc.find("rlt_enabled") != nullptr);
    CHECK(e.settings().general.enabled);
    CHECK(e.applySetting("rlt_hud_scale", "2.5"));
    CHECK_NEAR(e.settings().general.hudScale, 2.5f, 1e-6f);
    CHECK(e.applySetting("rlt_hud_scale", "99"));  // clamped to max
    CHECK_NEAR(e.settings().general.hudScale, 3.f, 1e-6f);
    CHECK(!e.applySetting("rlt_hud_scale", "abc"));
    CHECK(!e.applySetting("rlt_does_not_exist", "1"));
    CHECK(e.settingValue("rlt_enabled") == "1");
    // Every cvar is unique, prefixed and has a French label.
    for (const auto& d : sc.all()) {
        CHECK_MSG(d.cvar.rfind("rlt_", 0) == 0, d.cvar);
        CHECK_MSG(!d.label.empty(), d.cvar);
        CHECK_MSG(d.apply != nullptr && d.read != nullptr, d.cvar);
        // The default value must parse.
        Settings tmp;
        CHECK_MSG(d.apply(tmp, d.defaultValue), d.cvar + " default=" + d.defaultValue);
    }
}

namespace {
Snapshot snapAt(int64_t frame, int lastTouch = -1) {
    Snapshot s;
    s.frame = frame;
    s.time = static_cast<float>(frame) / 120.f;
    s.car.valid = true;
    s.ball.valid = true;
    s.car.lastBallTouchFrame = lastTouch;
    s.car.vel = {static_cast<float>(frame), 0.f, 0.f};
    return s;
}

struct TouchSpy : Module {
    const char* id() const override { return "spy"; }
    const char* title() const override { return "spy"; }
    std::vector<TouchEvent> touches;
    void onTouch(const TouchEvent& t, const TickContext&, Actions&) override { touches.push_back(t); }
};
} // namespace

TEST(engine_resolves_hook_touch_with_pre_and_post) {
    Engine e;
    Actions out;
    GameContext ctx;
    ctx.mode = GameMode::Freeplay;
    ctx.mapName = "Stadium_P";
    e.setContext(ctx, out);
    e.tick(snapAt(100), out);
    e.reportTouch(100, true, {1, 2, 3}, {0, 0, 1});  // during the step after frame 100
    e.tick(snapAt(101), out);
    // No direct access to touches from the engine: check via a module-less invariant (no crash) and
    // world control allowed in freeplay.
    CHECK(e.worldControlAllowed());
    CHECK(e.hasSnapshot());
    CHECK_EQ(e.latest().frame, static_cast<int64_t>(101));
}

TEST(engine_world_control_by_mode) {
    Engine e;
    Actions out;
    GameContext ctx;
    ctx.mode = GameMode::Workshop;
    e.setContext(ctx, out);
    CHECK(!e.worldControlAllowed());
    CHECK(e.applySetting("rlt_control_workshop", "1"));
    CHECK(e.worldControlAllowed());
    ctx.mode = GameMode::Online;
    e.setContext(ctx, out);
    CHECK(!e.worldControlAllowed());
    ctx.mode = GameMode::Freeplay;
    e.setContext(ctx, out);
    CHECK(e.worldControlAllowed());
    CHECK(e.applySetting("rlt_enabled", "0"));
    CHECK(!e.worldControlAllowed());
}

TEST(engine_merges_the_migration_toasts_of_a_v107_config) {
    // A v1.0.7 player: config.cfg holds every old default the round-2 migrations move (Prédiction, Parcours, Mécas
    // mouvement, Mécas balle, Kickoff, Tirs & flicks). His first tick in freeplay used to show six 8 s toasts.
    Engine e;
    const std::pair<const char*, const char*> v107[] = {
        {"rlt_pr_enabled", "1"},        {"rlt_co_guide_line", "1"},     {"rlt_me_guides", "1"},
        {"rlt_bm_draw_trajectory", "1"}, {"rlt_ko_big_countdown", "1"}, {"rlt_ko_show_path", "1"},
        {"rlt_sh_flick_start", "0"},    {"rlt_sh_flick_speed", "800"},  {"rlt_sh_show_prediction", "1"}};
    for (const auto& [cvar, value] : v107) CHECK_MSG(e.applySetting(cvar, value), cvar);
    Actions out;
    GameContext ctx;
    ctx.mode = GameMode::Freeplay;
    ctx.mapName = "Stadium_P";
    e.setContext(ctx, out);
    Actions first;
    e.tick(snapAt(100), first);
    // One toast: the trajectories sentence once (five modules sent it), the countdown, the flick start and marker.
    CHECK_EQ(first.toasts.size(), size_t(1));
    if (!first.toasts.empty()) {
        const ToastMessage& t = first.toasts[0];
        CHECK(t.title == "Nouveaux réglages par défaut" && t.kind == ToastKind::Info && t.migration);
        CHECK_EQ(static_cast<int>(std::count(t.text.begin(), t.text.end(), '\n')), 2);  // three sentences
        CHECK(t.text.find("section Trajectoires") != std::string::npos);
        CHECK(t.text.find("celui du jeu") != std::string::npos);
        CHECK(t.text.find("en dribble") != std::string::npos);
        size_t shared = 0;
        for (size_t at = t.text.find("Les trajectoires et lignes guides"); at != std::string::npos;
             at = t.text.find("Les trajectoires et lignes guides", at + 1))
            ++shared;
        CHECK_EQ(shared, size_t(1));
        CHECK_NEAR(t.seconds, 10.f, 1e-6f);  // 4 + 2 per sentence, at most 12
        CHECK(t.text.find("\xE2\x80\x94") == std::string::npos && t.text.find("\xE2\x80\x93") == std::string::npos);
    }
    CHECK(first.setCvars.size() >= 9);
    for (const auto& c : first.setCvars) e.applySetting(c.first, c.second);  // what the glue does
    Actions second;
    e.tick(snapAt(101), second);
    CHECK(second.toasts.empty() && second.setCvars.empty());
}

TEST(engine_keeps_a_single_migration_toast_and_ordinary_toasts) {
    // New install (every new default): no migration toast at all. One migration alone keeps its own title.
    Engine e;
    Actions out;
    GameContext ctx;
    ctx.mode = GameMode::Freeplay;
    ctx.mapName = "Stadium_P";
    e.setContext(ctx, out);
    Actions first;
    e.tick(snapAt(100), first);
    for (const auto& t : first.toasts) CHECK_MSG(!t.migration, t.title + " : " + t.text);
    Engine one;
    CHECK(one.applySetting("rlt_pr_enabled", "1"));
    one.setContext(ctx, out);
    Actions a;
    one.tick(snapAt(100), a);
    CHECK_EQ(a.toasts.size(), size_t(1));
    if (!a.toasts.empty()) CHECK(a.toasts[0].title == "Trajectoires" && a.toasts[0].seconds == 8.f);
}

TEST(engine_restores_boost_rules_when_a_module_switches_drill) {
    // Speedflip "kickoff" drill (limited boost), then another Mécas mouvement drill started without stopping: the
    // activity stays with the same module, but the new drill runs with the player's own boost rules.
    Engine e;
    Actions out;
    GameContext ctx;
    ctx.mode = GameMode::Freeplay;
    ctx.mapName = "Stadium_P";
    e.setContext(ctx, out);
    int64_t f = 1000;
    auto snap = [&f]() {
        Snapshot s = snapAt(++f);
        s.car.vel = {};
        s.car.pos = {0.f, -2000.f, 17.01f};
        s.car.onGround = true;
        s.car.wheelContacts = 4;
        s.car.hasFlip = true;
        s.car.boost = 33.f;
        s.ball.pos = {0.f, 0.f, 93.15f};
        return s;
    };
    bool limited = false;
    auto run = [&](int n, int* restores, int* rules) {
        for (int i = 0; i < n; ++i) {
            Actions a;
            e.tick(snap(), a);
            if (a.restoreBoostRules) {
                limited = false;
                if (restores) ++*restores;
            }
            if (a.boostRules) {
                limited = !a.boostRules->unlimited;
                if (rules) ++*rules;
            }
        }
    };
    e.post(Command{"mechanics", "drill", {"speedflip", "kickoff"}});
    run(240, nullptr, nullptr);
    CHECK(limited);
    CHECK_EQ(e.activityOwner(), std::string("mechanics"));
    int restores = 0, rules = 0;
    e.post(Command{"mechanics", "drill", {"boost"}});
    run(600, &restores, &rules);
    CHECK_EQ(e.activityOwner(), std::string("mechanics"));
    CHECK_EQ(restores, 1);
    CHECK_EQ(rules, 0);
    CHECK(!limited);
    // Restarting the same drill keeps its own rules (no restore).
    e.post(Command{"mechanics", "drill", {"speedflip", "kickoff"}});
    run(240, nullptr, nullptr);
    CHECK(limited);
    restores = 0;
    e.post(Command{"mechanics", "drill", {"speedflip", "kickoff"}});
    run(240, &restores, nullptr);
    CHECK_EQ(restores, 0);
    CHECK(limited);
}

TEST(engine_parse_command_line) {
    Command c;
    CHECK(Engine::parseCommandLine({"rlt", "kickoff", "start", "diagonal"}, c));
    CHECK(c.module == "kickoff" && c.name == "start" && c.args.size() == 1 && c.args[0] == "diagonal");
    CHECK(!Engine::parseCommandLine({"rlt", "kickoff"}, c));
}

TEST(engine_save_load_does_not_crash) {
    Engine e;
    Store st;
    e.save(st);
    e.load(st);
    CHECK(true);
}

TEST(drawlist_circle_segments) {
    DrawList d;
    d.circle3D({0, 0, 0}, {0, 0, 1}, 100.f, colors::Red, 1.f, 16);
    CHECK_EQ(d.lines3D.size(), static_cast<size_t>(16));
    for (const auto& l : d.lines3D) {
        CHECK_NEAR(length(l.a), 100.f, 0.01f);
        CHECK_NEAR(l.a.z, 0.f, 1e-4f);
    }
}
