// Tests of ProfileModule: settings schema, toasts once per change, commands, hotkey state, persistence,
// drawing and activity gating.
#include <algorithm>
#include <cmath>

#include "tests/TestFramework.h"
#include "tests/profile/ProfileTestUtil.h"

#include "core/modules/profile/ProfileModule.h"

using namespace rlt;
using namespace rlt_pf_test;

namespace {
GameEvent ev(GameEventKind k) {
    GameEvent e;
    e.kind = k;
    return e;
}

// Sends a profile the way the engine does: onProfileChanged then the ProfileChanged event.
Actions sendProfile(ProfileModule& m, Harness& h, const PlayerProfile& p) {
    h.profile = p;
    m.onProfileChanged(p);
    Actions out;
    m.onGameEvent(ev(GameEventKind::ProfileChanged), h.ctx(), out);
    return out;
}

Actions command(ProfileModule& m, Harness& h, const std::string& name, std::vector<std::string> args = {}) {
    Command c;
    c.module = "profile";
    c.name = name;
    c.args = std::move(args);
    Actions out;
    m.onCommand(c, h.ctx(), out);
    return out;
}

ProfileModule makeModule(Harness& h) {
    ProfileModule m;
    m.onSettingsChanged(h.settings);
    return m;
}
} // namespace

TEST(profile_identity) {
    ProfileModule m;
    CHECK_EQ(std::string(m.id()), "profile");
    CHECK_EQ(std::string(m.title()), "Ma config");
}

TEST(profile_schema_defaults_and_texts) {
    SettingsSchema schema;
    ProfileModule::describeSettings(schema);
    const auto defs = schema.forModule("profile");
    CHECK(defs.size() >= 14);
    Settings s;
    schema.applyDefaults(s);
    const ProfileConfig def;
    CHECK(s.profile.enabled == def.enabled);
    CHECK_EQ(s.profile.buttonStyle, def.buttonStyle);
    CHECK(s.profile.hotkeyMenu && s.profile.hotkeyStop && !s.profile.hotkeyKickoff && !s.profile.showHitbox);
    CHECK(s.profile.hitboxColor == def.hitboxColor);
    CHECK_NEAR(s.profile.hitboxWidth, def.hitboxWidth, 1e-6f);
    for (const SettingDef* d : defs) {
        CHECK_MSG(d->cvar.rfind("rlt_pf_", 0) == 0, d->cvar);
        CHECK(!d->label.empty());
        CHECK(!d->tooltip.empty());
        CHECK(!d->section.empty());
        CHECK(!hasEmDash(d->label) && !hasEmDash(d->tooltip));
        for (const auto& c : d->choices) CHECK(!hasEmDash(c));
        // Every default parses and reads back identically.
        Settings t;
        CHECK_MSG(d->apply(t, d->defaultValue), d->cvar);
        CHECK_MSG(d->read(t) == d->defaultValue, d->cvar);
    }
    const auto sections = schema.sections("profile");
    CHECK(sections.size() >= 4);
    CHECK_EQ(sections.front(), "Ma config");
    // Range clamping.
    CHECK(schema.apply(s, "rlt_pf_button_style", "9"));
    CHECK_EQ(s.profile.buttonStyle, 2);
    CHECK(schema.apply(s, "rlt_pf_advice_margin", "3"));
    CHECK_NEAR(s.profile.adviceMargin, 0.5f, 1e-6f);
    CHECK(!schema.apply(s, "rlt_pf_hitbox_width", "abc"));
}

TEST(profile_report_reflects_profile) {
    Harness h;
    ProfileModule m = makeModule(h);
    CHECK(!m.report().valid);
    sendProfile(m, h, proGamepadProfile());
    const ProfileReport& r = m.report();
    CHECK(r.valid);
    CHECK_EQ(r.device, "Manette");
    CHECK_EQ(r.car, "Octane");
    CHECK(r.hitbox.family == pf::HitboxFamily::Octane);
    CHECK_EQ(r.camera.size(), static_cast<size_t>(8));
    CHECK(!r.controller.empty());
    CHECK_EQ(r.bindings.size(), static_cast<size_t>(GameAction::Count));
    CHECK(!r.audit.empty());
    CHECK(!r.mechanics.empty());
    CHECK_EQ(r.reads, 1);
    // PlayStation style changes the labels of the report.
    h.set("rlt_pf_button_style", "2");
    m.onSettingsChanged(h.settings);
    CHECK_EQ(m.report().bindings[static_cast<int>(GameAction::Jump)].keys, "Croix");
}

