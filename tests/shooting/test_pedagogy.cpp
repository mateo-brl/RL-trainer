// Tirs & flicks for a beginner: niveau 1 reachable with a typical beginner attempt (the top level stays demanding),
// a new player starts at niveau 1, "flip" in every text, and a compact HUD of at most 3 main lines without ms or
// degrees.

#include <cmath>

#include "core/common/Units.h"
#include "tests/shooting/ShootingTestUtil.h"

using namespace rlt;
using namespace rlt::shoot;
using namespace shtest;

namespace {

void runTicks(Harness& h, World& w, int n) {
    for (int i = 0; i < n; ++i) {
        const Actions a = h.tick(w.snap());
        w.apply(a);
        w.advance();
    }
}

void runToLive(Harness& h, World& w) {
    for (int i = 0; i < 600 && h.mod.phase() != ShootPhase::Live; ++i) runTicks(h, w, 1);
}

ViewContext viewFor(const World& w) {
    ViewContext v;
    v.valid = true;
    v.camPos = w.car.pos + Vec3{0.f, -300.f, 150.f};
    v.camBasis = basisFromRot(rotDeg(-5.f, 90.f, 0.f));
    v.fovDeg = 110.f;
    v.screenW = 1920.f;
    v.screenH = 1080.f;
    v.carValid = true;
    v.carVisualPos = w.car.pos;
    v.carVisualBasis = w.car.basis;
    v.ballValid = true;
    v.ballVisualPos = w.ball.pos;
    v.ballVel = w.ball.vel;
    return v;
}

// Main (non-detail) lines of the panel, checked against the compact HUD contract.
std::vector<std::string> mainLines(Harness& h, const World& w, const char* phase, std::string* title = nullptr) {
    const DrawList d = h.draw(viewFor(w));
    std::vector<std::string> out;
    CHECK_MSG(d.panels.size() == 1, phase);
    if (d.panels.empty()) return out;
    if (title) *title = d.panels[0].title;
    for (const auto& l : d.panels[0].lines) {
        CHECK_MSG(l.text.find("dodge") == std::string::npos, l.text);
        if (l.detail) continue;
        out.push_back(l.text);
        CHECK_MSG(l.text.find(" ms") == std::string::npos && l.text.find(" uu") == std::string::npos, l.text);
    }
    CHECK_MSG(!out.empty() && out.size() <= 3, std::string(phase) + ": " + std::to_string(out.size()));
    return out;
}

bool noDegrees(const std::vector<std::string>& lines) {
    for (const auto& l : lines)
        if (l.find("\xC2\xB0") != std::string::npos) return false;
    return true;
}

// A beginner's front flick from a stopped car: slow (41 km/h), flip a bit late, ball a little off the roof centre,
// on goal but outside any zone.
FlickReport beginnerFlick() {
    FlickReport r;
    r.cls.base = FlickType::Front;
    r.category = FlickType::Front;
    r.contact = true;
    r.power = 1150.f;
    r.gain = 1150.f;
    r.f.dodgeFwd = 1.f;
    r.f.delay = 0.45f;
    r.f.launchFwd = 0.9f;
    r.f.vertFrac = 0.4f;
    r.f.keptFrac = 1.f;
    r.carryTime = 0.3f;
    r.relSpeedAtJump = 60.f;
    r.roofOffset = {5.f, 20.f, 0.f};
    r.elevationDeg = 30.f;
    r.launchVel = {0.f, 1000.f, 575.f};
    return r;
}

} // namespace

