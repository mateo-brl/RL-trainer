// Basic identity of the module (the detailed suites live in the other files of this folder).
#include "tests/TestFramework.h"

#include "core/modules/ballmech/BallMechModule.h"

using namespace rlt;

TEST(ballmech_identity) {
    BallMechModule m;
    CHECK(std::string(m.id()) == "ballmech");
    CHECK(std::string(m.title()) == "Mécas balle");
    CHECK(m.config().enabled);
    CHECK(!m.drillActive());
    CHECK(m.lastResult() == nullptr);
    CHECK_EQ(m.sessionResets(), 0);
    CHECK_NEAR(m.drillLevel("flipreset"), 0.f, 1e-6f);  // a new player starts at level 1 (the easiest)
    CHECK_NEAR(m.drillLevel("inconnu"), 0.f, 1e-6f);
}

TEST(ballmech_without_host_never_controls_the_world) {
    // No ActivityHost attached: drills are refused, nothing crashes.
    BallMechModule m;
    Settings s;
    SettingsSchema schema;
    BallMechModule::describeSettings(schema);
    schema.applyDefaults(s);
    m.onSettingsChanged(s);
    Snapshot snap;
    TickHistory hist;
    GameContext game;
    game.mode = GameMode::Freeplay;
    game.worldControlAllowed = true;
    PlayerProfile profile;
    TickContext ctx{snap, hist, s, game, profile, false, false, true};
    Command c;
    c.module = "ballmech";
    c.name = "drill";
    c.args = {"flick"};
    Actions a;
    m.onCommand(c, ctx, a);
    CHECK(!m.drillActive());
    CHECK(!a.toasts.empty());
    Actions t;
    m.onTick(ctx, t);
    CHECK(!t.setCar && !t.setBall && !t.freezeCar);
}