TEST(profile_advice_count_follows_settings) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile p = gamepadProfile();  // no directional air roll + shared air roll: 2 binding advices
    p.camera.distance = 200.f;           // 1 camera advice
    p.dodgeDeadzone = 0.2f;              // 1 deadzone advice
    sendProfile(m, h, p);
    CHECK_EQ(m.report().adviceCount, 4);
    h.set("rlt_pf_advice_camera", "0");
    m.onSettingsChanged(h.settings);
    CHECK_EQ(m.report().adviceCount, 3);
    CHECK(!m.report().showCameraAdvice);
    h.set("rlt_pf_advice_bindings", "0");
    h.set("rlt_pf_advice_deadzone", "0");
    m.onSettingsChanged(h.settings);
    CHECK_EQ(m.report().adviceCount, 0);
}

TEST(profile_toast_once_on_detection) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile p = proGamepadProfile();
    Actions a = sendProfile(m, h, p);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) {
        CHECK(containsText(a.toasts[0].text, "Config détectée : manette, Octane, caméra balle"));
        CHECK(!hasEmDash(a.toasts[0].text));
    }
    CHECK(a.requestSave);
    // Same profile again: nothing.
    CHECK(sendProfile(m, h, p).toasts.empty());
    // Ball cam toggled (live state): nothing.
    p.camera.ballCam = false;
    CHECK(sendProfile(m, h, p).toasts.empty());
    // Ticks with nothing pending: no output.
    Actions t;
    m.onTick(h.ctx(), t);
    CHECK(t.empty());
}

TEST(profile_toast_on_real_changes_only) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile p = proGamepadProfile();
    sendProfile(m, h, p);

    // Device switch: announced once per device, switching back is silent.
    PlayerProfile kb = p;
    kb.usingGamepad = false;
    Actions a = sendProfile(m, h, kb);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "Config mise à jour : clavier-souris"));
    CHECK(sendProfile(m, h, p).toasts.empty());
    CHECK(sendProfile(m, h, kb).toasts.empty());

    // Car change.
    PlayerProfile dom = p;
    const pf::HitboxPreset* d = pf::findPreset(pf::HitboxFamily::Dominus);
    CHECK(d != nullptr);
    if (d) {
        dom.hitboxExtent = d->size;
        dom.hitboxOffset = d->offset;
    }
    dom.carBodyId = 403;
    dom.carBodyName = "Dominus";
    a = sendProfile(m, h, dom);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "voiture : Dominus"));

    // Bindings change.
    PlayerProfile b = dom;
    b.gamepadBindings.push_back({"RollRight", "XboxTypeS_LeftThumbStick"});
    a = sendProfile(m, h, b);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "touches modifiées"));

    // Deadzone change on a gamepad.
    PlayerProfile dz = b;
    dz.dodgeDeadzone = 0.65f;
    a = sendProfile(m, h, dz);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));

    // Camera change: no toast, but the tab shows it.
    PlayerProfile cam = dz;
    cam.camera.distance = 300.f;
    a = sendProfile(m, h, cam);
    CHECK(a.toasts.empty());
    CHECK(containsText(m.report().lastChange, "caméra modifiée"));

    // Car temporarily unknown (menu, replay): silent, and coming back to the same car is silent too.
    PlayerProfile menu = cam;
    menu.carBodyId = 0;
    menu.carBodyName.clear();
    CHECK(sendProfile(m, h, menu).toasts.empty());
    CHECK(sendProfile(m, h, cam).toasts.empty());
}

