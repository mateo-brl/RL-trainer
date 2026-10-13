// Tests of the automatic hotkey picker.
#include <algorithm>
#include <cmath>

#include "tests/TestFramework.h"
#include "tests/profile/ProfileTestUtil.h"

#include "core/modules/profile/ProfileHotkeys.h"

using namespace rlt;
using namespace rlt::pf;
using namespace rlt_pf_test;

namespace {
std::vector<HotkeyCommand> twoCommands() {
    ProfileConfig cfg;  // menu + stop by default
    return hotkeyCommands(cfg);
}
BakkesBinds allFree(const std::vector<std::string>& keys) {
    BakkesBinds b;
    for (const auto& k : keys) b[bindMapKey(k)] = "";
    return b;
}
} // namespace

TEST(hotkeys_commands_from_settings) {
    ProfileConfig cfg;
    auto cmds = hotkeyCommands(cfg);
    CHECK_EQ(cmds.size(), static_cast<size_t>(2));
    CHECK_EQ(cmds[0].command, "rlt_menu");
    CHECK_EQ(cmds[1].command, "rlt engine stop");
    cfg.hotkeyFirstTouch = true;
    cfg.hotkeyKickoff = true;
    cfg.hotkeyMenu = false;
    cmds = hotkeyCommands(cfg);
    CHECK_EQ(cmds.size(), static_cast<size_t>(3));
    CHECK_EQ(cmds[1].command, "rlt firsttouch drill mix");
    CHECK_EQ(cmds[2].command, "rlt kickoff start random");
    CHECK(isKnownHotkeyCommand("rlt_menu"));
    CHECK(!isKnownHotkeyCommand("unbind all"));
}

TEST(hotkeys_candidates) {
    const auto kb = hotkeyCandidates(false);
    CHECK_EQ(kb.front(), "F5");
    for (const auto& k : kb) {
        CHECK(!isReservedHotkey(k));
        CHECK(!isGamepadKey(k));
        CHECK(k != "F6" && k != "F2" && k != "F1");
    }
    const auto all = hotkeyCandidates(true);
    CHECK(all.size() > kb.size());
    CHECK(isGamepadKey(all.back()));
    CHECK(isReservedHotkey("F6"));
    CHECK(isReservedHotkey("f2"));
    CHECK(isReservedHotkey(""));
    CHECK(!isReservedHotkey("F7"));
}

TEST(hotkeys_basic_choice_unchecked) {
    const PlayerProfile p = keyboardProfile();
    const auto props = pickHotkeys(p, twoCommands(), hotkeyCandidates(false), {}, {}, true);
    CHECK_EQ(props.size(), static_cast<size_t>(2));
    CHECK_EQ(props[0].key, "F5");
    CHECK_EQ(props[1].key, "F7");
    CHECK(props[0].state == HotkeyState::Proposed);
    CHECK(!props[0].verified);
    CHECK_EQ(props[0].keyText, "F5");
    // Not allowed without BakkesMod info.
    const auto strict = pickHotkeys(p, twoCommands(), hotkeyCandidates(false), {}, {}, false);
    CHECK(strict[0].state == HotkeyState::NoFreeKey);
    CHECK(strict[0].key.empty());
}

TEST(hotkeys_avoid_every_bound_key) {
    PlayerProfile p = keyboardProfile();
    // F5 used by a known action in the PC list, F7 by an UNKNOWN action in the gamepad list (case differs),
    // F8 used by a chat preset: all must be skipped.
    p.pcBindings.push_back({"RearCamera", "F5"});
    p.gamepadBindings.push_back({"SomeFreeplayCommand", "f7"});
    p.pcBindings.push_back({"ChatPreset1", "F8"});
    const auto cands = hotkeyCandidates(false);
    const auto props = pickHotkeys(p, twoCommands(), cands, allFree(cands), {}, false);
    CHECK_EQ(props[0].key, "F9");
    CHECK_EQ(props[1].key, "F10");
    CHECK(props[0].verified);
    for (const auto& h : props) {
        CHECK(!keyUsedByGame(p, h.key));
        CHECK(h.key != "F5" && h.key != "F7" && h.key != "F8");
    }
}

TEST(hotkeys_avoid_bakkesmod_binds) {
    const PlayerProfile p = keyboardProfile();
    const auto cands = hotkeyCandidates(false);
    BakkesBinds b = allFree(cands);
    b[bindMapKey("F5")] = "togglemenu settings";
    b[bindMapKey("F7")] = "rlt_menu";  // already bound by hand to our command
    const auto props = pickHotkeys(p, twoCommands(), cands, b, {}, false);
    CHECK_EQ(props[0].key, "F7");
    CHECK(props[0].state == HotkeyState::Applied);
    CHECK(props[0].verified);
    CHECK_EQ(props[1].key, "F8");  // F5 is taken by BakkesMod
    CHECK(props[1].state == HotkeyState::Proposed);
}