TEST(shooting_beginner_flick_succeeds_at_niveau_1) {
    const AimResult aim = scoreAim(goalMouthRect(), true, {500.f, 300.f});
    CHECK(aim.onGoal);
    FlickGradeParams p;
    p.requested = static_cast<int>(FlickType::Front);
    p.level = kShootStartLevel;
    p.minPower = flickMinPower(kShootStartLevel);
    const AttemptResult easy = gradeFlick(beginnerFlick(), aim, p, PlayerProfile{});
    CHECK(easy.success);
    CHECK(easy.verdict >= Verdict::Ok);
    // Niveau 10 stays demanding: the same flick is far too weak, and the one tip says so in words.
    p.level = 1.f;
    p.minPower = flickMinPower(1.f);
    const AttemptResult hard = gradeFlick(beginnerFlick(), aim, p, PlayerProfile{});
    CHECK(!hard.success);
    CHECK(!hard.tips.empty());
    if (!hard.tips.empty()) {
        CHECK(hard.tips.front().text.find("puissance") != std::string::npos ||
              hard.tips.front().text.find("toit") != std::string::npos ||
              hard.tips.front().text.find("plus tôt") != std::string::npos);
        CHECK(hard.tips.front().text.find("\xC2\xB0") == std::string::npos);
    }
    // A flick that never reaches the goal while aimed at it: told to flick harder, not to aim.
    FlickReport shortFlick = beginnerFlick();
    shortFlick.power = 600.f;
    p.level = kShootStartLevel;
    p.minPower = flickMinPower(kShootStartLevel);
    p.aimErrorDeg = 5.f;
    const AttemptResult s = gradeFlick(shortFlick, scoreAim(goalMouthRect(), false, {}), p, PlayerProfile{});
    CHECK(!s.success);
    bool shortTip = false, aimTip = false;
    for (const auto& t : s.tips) {
        shortTip = shortTip || t.code == "shooting.flick_short";
        aimTip = aimTip || t.code == "shooting.flick_off_target";
    }
    CHECK(shortTip && !aimTip);
}

TEST(shooting_beginner_target_at_niveau_1) {
    // A shot 230 uu right of the centre zone's middle: inside the big niveau 1 zone, outside the niveau 10 one.
    const ZoneRect easy = zoneRect(Zone::Center, kShootStartLevel);
    const ZoneRect hard = zoneRect(Zone::Center, 1.f);
    const Vec2 hit{easy.u + 230.f, easy.z};
    TargetGradeParams p;
    p.zone = Zone::Center;
    p.setup = TargetSetup::Rolling;
    p.rect = easy;
    CHECK(gradeTarget(scoreAim(easy, true, hit), 1500.f, p).success);
    p.rect = hard;
    const AttemptResult miss = gradeTarget(scoreAim(hard, true, {hard.u + 230.f, hard.z}), 1500.f, p);
    CHECK(!miss.success);
    CHECK(!miss.tips.empty() && miss.tips.front().code == "shooting.target_right");
}

TEST(shooting_new_player_starts_at_niveau_1) {
    Harness h;
    CHECK_NEAR(h.mod.flickLevel(), kShootStartLevel, 1e-6f);
    for (int i = 1; i < kTargetSetupCount; ++i)
        CHECK_NEAR(h.mod.targetLevel(static_cast<TargetSetup>(i)), kShootStartLevel, 1e-6f);
    CHECK_EQ(shootPlayerLevel(kShootStartLevel), 1);
    CHECK_EQ(shootPlayerLevel(1.f), 10);
    // A level saved for a drill never played does not count; a played one is kept.
    Store st;
    st.setFloat("shooting.stair.flick.level", 0.5f);
    st.setInt("shooting.stair.flick.attempts", 0);
    st.setFloat("shooting.stair.rolling.level", 0.6f);
    st.setInt("shooting.stair.rolling.attempts", 4);
    Harness other;
    other.mod.load(st);
    CHECK_NEAR(other.mod.flickLevel(), kShootStartLevel, 1e-6f);
    CHECK_NEAR(other.mod.targetLevel(TargetSetup::Rolling), 0.6f, 1e-6f);
}