TEST(profile_detection_waits_for_the_car) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile menu = gamepadProfile();
    menu.carBodyId = 0;
    menu.carBodyName.clear();
    Actions a = sendProfile(m, h, menu);
    CHECK(a.toasts.empty());
    CHECK(m.toastPending());
    // Context change (menu -> freeplay) does not flush either.
    Actions c;
    m.onGameEvent(ev(GameEventKind::ContextChanged), h.ctx(), c);
    CHECK(c.toasts.empty());
    // The car arrives with the next read: the toast names it.
    PlayerProfile game = gamepadProfile();
    a = sendProfile(m, h, game);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "Octane"));
    CHECK(!m.toastPending());
}

TEST(profile_detection_timeout_without_car) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile menu = gamepadProfile();
    menu.carBodyId = 0;
    menu.carBodyName.clear();
    sendProfile(m, h, menu);
    int toasts = 0;
    for (int i = 0; i < ProfileModule::kDetectionWaitTicks - 1; ++i) {
        Actions t;
        m.onTick(h.ctx(), t);
        toasts += static_cast<int>(t.toasts.size());
    }
    CHECK_EQ(toasts, 0);
    Actions last;
    m.onTick(h.ctx(), last);
    CHECK_EQ(last.toasts.size(), static_cast<size_t>(1));
    if (!last.toasts.empty()) CHECK(containsText(last.toasts[0].text, "Config détectée : manette, caméra balle"));
    Actions after;
    m.onTick(h.ctx(), after);
    CHECK(after.toasts.empty());
}

TEST(profile_toast_setting_and_invalid_profile) {
    Harness h;
    h.set("rlt_pf_toast", "0");
    ProfileModule m = makeModule(h);
    Actions a = sendProfile(m, h, gamepadProfile());
    CHECK(a.toasts.empty());
    CHECK(!m.toastPending());  // dropped, not delayed
    // Re-enabling does not replay an old detection.
    h.set("rlt_pf_toast", "1");
    m.onSettingsChanged(h.settings);
    CHECK(sendProfile(m, h, gamepadProfile()).toasts.empty());

    Harness h2;
    ProfileModule n = makeModule(h2);
    PlayerProfile invalid;  // valid = false
    CHECK(sendProfile(n, h2, invalid).toasts.empty());
    CHECK(!n.report().valid);
    CHECK(!n.toastPending());
}

TEST(profile_save_load_roundtrip) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, proGamepadProfile());
    command(m, h, "applied", {"F5", "rlt_menu", "F7", "rlt engine stop"});
    Store st;
    m.save(st);
    for (const auto& kv : st.raw()) CHECK_MSG(kv.first.rfind("profile.", 0) == 0, kv.first);

    ProfileModule n = makeModule(h);
    n.load(st);
    CHECK_EQ(n.report().applied.size(), static_cast<size_t>(2));
    // Same config after a restart: no new toast.
    CHECK(sendProfile(n, h, proGamepadProfile()).toasts.empty());
    // Different car after a restart: change toast.
    PlayerProfile other = proGamepadProfile();
    const pf::HitboxPreset* b = pf::findPreset(pf::HitboxFamily::Breakout);
    if (b) {
        other.hitboxExtent = b->size;
        other.hitboxOffset = b->offset;
    }
    other.carBodyId = 22;
    other.carBodyName = "Breakout";
    CHECK_EQ(sendProfile(n, h, other).toasts.size(), static_cast<size_t>(1));

    // Corrupted / hostile entries are filtered and the list is bounded.
    Store bad;
    std::vector<std::string> list = {"F5|rlt_menu", "nobar", "|rlt_menu", "F6|exec evil.cfg", "F5|rlt engine stop"};
    for (int i = 0; i < 40; ++i) list.push_back("NumPad" + std::to_string(i) + "|rlt_menu");
    bad.setList("profile.hotkeys", list);
    bad.setInt("profile.sig.devices", 255);
    ProfileModule z = makeModule(h);
    z.load(bad);
    CHECK(z.report().applied.size() <= ProfileModule::kMaxApplied);
    CHECK(!z.report().applied.empty());
    CHECK_EQ(z.report().applied[0].key, "F5");
    for (const auto& a : z.report().applied) CHECK(pf::isKnownHotkeyCommand(a.command));
    // Empty store: fresh state.
    ProfileModule e = makeModule(h);
    e.load(Store{});
    CHECK(e.report().applied.empty());
    CHECK_EQ(sendProfile(e, h, gamepadProfile()).toasts.size(), static_cast<size_t>(1));
}

