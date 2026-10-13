// Glue integration: refineContext as RLTrainer::refreshContext calls it (every 0.25 s, under the engine lock,
// right after MapsService::describeCurrentMap), and HUD prompts written with the player's button style.
#include <string>
#include <type_traits>

#include "tests/TestFramework.h"

#include "core/common/Controls.h"
#include "tests/maps/MapsTestUtil.h"

using namespace rlt;
using namespace maps_test;

namespace {

static_assert(std::is_same_v<decltype(&MapsModule::refineContext), void (MapsModule::*)(GameContext&) const>,
              "refineContext is read-only: the glue calls it on every context refresh");

const char* kGiantId = "2025564083";

// What MapsService::describeCurrentMap gives for a workshop map (fresh on every refresh).
GameContext described(const std::string& mapName, const std::string& id = "", MapCategory cat = MapCategory::Other,
                      const std::string& title = "") {
    GameContext g;
    g.mode = GameMode::Workshop;
    g.isCustomMap = true;
    g.mapName = mapName;
    g.workshopId = id;
    g.mapTitle = title.empty() ? prettifyMapName(mapName) : title;
    g.category = cat;
    return g;
}

// One glue context refresh: fresh description refined by the module, handed to the engine (here the harness)
// only when a compared field changed, like RLTrainer::refreshContext. True when the context changed.
bool refresh(Harness& h, GameContext fresh) {
    h.mod.refineContext(fresh);
    const GameContext& cur = h.game;
    if (fresh.mode == cur.mode && fresh.mapName == cur.mapName && fresh.workshopId == cur.workshopId &&
        fresh.category == cur.category && fresh.isCustomMap == cur.isCustomMap)
        return false;
    h.setContext(fresh.mode, fresh.isCustomMap, fresh.mapName, fresh.workshopId, fresh.mapTitle, fresh.category);
    return true;
}

// Finishes one own-timed run on the current map so the HUD shows the "relaunch a run" prompt.
void finishOwnRun(Harness& h) {
    h.tick(0.1f, {}, {});
    h.tick(0.2f, {}, {800.f, 0.f, 0.f});
    h.tick(3.2f, {}, {800.f, 0.f, 0.f});
    h.event(GameEventKind::Goal);
}

} // namespace

// ---- refineContext -----------------------------------------------------------------------------------------
TEST(maps_glue_refine_is_safe_before_any_map) {
    Harness h;  // no context, no stats, nothing loaded
    GameContext g = described("Some_Map");
    const GameContext before = g;
    h.mod.refineContext(g);
    CHECK(g.mapTitle == before.mapTitle && g.category == before.category && g.workshopId.empty());
    GameContext blank;
    blank.isCustomMap = true;  // nothing known about the map
    h.mod.refineContext(blank);
    CHECK(blank.mapName.empty() && blank.workshopId.empty() && blank.mapTitle.empty());
    CHECK(blank.category == MapCategory::Standard);
    GameContext stock = described("Some_Map");
    stock.isCustomMap = false;
    stock.mode = GameMode::Freeplay;
    stock.category = MapCategory::Standard;
    h.mod.refineContext(stock);
    CHECK(stock.category == MapCategory::Standard);
    // Read-only: no stats entry, no map entered.
    CHECK(h.mod.statsList().empty());
    CHECK(!h.mod.status().onCustomMap);
    CHECK(h.all.empty());
}

TEST(maps_glue_choice_reaches_the_first_context_of_a_map) {
    Harness h;
    h.command("category", {"dribble", "Some_Map"});  // chosen from the console, Maps tab never opened
    GameContext g = described("Some_Map");
    h.mod.refineContext(g);
    CHECK(g.category == MapCategory::Dribble);
    CHECK(!h.mod.status().onCustomMap && h.mod.statsList().size() == 1);
    // Loading the map: the very first context already carries the choice.
    CHECK(refresh(h, described("Some_Map")));
    CHECK(h.game.category == MapCategory::Dribble);
    CHECK(h.mod.status().category == MapCategory::Dribble && h.mod.status().categoryOverride);
    CHECK(!refresh(h, described("Some_Map")));
}