TEST(hotkeys_keep_applied_keys_stable) {
    const PlayerProfile p = keyboardProfile();
    const auto cands = hotkeyCandidates(false);
    std::vector<AppliedHotkey> applied = {{"F10", "rlt_menu"}};
    BakkesBinds b = allFree(cands);
    b[bindMapKey("F10")] = "rlt_menu";
    const auto props = pickHotkeys(p, twoCommands(), cands, b, applied, false);
    CHECK_EQ(props[0].key, "F10");
    CHECK(props[0].state == HotkeyState::Applied);
    CHECK_EQ(props[1].key, "F5");
    // Same inputs, same answer.
    const auto again = pickHotkeys(p, twoCommands(), cands, b, applied, false);
    for (size_t i = 0; i < props.size(); ++i) CHECK_EQ(again[i].key, props[i].key);
    // Applied key now used by the game: a new key is proposed, the old one is not reused.
    PlayerProfile q = p;
    q.pcBindings.push_back({"Jump", "F10"});
    const auto moved = pickHotkeys(q, twoCommands(), cands, b, applied, false);
    CHECK(moved[0].key != "F10");
    CHECK(moved[0].state == HotkeyState::Proposed);
    // Applied key rebound in BakkesMod to something else: proposed elsewhere.
    BakkesBinds other = b;
    other[bindMapKey("F10")] = "say hello";
    const auto rebound = pickHotkeys(p, twoCommands(), cands, other, applied, false);
    CHECK(rebound[0].key != "F10");
}

TEST(hotkeys_order_of_bindings_does_not_matter) {
    PlayerProfile a = keyboardProfile();
    a.pcBindings.push_back({"RearCamera", "F5"});
    PlayerProfile b = a;
    std::reverse(b.pcBindings.begin(), b.pcBindings.end());
    std::reverse(b.gamepadBindings.begin(), b.gamepadBindings.end());
    const auto cands = hotkeyCandidates(false);
    const auto pa = pickHotkeys(a, twoCommands(), cands, allFree(cands), {}, false);
    const auto pb = pickHotkeys(b, twoCommands(), cands, allFree(cands), {}, false);
    for (size_t i = 0; i < pa.size(); ++i) CHECK_EQ(pa[i].key, pb[i].key);
}

TEST(hotkeys_exhaustion_and_gamepad_candidates) {
    PlayerProfile p = gamepadProfile();
    const auto kb = hotkeyCandidates(false);
    for (const auto& k : kb) p.pcBindings.push_back({"Custom" + k, k});
    const auto cands = hotkeyCandidates(true);
    const auto props = pickHotkeys(p, twoCommands(), cands, allFree(cands), {}, false);
    // Every keyboard candidate is used; the default gamepad list uses the D-pad (chat) and Back (scoreboard).
    CHECK(props[0].state == HotkeyState::NoFreeKey);
    CHECK(props[1].state == HotkeyState::NoFreeKey);
    CHECK(containsText(props[0].note, "Aucune"));
    // Free the D-pad up: it becomes the proposal for the first command.
    PlayerProfile q = p;
    q.gamepadBindings.erase(std::remove_if(q.gamepadBindings.begin(), q.gamepadBindings.end(),
                                           [](const KeyBinding& b) { return b.key == "XboxTypeS_DPad_Up"; }),
                            q.gamepadBindings.end());
    const auto pad = pickHotkeys(q, twoCommands(), cands, allFree(cands), {}, false);
    CHECK_EQ(pad[0].key, "XboxTypeS_DPad_Up");
    CHECK(pad[1].state == HotkeyState::NoFreeKey);
}