TEST(profile_hotkey_commands_flow) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, keyboardProfile());
    const ProfileReport& r0 = m.report();
    CHECK(!r0.bakkesChecked);
    CHECK_EQ(r0.hotkeys.size(), static_cast<size_t>(2));
    CHECK_EQ(r0.pendingProposals, 2);

    // The tab reports BakkesMod binds: F5 is used by BakkesMod.
    std::vector<std::string> args;
    for (const auto& k : r0.hotkeyCandidates) {
        args.push_back(k);
        args.push_back(k == "F5" ? "togglemenu something" : "");
    }
    args.push_back("SpaceBar");  // not a candidate: ignored
    args.push_back("");
    command(m, h, "bmbinds", args);
    const ProfileReport& r1 = m.report();
    CHECK(r1.bakkesChecked);
    CHECK_EQ(r1.hotkeys[0].key, "F7");
    CHECK(r1.hotkeys[0].verified);

    // The tab applied them.
    Actions a = command(m, h, "applied", {"F7", "rlt_menu", "F8", "rlt engine stop", "F9", "exec bad"});
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) {
        CHECK(a.toasts[0].kind == ToastKind::Ok);
        CHECK(containsText(a.toasts[0].text, "F7 = Ouvrir RL Trainer"));
    }
    CHECK(a.requestSave);
    const ProfileReport& r2 = m.report();
    CHECK_EQ(r2.applied.size(), static_cast<size_t>(2));
    CHECK(r2.hotkeys[0].state == pf::HotkeyState::Applied);
    CHECK_EQ(r2.pendingProposals, 0);

    // Undo.
    a = command(m, h, "undone", {"F7", "F8", "F11"});
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    CHECK(m.report().applied.empty());
    CHECK_EQ(m.report().pendingProposals, 2);

    // Nothing applied at all: warning.
    a = command(m, h, "applied", {"F7", "not a command"});
    CHECK(!a.toasts.empty() && a.toasts[0].kind == ToastKind::Warning);

    // Forget.
    command(m, h, "applied", {"F7", "rlt_menu"});
    a = command(m, h, "forget");
    CHECK(m.report().applied.empty());
    CHECK(!a.chat.empty());

    // Hotkeys listing.
    a = command(m, h, "hotkeys");
    CHECK(a.chat.size() >= 2);
}

TEST(profile_refresh_and_summary_commands) {
    Harness h;
    ProfileModule m = makeModule(h);
    Actions a = command(m, h, "resume");
    CHECK(!a.chat.empty());  // "pas encore lue"
    CHECK(a.toasts.empty());

    sendProfile(m, h, gamepadProfile());
    a = command(m, h, "refresh");
    CHECK(m.report().refreshPending);
    CHECK(!a.chat.empty());
    // A read happens (forced from the tab), nothing changed, then "refresh done".
    sendProfile(m, h, gamepadProfile());
    CHECK(!m.report().refreshPending);
    a = command(m, h, "refresh", {"done"});
    CHECK_EQ(m.report().forcedReads, 1);
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "rien n'a changé"));
    // A read with a change already toasted: no second toast on "done".
    PlayerProfile kb = keyboardProfile();
    Actions changed = sendProfile(m, h, kb);
    CHECK_EQ(changed.toasts.size(), static_cast<size_t>(1));
    a = command(m, h, "refresh", {"done"});
    CHECK(a.toasts.empty());

    a = command(m, h, "resume");
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    CHECK(a.chat.size() >= 2);  // summary + at least one advice (default bindings)
    for (const auto& c : a.chat) CHECK(!hasEmDash(c.text));

    a = command(m, h, "nope");
    CHECK(!a.chat.empty());

    // resettoast: the detection toast comes back.
    a = command(m, h, "resettoast");
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    if (!a.toasts.empty()) CHECK(containsText(a.toasts[0].text, "Config détectée"));
}