TEST(maps_glue_removed_choice_does_not_stick_through_the_engine_context) {
    Harness h;
    CHECK(refresh(h, described("Some_Map")));
    CHECK(h.mod.status().onCustomMap);
    CHECK(h.game.category == MapCategory::Other);
    CHECK(!refresh(h, described("Some_Map")));  // stable

    h.command("category", {"rings"});
    CHECK(refresh(h, described("Some_Map")));
    CHECK(h.game.category == MapCategory::Rings);
    CHECK(h.mod.status().category == MapCategory::Rings && h.mod.status().categoryOverride);
    CHECK(!refresh(h, described("Some_Map")));

    // Back to auto: the engine context still says Rings, it must not be read back as the map's category.
    h.command("category", {"auto"});
    CHECK(refresh(h, described("Some_Map")));
    CHECK(h.game.category == MapCategory::Other);
    const MapsLiveStatus st = h.mod.status();
    CHECK(st.category == MapCategory::Other && !st.categoryOverride);
    CHECK(st.categorySource == "inconnue");
    CHECK(!refresh(h, described("Some_Map")));
    MapStatsView v;
    CHECK(h.mod.statsFor("some_map", v));
    CHECK(v.category == MapCategory::Other && !v.categoryOverride);

    // Same with a category coming from the installed maps list: auto goes back to it.
    h.command("category", {"obstacle"});
    CHECK(refresh(h, described("Some_Map", "", MapCategory::Shooting)));
    CHECK(h.game.category == MapCategory::Obstacle);
    h.command("category", {"auto"});
    CHECK(refresh(h, described("Some_Map", "", MapCategory::Shooting)));
    CHECK(h.game.category == MapCategory::Shooting);
    CHECK(h.mod.status().categorySource == "liste des maps");
    CHECK(!refresh(h, described("Some_Map", "", MapCategory::Shooting)));
}

TEST(maps_glue_refine_keeps_the_known_workshop_id) {
    Harness h;
    CHECK(refresh(h, described("LethamyrGiantRings", kGiantId, MapCategory::Rings, "Lethamyr's Giant Rings")));
    CHECK(h.mod.status().key == kGiantId);
    // The glue lost the id (map launched by another loader): same file, the id key stays.
    GameContext g = described("LethamyrGiantRings");
    h.mod.refineContext(g);
    CHECK(g.workshopId == kGiantId);
    CHECK(g.category == MapCategory::Rings);
    CHECK(g.mapTitle == "Lethamyr's Giant Rings");
    CHECK(!refresh(h, described("LethamyrGiantRings")));
    CHECK(h.mod.status().key == kGiantId);
    CHECK(h.mod.statsList().size() == 1);  // no second entry under the file name
    // Another file never borrows the id.
    GameContext other = described("Other_Map");
    h.mod.refineContext(other);
    CHECK(other.workshopId.empty());
}

// ---- Button style ------------------------------------------------------------------------------------------
TEST(maps_glue_hud_prompt_uses_the_button_style) {
    Harness h;
    h.profile.valid = true;
    h.profile.usingGamepad = true;
    h.profile.gamepadBindings = {{"ResetTraining", "XboxTypeS_Back"}};
    h.setContext(GameMode::Workshop, true, "Some_Map");
    finishOwnRun(h);
    DrawList d = h.draw();
    CHECK(!d.panels.empty());
    CHECK(containsText(d.panels.front(), "Reset (Back) pour relancer un run"));  // Auto: Xbox names
    h.settings.profile.buttonStyle = 2;  // PlayStation
    h.applySettings();
    d = h.draw();
    CHECK(containsText(d.panels.front(), "Reset (Share) pour relancer un run"));
    CHECK(!containsText(d.panels.front(), "(Back)"));
    h.settings.profile.buttonStyle = 1;  // Xbox
    h.applySettings();
    d = h.draw();
    CHECK(containsText(d.panels.front(), "Reset (Back) pour relancer un run"));
}