// Review fix: an applied hotkey whose BakkesMod bind disappeared (unbind, BakkesMod tab, binds not saved) must
// not stay "En place": it is proposed again, on the same key.
TEST(hotkeys_applied_but_free_is_proposed_again) {
    const PlayerProfile p = keyboardProfile();
    const auto cands = hotkeyCandidates(false);
    const std::vector<AppliedHotkey> applied = {{"F5", "rlt_menu"}};
    const auto props = pickHotkeys(p, twoCommands(), cands, allFree(cands), applied, false);
    CHECK_EQ(props[0].key, "F5");
    CHECK(props[0].state == HotkeyState::Proposed);
    CHECK(props[0].verified);
    CHECK(props[0].previousMissing);
    CHECK(!props[0].previousConflict);
    CHECK_EQ(props[0].previousKey, "F5");
    CHECK(containsText(props[0].note, "réapplique"));
    CHECK_EQ(props[1].key, "F7");
    CHECK(props[1].state == HotkeyState::Proposed);
    CHECK(props[1].previousKey.empty());

    // The same key is preferred even when it is not the first free candidate.
    const std::vector<AppliedHotkey> later = {{"F10", "rlt_menu"}};
    const auto again = pickHotkeys(p, twoCommands(), cands, allFree(cands), later, false);
    CHECK_EQ(again[0].key, "F10");
    CHECK(again[0].state == HotkeyState::Proposed);
    CHECK_EQ(again[1].key, "F5");

    // Unknown BakkesMod state (not checked yet): still trusted as applied, flagged unverified.
    BakkesBinds unknown = allFree(cands);
    unknown.erase(bindMapKey("F5"));
    const auto unk = pickHotkeys(p, twoCommands(), cands, unknown, applied, false);
    CHECK_EQ(unk[0].key, "F5");
    CHECK(unk[0].state == HotkeyState::Applied);
    CHECK(!unk[0].verified);

    // Lost bind on a key the game now uses: no re-proposal there, no conflict warning (nothing left to remove).
    PlayerProfile q = p;
    q.pcBindings.push_back({"RearCamera", "F5"});
    const auto used = pickHotkeys(q, twoCommands(), cands, allFree(cands), applied, false);
    CHECK_EQ(used[0].key, "F7");
    CHECK(used[0].state == HotkeyState::Proposed);
    CHECK(!used[0].previousConflict);
    CHECK(containsText(used[0].note, "Libre dans le jeu et dans BakkesMod"));
}

// Spec gap: an applied key later bound in Rocket League keeps firing our command; the proposal says so.
TEST(hotkeys_game_conflict_is_reported) {
    PlayerProfile p = keyboardProfile();
    p.pcBindings.push_back({"Jump", "F10"});
    const auto cands = hotkeyCandidates(false);
    BakkesBinds b = allFree(cands);
    b[bindMapKey("F10")] = "rlt_menu";
    const std::vector<AppliedHotkey> applied = {{"F10", "rlt_menu"}};
    const auto props = pickHotkeys(p, twoCommands(), cands, b, applied, false);
    CHECK_EQ(props[0].key, "F5");
    CHECK(props[0].state == HotkeyState::Proposed);
    CHECK(props[0].previousConflict);
    CHECK_EQ(props[0].previousKey, "F10");
    CHECK(containsText(props[0].note, "ancienne touche"));
    CHECK(!hasEmDash(props[0].note));

    // Every candidate taken: the note still explains the conflict.
    PlayerProfile full = p;
    for (const auto& k : cands) full.pcBindings.push_back({"Custom" + k, k});
    const auto none = pickHotkeys(full, twoCommands(), cands, b, applied, false);
    CHECK(none[0].state == HotkeyState::NoFreeKey);
    CHECK(containsText(none[0].note, "ancienne touche"));
}

TEST(hotkeys_applied_status_states) {
    PlayerProfile p = keyboardProfile();
    p.pcBindings.push_back({"Jump", "F9"});
    const auto cands = hotkeyCandidates(false);
    BakkesBinds b;
    b[bindMapKey("F5")] = "rlt_menu";         // active
    b[bindMapKey("F7")] = "";                 // removed outside RL Trainer
    b[bindMapKey("F8")] = "say gg";           // rebound by the player
    b[bindMapKey("F9")] = "rlt engine stop";  // game uses F9 now
    const std::vector<AppliedHotkey> applied = {{"F5", "rlt_menu"},
                                                {"F7", "rlt engine stop"},
                                                {"F8", "rlt_menu"},
                                                {"F9", "rlt engine stop"},
                                                {"F10", "rlt_menu"}};  // F10 not checked yet
    const auto st = appliedStatus(p, b, applied, true);
    CHECK_EQ(st.size(), applied.size());
    CHECK(st[0].state == AppliedState::Active);
    CHECK(st[1].state == AppliedState::Missing);
    CHECK(st[2].state == AppliedState::Rebound);
    CHECK(st[3].state == AppliedState::GameConflict);
    CHECK(st[4].state == AppliedState::Unknown);
    CHECK_EQ(st[3].key, "F9");
    CHECK_EQ(st[3].command, "rlt engine stop");
    // Game bindings unknown: no conflict can be claimed.
    const auto blind = appliedStatus(p, b, applied, false);
    CHECK(blind[3].state == AppliedState::Active);
    // A game binding on a key whose bind is gone is not a conflict.
    PlayerProfile q = p;
    q.pcBindings.push_back({"RearCamera", "F7"});
    CHECK(appliedStatus(q, b, applied, true)[1].state == AppliedState::Missing);
    CHECK(bakkesKnownFree(b, "f7"));
    CHECK(!bakkesKnownFree(b, "F5"));
    CHECK(!bakkesKnownFree(b, "F11"));
}