TEST(profile_draw_hitbox) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, gamepadProfile());
    ViewContext view;
    view.valid = true;
    view.carValid = true;
    view.carVisualPos = {100.f, 200.f, 17.f};
    DrawContext dc{h.settings, h.game, h.profile, h.snap, true, false, 1.f};

    DrawList off;
    m.draw(view, dc, off);
    CHECK(off.empty());  // disabled by default

    h.set("rlt_pf_show_hitbox", "1");
    m.onSettingsChanged(h.settings);
    DrawList on;
    m.draw(view, dc, on);
    CHECK_EQ(on.lines3D.size(), static_cast<size_t>(12));
    // The box is centred on origin + offset and has the Octane length.
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& l : on.lines3D) {
        minX = std::min({minX, l.a.x, l.b.x});
        maxX = std::max({maxX, l.a.x, l.b.x});
    }
    CHECK_NEAR(maxX - minX, 118.0074f, 0.01f);
    CHECK_NEAR((maxX + minX) * 0.5f, 100.f + 13.87566f, 0.01f);

    // No car, replay, or module disabled: nothing.
    ViewContext nocar = view;
    nocar.carValid = false;
    DrawList d1;
    m.draw(nocar, dc, d1);
    CHECK(d1.empty());
    GameContext replay = h.game;
    replay.mode = GameMode::Replay;
    DrawContext rc{h.settings, replay, h.profile, h.snap, false, false, 1.f};
    DrawList d2;
    m.draw(view, rc, d2);
    CHECK(d2.empty());
    h.set("rlt_pf_enabled", "0");
    m.onSettingsChanged(h.settings);
    DrawList d3;
    m.draw(view, dc, d3);
    CHECK(d3.empty());
}

TEST(profile_never_takes_world_control) {
    Harness h;
    FakeHost host;
    ProfileModule m = makeModule(h);
    m.attach(&host);
    std::vector<Actions> outs;
    outs.push_back(sendProfile(m, h, gamepadProfile()));
    for (int i = 0; i < 700; ++i) {
        Actions a;
        h.snap.frame = i;
        m.onTick(h.ctx(), a);
        outs.push_back(std::move(a));
    }
    for (const char* c : {"refresh", "resume", "hotkeys", "forget", "resettoast"}) outs.push_back(command(m, h, c));
    Actions e;
    m.onGameEvent(ev(GameEventKind::ContextChanged), h.ctx(), e);
    m.onGameEvent(ev(GameEventKind::Paused), h.ctx(), e);
    m.onGameEvent(ev(GameEventKind::MapUnloaded), h.ctx(), e);
    outs.push_back(std::move(e));
    m.onActivityStopped();
    for (const auto& a : outs) CHECK(!hasWorldControl(a));
    CHECK_EQ(host.requests, 0);
    CHECK_EQ(host.releases, 0);
    // Also in a context without world control (online): still no world actions, no crash.
    h.game.mode = GameMode::Online;
    h.game.worldControlAllowed = false;
    Actions on;
    m.onTick(h.ctx(), on);
    CHECK(!hasWorldControl(on));
}

TEST(profile_texts_have_no_em_dash) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile p = gamepadProfile();
    p.camera = {60.f, 200.f, -20.f, 400.f, 1.f, 10.f, 2.f, false};
    p.dodgeDeadzone = 0.1f;
    p.controllerDeadzone = 0.3f;
    sendProfile(m, h, p);
    const ProfileReport& r = m.report();
    for (const auto& c : r.camera) CHECK(!hasEmDash(c.advice) && !hasEmDash(c.label));
    for (const auto& c : r.controller) CHECK(!hasEmDash(c.advice) && !hasEmDash(c.label));
    for (const auto& a : r.audit) CHECK(!hasEmDash(a.text) && !hasEmDash(a.title));
    for (const auto& k : r.hotkeys) CHECK(!hasEmDash(k.note) && !hasEmDash(k.label));
    CHECK(!hasEmDash(r.dodgeExplanation) && !hasEmDash(r.summary) && !hasEmDash(r.car));
    int advice = 0;
    for (const auto& c : r.camera) advice += c.status == pf::CheckStatus::Advice ? 1 : 0;
    CHECK_EQ(advice, 8);  // every camera value outside the usual ranges, ball cam off
}