TEST(shooting_texts_say_flip_and_tag_difficulty) {
    // Wrong-type corrections for every requested flick: "flip", and no rotation given in degrees.
    for (int want = 0; want < kRequestableFlickCount; ++want) {
        FlickReport r = beginnerFlick();
        r.cls.base = FlickType::Side;
        r.category = FlickType::Side;
        r.f.dodgeFwd = 0.f;
        r.f.dodgeSide = 1.f;
        r.f.yawTurnDeg = 10.f;
        FlickGradeParams p;
        p.requested = want;
        const AttemptResult a = gradeFlick(r, scoreAim(goalMouthRect(), true, {0.f, 300.f}), p, PlayerProfile{});
        for (const auto& t : a.tips) {
            CHECK_MSG(t.text.find("dodge") == std::string::npos && t.text.find("Dodge") == std::string::npos, t.text);
            CHECK_MSG(t.text.find("\xC2\xB0") == std::string::npos, t.text);
        }
    }
    FlickReport none = beginnerFlick();
    none.contact = false;
    CHECK(gradeFlick(none, scoreAim(goalMouthRect(), false, {}), FlickGradeParams{}, PlayerProfile{})
              .tips.front()
              .text.find("Flippe") == 0);
    // Basics first: the front flick and the back flick are for beginners, the musty is advanced.
    CHECK(flickTier(FlickType::Front) == ShootTier::Beginner);
    CHECK(flickTier(FlickType::Backflip) == ShootTier::Beginner);
    CHECK(flickTier(FlickType::Flick90) == ShootTier::Intermediate);
    CHECK(flickTier(FlickType::Musty) == ShootTier::Advanced);
    CHECK(std::string(tierLabel(ShootTier::Advanced)) == "Avancé");
    CHECK(std::string(shootingAdvice()).find("Débutant : ") == 0);
    // The technical details keep the numbers.
    CHECK(flickTechNote(FlickType::Flick45, 0.6f, -35.f, 230.f).find("65") != std::string::npos);
}

TEST(shooting_hud_compact_budget) {
    // Flick drill: countdown, live, result.
    {
        Harness h;
        World w;
        h.tick(w.snap());
        h.command("flick", {"front"});
        runTicks(h, w, 3);
        std::string title;
        const auto countdown = mainLines(h, w, "flick countdown", &title);
        CHECK(title.find("niveau 1/10") != std::string::npos);
        runToLive(h, w);
        w.ballRides = true;
        runTicks(h, w, 5);
        const auto live = mainLines(h, w, "flick live");
        CHECK(noDegrees(live));
        bool cue = false;
        for (const auto& l : live) cue = cue || l.find("flip avant") != std::string::npos;
        CHECK(cue);
        FlickScript s;
        s.start = w.car.pos;
        s.yawDeg = static_cast<float>(w.car.rot.yaw) * 360.f / 65536.f;
        s.startFrame = w.frame;
        s.team = w.car.team;
        s.launch = {2200.f, 0.f, 350.f};
        const ScriptRun run = buildFlick(s);
        for (size_t i = 0; i < run.snaps.size(); ++i) {
            for (const auto& t : run.touches)
                if (t.first == i) h.touch(t.second);
            h.tick(run.snaps[i]);
        }
        w.frame = run.snaps.back().frame + 1;
        w.car = run.snaps.back().car;
        w.ball = run.snaps.back().ball;
        CHECK(h.mod.phase() == ShootPhase::Result);
        const auto result = mainLines(h, w, "flick result");
        CHECK(!result.empty() && result[0].find("/100") != std::string::npos);
        CHECK(result.back().find("Essai suivant dans") == 0);
    }
    // Targets and power drills.
    for (const char* drill : {"targets", "power"}) {
        Harness h;
        World w;
        h.tick(w.snap());
        h.command(drill);
        runTicks(h, w, 3);
        mainLines(h, w, drill);
        runToLive(h, w);
        const auto live = mainLines(h, w, drill);
        if (std::string(drill) == "power") {
            bool goal = false;
            for (const auto& l : live) goal = goal || l.find("Objectif : ") == 0;
            CHECK(goal);
        }
    }
}