TEST(profile_no_hotkeys_before_bindings_are_known) {
    Harness h;
    ProfileModule m = makeModule(h);
    CHECK(!m.report().hotkeysReady);
    CHECK_EQ(m.report().pendingProposals, 0);
    for (const auto& k : m.report().hotkeys) {
        CHECK(k.key.empty());
        CHECK(k.state == pf::HotkeyState::NoFreeKey);
    }
    PlayerProfile noBinds = gamepadProfile();
    noBinds.pcBindings.clear();
    noBinds.gamepadBindings.clear();
    sendProfile(m, h, noBinds);
    CHECK(!m.report().hotkeysReady);
    CHECK_EQ(m.report().pendingProposals, 0);
    sendProfile(m, h, gamepadProfile());
    CHECK(m.report().hotkeysReady);
    CHECK_EQ(m.report().pendingProposals, 2);
}

TEST(profile_rereads_do_not_restart_the_car_wait) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile menu = gamepadProfile();
    menu.carBodyId = 0;
    menu.carBodyName.clear();
    sendProfile(m, h, menu);
    int toasts = 0;
    for (int i = 0; i < ProfileModule::kDetectionWaitTicks; ++i) {
        if (i % 100 == 50) {
            menu.camera.ballCam = !menu.camera.ballCam;  // re-reads while waiting (ball cam toggles)
            toasts += static_cast<int>(sendProfile(m, h, menu).toasts.size());
        }
        Actions t;
        m.onTick(h.ctx(), t);
        toasts += static_cast<int>(t.toasts.size());
    }
    CHECK_EQ(toasts, 1);
    CHECK(!m.toastPending());
}

TEST(profile_last_change_is_sticky) {
    Harness h;
    ProfileModule m = makeModule(h);
    PlayerProfile p = gamepadProfile();
    sendProfile(m, h, p);
    CHECK(m.report().lastChange.empty());
    p.camera.fov = 100.f;
    sendProfile(m, h, p);
    CHECK(containsText(m.report().lastChange, "caméra modifiée"));
    sendProfile(m, h, p);  // nothing new
    CHECK(containsText(m.report().lastChange, "caméra modifiée"));
    // "refresh done" after an unchanged read says nothing changed, even with a sticky last change.
    Command c;
    c.module = "profile";
    c.name = "refresh";
    c.args = {"done"};
    Actions a;
    m.onCommand(c, h.ctx(), a);
    CHECK(!a.toasts.empty() && containsText(a.toasts[0].text, "rien n'a changé"));
}

namespace {
std::vector<std::string> bindArgs(const ProfileReport& r, const std::vector<std::pair<std::string, std::string>>& bound) {
    std::vector<std::string> args;
    for (const auto& k : r.hotkeyCandidates) {
        args.push_back(k);
        std::string v;
        for (const auto& b : bound)
            if (b.first == k) v = b.second;
        args.push_back(v);
    }
    return args;
}
} // namespace

// Review fix: before the car is read (menu, first seconds after a spawn), the PlayerProfile default hitbox
// (Octane values) must not be reported as an exact in-game read, nor drawn.
TEST(profile_unread_car_has_no_hitbox) {
    Harness h;
    h.set("rlt_pf_show_hitbox", "1");
    ProfileModule m = makeModule(h);
    PlayerProfile menu = gamepadProfile();
    menu.carBodyId = 0;
    menu.carBodyName.clear();
    sendProfile(m, h, menu);
    const ProfileReport& r = m.report();
    CHECK(r.valid);
    CHECK(r.hitbox.quality == pf::MatchQuality::None);
    CHECK(r.hitbox.family == pf::HitboxFamily::Unknown);
    CHECK_EQ(r.car, "Voiture pas encore lue (entre en jeu)");
    CHECK(!containsText(r.summary, "Octane"));

    ViewContext view;
    view.valid = true;
    view.carValid = true;
    view.carVisualPos = {0.f, 0.f, 17.f};
    DrawContext dc{h.settings, h.game, h.profile, h.snap, true, false, 1.f};
    DrawList none;
    m.draw(view, dc, none);
    CHECK(none.empty());

    // The car is read: the measured hitbox shows and is drawn.
    PlayerProfile game = gamepadProfile();
    const pf::HitboxPreset* d = pf::findPreset(pf::HitboxFamily::Dominus);
    CHECK(d != nullptr);
    if (d) {
        game.hitboxExtent = d->size;
        game.hitboxOffset = d->offset;
    }
    game.carBodyId = 403;
    game.carBodyName = "Dominus";
    sendProfile(m, h, game);
    CHECK(m.report().hitbox.quality == pf::MatchQuality::Exact);
    CHECK(m.report().hitbox.family == pf::HitboxFamily::Dominus);
    DrawList drawn;
    m.draw(view, dc, drawn);
    CHECK_EQ(drawn.lines3D.size(), static_cast<size_t>(12));
}

// Review fix: an applied hotkey that BakkesMod reports free is proposed again (same key) and can be re-applied.
TEST(profile_lost_bind_can_be_reapplied) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, keyboardProfile());
    command(m, h, "applied", {"F5", "rlt_menu", "F7", "rlt engine stop"});
    command(m, h, "bmbinds", bindArgs(m.report(), {{"F5", "rlt_menu"}, {"F7", "rlt engine stop"}}));
    CHECK_EQ(m.report().pendingProposals, 0);
    CHECK(m.report().hotkeys[0].state == pf::HotkeyState::Applied);
    CHECK(m.report().hotkeys[0].verified);

    // The player removed the F5 bind in BakkesMod.
    command(m, h, "bmbinds", bindArgs(m.report(), {{"F7", "rlt engine stop"}}));
    const ProfileReport& r = m.report();
    CHECK_EQ(r.hotkeys[0].key, "F5");
    CHECK(r.hotkeys[0].state == pf::HotkeyState::Proposed);
    CHECK(r.hotkeys[0].verified);
    CHECK(r.hotkeys[0].previousMissing);
    CHECK_EQ(r.pendingProposals, 1);
    CHECK_EQ(r.hotkeysMissing, 1);
    CHECK_EQ(r.appliedStatus.size(), static_cast<size_t>(2));
    CHECK(r.appliedStatus[0].state == pf::AppliedState::Missing);
    CHECK(r.appliedStatus[1].state == pf::AppliedState::Active);
    CHECK(r.hotkeys[1].state == pf::HotkeyState::Applied);

    // Re-applied from the tab: back in place, no duplicate in the undo list.
    command(m, h, "applied", {"F5", "rlt_menu"});
    CHECK(m.report().hotkeys[0].state == pf::HotkeyState::Applied);
    CHECK_EQ(m.report().pendingProposals, 0);
    CHECK_EQ(m.report().hotkeysMissing, 0);
    CHECK_EQ(m.report().applied.size(), static_cast<size_t>(2));

    // Lost again while the game now uses F5: the new key replaces the stale entry once applied.
    PlayerProfile kb = keyboardProfile();
    kb.pcBindings.push_back({"RearCamera", "F5"});
    sendProfile(m, h, kb);
    command(m, h, "bmbinds", bindArgs(m.report(), {{"F7", "rlt engine stop"}}));
    CHECK_EQ(m.report().hotkeys[0].key, "F8");
    CHECK(m.report().hotkeys[0].state == pf::HotkeyState::Proposed);
    command(m, h, "applied", {"F8", "rlt_menu"});
    CHECK_EQ(m.report().applied.size(), static_cast<size_t>(2));
    for (const auto& a : m.report().applied) CHECK(a.key != "F5");
}

// Review fix: "undone" must not claim the keys are free (the player may have rebound one); the real binds
// come with the tab's next "bmbinds".
TEST(profile_undo_forgets_bind_state) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, keyboardProfile());
    command(m, h, "bmbinds", bindArgs(m.report(), {}));
    CHECK(m.report().bakkesChecked);
    command(m, h, "applied", {"F5", "rlt_menu"});
    // The player rebinds F5 to another command, then clicks "Annuler": the tab refuses to unbind F5.
    Actions a = command(m, h, "undone", {"F5"});
    CHECK_EQ(a.toasts.size(), static_cast<size_t>(1));
    CHECK(m.report().applied.empty());
    CHECK(!m.report().bakkesChecked);  // F5 unknown again
    CHECK_EQ(m.report().hotkeys[0].key, "F5");
    CHECK(!m.report().hotkeys[0].verified);
    CHECK(!containsText(m.report().hotkeys[0].note, "Libre dans le jeu et dans BakkesMod"));
    // The tab's post-undo check: F5 still bound by the player. It is not proposed any more.
    command(m, h, "bmbinds", bindArgs(m.report(), {{"F5", "say gg"}}));
    CHECK(m.report().bakkesChecked);
    CHECK_EQ(m.report().hotkeys[0].key, "F7");
    CHECK(m.report().hotkeys[0].verified);
}

// Review fix: no detection or change toast during an online match; they wait for the next event outside it.
TEST(profile_no_toast_during_online_match) {
    Harness h;
    h.game.mode = GameMode::Online;
    h.game.worldControlAllowed = false;
    ProfileModule m = makeModule(h);
    Actions a = sendProfile(m, h, gamepadProfile());
    CHECK(a.toasts.empty());
    CHECK(m.toastPending());
    Actions c = command(m, h, "resettoast");
    CHECK(c.toasts.empty());
    CHECK(m.toastPending());
    // Back to the menu after the match.
    h.game.mode = GameMode::Menu;
    Actions back;
    m.onGameEvent(ev(GameEventKind::ContextChanged), h.ctx(), back);
    CHECK_EQ(back.toasts.size(), static_cast<size_t>(1));
    if (!back.toasts.empty()) CHECK(containsText(back.toasts[0].text, "Config détectée"));
    CHECK(!m.toastPending());

    // Controller switch during the next online match: held, then shown once outside.
    h.game.mode = GameMode::Online;
    PlayerProfile kb = gamepadProfile();
    kb.usingGamepad = false;
    CHECK(sendProfile(m, h, kb).toasts.empty());
    CHECK(m.toastPending());
    h.game.mode = GameMode::Freeplay;
    Actions t;
    m.onTick(h.ctx(), t);
    CHECK_EQ(t.toasts.size(), static_cast<size_t>(1));
    if (!t.toasts.empty()) CHECK(containsText(t.toasts[0].text, "clavier-souris"));
}

// Spec gap: an applied key later bound in Rocket League is reported as a conflict (tab warning + chat).
TEST(profile_hotkey_game_conflict_reported) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, keyboardProfile());
    command(m, h, "applied", {"F5", "rlt_menu"});
    command(m, h, "bmbinds", bindArgs(m.report(), {{"F5", "rlt_menu"}}));
    CHECK_EQ(m.report().hotkeyConflicts, 0);
    PlayerProfile kb = keyboardProfile();
    kb.pcBindings.push_back({"Jump", "F5"});
    sendProfile(m, h, kb);
    const ProfileReport& r = m.report();
    CHECK_EQ(r.hotkeyConflicts, 1);
    CHECK(r.appliedStatus[0].state == pf::AppliedState::GameConflict);
    CHECK(r.hotkeys[0].key != "F5");
    CHECK(r.hotkeys[0].previousConflict);
    Actions a = command(m, h, "hotkeys");
    bool warned = false;
    for (const auto& c : a.chat) warned = warned || containsText(c.text, "Retirer les conflits");
    CHECK(warned);
    // Removed through "Retirer les conflits": the entry leaves the undo list, no conflict left.
    command(m, h, "undone", {"F5"});
    CHECK_EQ(m.report().hotkeyConflicts, 0);
    CHECK(m.report().applied.empty());
}

TEST(profile_console_refresh_points_to_the_tab) {
    Harness h;
    ProfileModule m = makeModule(h);
    sendProfile(m, h, gamepadProfile());
    Actions a = command(m, h, "refresh");
    CHECK_EQ(a.chat.size(), static_cast<size_t>(1));
    if (!a.chat.empty()) {
        CHECK(containsText(a.chat[0].text, "Ma config"));
        CHECK(!hasEmDash(a.chat[0].text));
    }
}
