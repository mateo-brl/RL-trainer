#include "core/modules/profile/ProfileModule.h"

#include <algorithm>

namespace rlt {

namespace {
constexpr size_t kMaxPendingChanges = 6;
constexpr size_t kMaxKeyLength = 64;
constexpr int kDeviceKeyboard = 1;
constexpr int kDeviceGamepad = 2;

int deviceBit(const PlayerProfile& p) { return p.usingGamepad ? kDeviceGamepad : kDeviceKeyboard; }
// The glue sets the body id only together with a successful hitbox read: a known car means a measured hitbox.
// Before that, hitboxExtent/Offset still hold the PlayerProfile defaults (Octane) and must not be shown.
bool carKnown(const PlayerProfile& p) { return p.carBodyId != 0 || !p.carBodyName.empty(); }

std::string joinComma(const std::vector<std::string>& parts) {
    std::string s;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!s.empty()) s += ", ";
        s += p;
    }
    return s;
}

int countAdvice(const std::vector<pf::SettingCheck>& checks) {
    return static_cast<int>(std::count_if(checks.begin(), checks.end(),
                                          [](const pf::SettingCheck& c) { return c.status == pf::CheckStatus::Advice; }));
}

bool isCandidate(const std::vector<std::string>& candidates, const std::string& key) {
    for (const auto& c : candidates)
        if (sameName(c, key)) return true;
    return false;
}
} // namespace

// ---- Settings ----------------------------------------------------------------------------------------------
void ProfileModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::profile, "profile", "Ma config");
    w.toggle("rlt_pf_enabled", &ProfileConfig::enabled, true, "Activer",
             "Analyse ta config Rocket League (touches, caméra, deadzones, voiture) et affiche des conseils. "
             "La lecture de ta config pour les autres modules reste active.");
    w.choice("rlt_pf_button_style", &ProfileConfig::buttonStyle, 0, {"Auto (noms Xbox)", "Xbox", "PlayStation"},
             "Style des boutons manette",
             "Noms des boutons dans les consignes : A, B, LB, RT... ou Croix, Rond, L1, R2... Le jeu ne dit pas "
             "quelle manette tu utilises : choisis PlayStation si c'est la tienne.");
    w.toggle("rlt_pf_toast", &ProfileConfig::toastOnChange, true, "Notification de détection",
             "Une notification quand ta config est détectée la première fois, puis quand elle change "
             "(manette ou clavier, voiture, touches, réglages manette).");

    auto a = schema.section(&Settings::profile, "profile", "Conseils");
    a.toggle("rlt_pf_advice_camera", &ProfileConfig::cameraAdvice, true, "Conseils caméra",
             "Compare tes réglages caméra aux plages les plus utilisées par les pros. Ce sont des repères, pas des "
             "règles : garde ce qui te va.");
    a.toggle("rlt_pf_advice_deadzone", &ProfileConfig::deadzoneAdvice, true, "Conseils deadzones",
             "Vérifie ta dodge deadzone et ta deadzone manette (flips involontaires, stick qui dérive).");
    a.toggle("rlt_pf_advice_bindings", &ProfileConfig::bindingAdvice, true, "Audit des touches",
             "Vérifie l'air roll directionnel, la caméra balle, le reset et les boutons partagés.");
    a.advanced();
    a.number("rlt_pf_advice_margin", &ProfileConfig::adviceMargin, 0.f, 0.f, 0.5f, "Marge des plages",
             "Élargit chaque plage conseillée de cette fraction de sa largeur avant d'afficher un conseil "
             "(0 = plages strictes).",
             2);

    auto h = schema.section(&Settings::profile, "profile", "Raccourcis automatiques");
    h.toggle("rlt_pf_hk_menu", &ProfileConfig::hotkeyMenu, true, "Raccourci : ouvrir RL Trainer",
             "Propose une touche libre pour ouvrir ou fermer la fenêtre RL Trainer (commande rlt_menu).");
    h.toggle("rlt_pf_hk_stop", &ProfileConfig::hotkeyStop, true, "Raccourci : arrêter l'exercice",
             "Propose une touche libre pour arrêter l'exercice en cours (commande rlt engine stop).");
    h.toggle("rlt_pf_hk_firsttouch", &ProfileConfig::hotkeyFirstTouch, false, "Raccourci : premières touches",
             "Propose une touche libre pour lancer l'exercice de premières touches en mode mix.");
    h.toggle("rlt_pf_hk_kickoff", &ProfileConfig::hotkeyKickoff, false, "Raccourci : kickoff aléatoire",
             "Propose une touche libre pour lancer un kickoff aléatoire.");
    h.advanced();
    h.toggle("rlt_pf_hk_gamepad", &ProfileConfig::hotkeyGamepad, false, "Proposer des boutons manette",
             "Ajoute la croix directionnelle et Back aux touches candidates, seulement s'ils ne servent à rien "
             "dans le jeu (souvent pris par les messages rapides).");

    auto d = schema.section(&Settings::profile, "profile", "Affichage");
    d.toggle("rlt_pf_show_hitbox", &ProfileConfig::showHitbox, false, "Afficher ma hitbox",
             "Dessine la boîte de collision réelle de ta voiture en jeu (lue dans le jeu, pas devinée).");
    d.color("rlt_pf_hitbox_color", &ProfileConfig::hitboxColor, Color{0, 220, 255, 200}, "Couleur de la hitbox",
            "Couleur des traits de la hitbox.");
    d.advanced();
    d.number("rlt_pf_hitbox_width", &ProfileConfig::hitboxWidth, 2.f, 1.f, 5.f, "Épaisseur de la hitbox",
             "Épaisseur des traits de la hitbox, en pixels.", 1);
}

void ProfileModule::onSettingsChanged(const Settings& settings) {
    cfg_ = settings.profile;
    rebuild();
}

// ---- Profile -----------------------------------------------------------------------------------------------
void ProfileModule::onProfileChanged(const PlayerProfile& profile) {
    profile_ = profile;
    ++reads_;
    refreshPending_ = false;
    lastReadQueuedToast_ = false;
    lastReadChanges_.clear();

    if (profile_.valid) {
        const pf::ProfileSignature ns = pf::profileSignature(profile_);
        const pf::HitboxMatch hb = pf::classifyHitbox(profile_);
        std::vector<std::string> changes;   // toasted
        std::vector<std::string> silent;    // shown in the tab only

        if (!announced_) {
            queueDetection();
            announcedDevices_ |= deviceBit(profile_);
        } else {
            if (!(announcedDevices_ & deviceBit(profile_))) {
                changes.push_back(profile_.usingGamepad ? "manette" : "clavier-souris");
                announcedDevices_ |= deviceBit(profile_);
            }
            if (!ns.car.empty() && !sig_.car.empty() && ns.car != sig_.car)
                changes.push_back("voiture : " + pf::carLabel(hb));
            if (!ns.bindings.empty() && !sig_.bindings.empty() && ns.bindings != sig_.bindings)
                changes.push_back("touches modifiées");
            if (!sig_.controller.empty() && ns.controller != sig_.controller) {
                if (profile_.usingGamepad) changes.push_back("réglages manette modifiés");
                else silent.push_back("réglages manette modifiés");
            }
            // The camera may be read live or from the saved settings: never toasted, shown in the tab.
            if (!sig_.camera.empty() && ns.camera != sig_.camera) silent.push_back("caméra modifiée");
        }

        for (const auto& c : changes) addChange(c);
        if (!changes.empty()) lastReadQueuedToast_ = true;
        std::vector<std::string> all = changes;
        all.insert(all.end(), silent.begin(), silent.end());
        lastReadChanges_ = joinComma(all);
        if (!lastReadChanges_.empty()) lastChange_ = lastReadChanges_;

        const pf::ProfileSignature before = sig_;
        if (!ns.car.empty()) sig_.car = ns.car;
        if (!ns.bindings.empty()) sig_.bindings = ns.bindings;
        sig_.controller = ns.controller;
        sig_.camera = ns.camera;
        if (sig_.car != before.car || sig_.bindings != before.bindings || sig_.controller != before.controller ||
            sig_.camera != before.camera)
            saveDirty_ = true;
    }
    rebuild();
}

void ProfileModule::queueDetection() {
    if (!pendingDetection_) waitTicks_ = 0;  // a re-read while waiting for the car does not restart the wait
    pendingDetection_ = true;
    pendingChanges_.clear();
    lastReadQueuedToast_ = true;
}

void ProfileModule::addChange(const std::string& change) {
    if (pendingDetection_) return;  // the detection toast describes the current state anyway
    for (const auto& c : pendingChanges_)
        if (c == change) return;
    if (pendingChanges_.size() >= kMaxPendingChanges) pendingChanges_.erase(pendingChanges_.begin());
    pendingChanges_.push_back(change);
}

std::string ProfileModule::adviceSuffix() const {
    const int n = report_.adviceCount;
    if (n <= 0) return {};
    return " (" + std::to_string(n) + (n > 1 ? " conseils" : " conseil") + " dans Ma config)";
}

void ProfileModule::toast(Actions& out, const std::string& text, ToastKind kind, float seconds) const {
    ToastMessage t;
    t.title = "RL Trainer";
    t.text = text;
    t.kind = kind;
    t.seconds = seconds;
    out.toasts.push_back(std::move(t));
}

void ProfileModule::chat(Actions& out, const std::string& text) const {
    ChatMessage m;
    m.text = text;
    out.chat.push_back(std::move(m));
}

void ProfileModule::flush(Actions& out, bool fromTick, GameMode mode) {
    if (saveDirty_) {
        out.requestSave = true;  // invisible to the player: allowed in every mode
        saveDirty_ = false;
    }
    if (!pendingDetection_ && pendingChanges_.empty()) return;
    // Never pop a notification during an online match: keep it queued for the next event outside it.
    if (mode == GameMode::Online) return;
    const bool allowed = cfg_.enabled && cfg_.toastOnChange && profile_.valid;
    if (pendingDetection_) {
        if (!carKnown(profile_)) {
            // Wait for the car (read when the player is in a game) so the first toast names it.
            if (fromTick) ++waitTicks_;
            if (waitTicks_ < kDetectionWaitTicks) return;
        }
        if (allowed) toast(out, "Config détectée : " + report_.summary + adviceSuffix(), ToastKind::Info);
        pendingDetection_ = false;
        pendingChanges_.clear();
        announced_ = true;
        out.requestSave = true;
        return;
    }
    if (allowed) toast(out, "Config mise à jour : " + joinComma(pendingChanges_), ToastKind::Info);
    pendingChanges_.clear();
    out.requestSave = true;
}

// ---- Report ------------------------------------------------------------------------------------------------
void ProfileModule::rebuild() {
    ProfileReport r;
    r.valid = profile_.valid;
    r.profile = profile_;
    r.style = keyStyleFromIndex(cfg_.buttonStyle);
    r.device = pf::deviceLabel(profile_);
    if (carKnown(profile_)) {
        r.hitbox = pf::classifyHitbox(profile_);
        r.car = pf::carLabel(r.hitbox);
    } else {
        r.hitbox = pf::HitboxMatch{};  // quality None: nothing shown or drawn until the car is read in a game
        r.car = "Voiture pas encore lue (entre en jeu)";
    }
    r.summary = pf::detectionSummary(profile_, r.hitbox);
    r.camera = pf::analyzeCamera(profile_.camera, cfg_.adviceMargin);
    r.controller = pf::analyzeController(profile_, cfg_.adviceMargin);
    r.dodgeExplanation = pf::dodgeDeadzoneExplanation(profile_);
    r.bindings = pf::bindingRows(profile_, r.style);
    r.audit = pf::auditBindings(profile_, r.style);
    r.mechanics = pf::mechanicsReadiness(profile_, r.style);
    r.deviceBindingsReadable = pf::deviceBindingsReadable(profile_);
    r.showCameraAdvice = cfg_.cameraAdvice;
    r.showControllerAdvice = cfg_.deadzoneAdvice;
    r.showBindingAdvice = cfg_.bindingAdvice;
    if (r.valid) {
        if (cfg_.cameraAdvice) r.adviceCount += countAdvice(r.camera);
        if (cfg_.deadzoneAdvice) r.adviceCount += countAdvice(r.controller);
        if (cfg_.bindingAdvice)
            r.adviceCount += static_cast<int>(std::count_if(r.audit.begin(), r.audit.end(), [](const pf::AuditItem& a) {
                return a.status == pf::CheckStatus::Advice;
            }));
    }
    r.reads = reads_;
    r.forcedReads = forcedReads_;
    r.refreshPending = refreshPending_;
    r.lastChange = lastChange_;
    report_ = std::move(r);
    rebuildHotkeys();
}

void ProfileModule::rebuildHotkeys() {
    report_.hotkeyCandidates = pf::hotkeyCandidates(cfg_.hotkeyGamepad);
    report_.hotkeysReady = profile_.valid && (!profile_.pcBindings.empty() || !profile_.gamepadBindings.empty());
    if (report_.hotkeysReady) {
        report_.hotkeys = pf::pickHotkeys(profile_, pf::hotkeyCommands(cfg_), report_.hotkeyCandidates, bakkes_,
                                          applied_, true);
    } else {
        // Without the game bindings any key could collide with an RL action: propose nothing yet.
        report_.hotkeys.clear();
        for (const auto& c : pf::hotkeyCommands(cfg_)) {
            pf::HotkeyProposal h;
            h.id = c.id;
            h.command = c.command;
            h.label = c.label;
            h.state = pf::HotkeyState::NoFreeKey;
            h.note = "En attente de tes touches Rocket League.";
            report_.hotkeys.push_back(std::move(h));
        }
    }
    for (auto& h : report_.hotkeys) h.keyText = keyLabel(h.key, report_.style);
    report_.applied = applied_;
    report_.appliedStatus = pf::appliedStatus(profile_, bakkes_, applied_, report_.hotkeysReady);
    report_.hotkeyConflicts = static_cast<int>(
        std::count_if(report_.appliedStatus.begin(), report_.appliedStatus.end(),
                      [](const pf::AppliedStatus& a) { return a.state == pf::AppliedState::GameConflict; }));
    report_.hotkeysMissing = static_cast<int>(
        std::count_if(report_.appliedStatus.begin(), report_.appliedStatus.end(),
                      [](const pf::AppliedStatus& a) { return a.state == pf::AppliedState::Missing; }));
    report_.bakkesChecked = std::all_of(report_.hotkeyCandidates.begin(), report_.hotkeyCandidates.end(),
                                        [&](const std::string& k) { return bakkes_.count(pf::bindMapKey(k)) != 0; });
    report_.pendingProposals = static_cast<int>(std::count_if(
        report_.hotkeys.begin(), report_.hotkeys.end(),
        [](const pf::HotkeyProposal& h) { return h.state == pf::HotkeyState::Proposed; }));
}

// ---- Callbacks ---------------------------------------------------------------------------------------------
void ProfileModule::onTick(const TickContext& ctx, Actions& out) {
    if (pendingDetection_ || !pendingChanges_.empty() || saveDirty_) flush(out, true, ctx.game.mode);
}

void ProfileModule::onGameEvent(const GameEvent& ev, const TickContext& ctx, Actions& out) {
    if (ev.kind == GameEventKind::ContextChanged || ev.kind == GameEventKind::MapUnloaded) waitTicks_ = 0;
    flush(out, false, ctx.game.mode);
}

void ProfileModule::onCommand(const Command& cmd, const TickContext& ctx, Actions& out) {
    const auto& args = cmd.args;
    if (cmd.name == "refresh") {
        if (!args.empty() && args[0] == "done") {
            ++forcedReads_;
            refreshPending_ = false;
            if (!lastReadQueuedToast_) {
                const std::string what = lastReadChanges_.empty() ? "rien n'a changé (" + report_.summary + ")"
                                                                  : lastReadChanges_;
                toast(out, "Config relue : " + what, ToastKind::Info, 4.f);
            }
        } else {
            refreshPending_ = true;
            chat(out, "Relecture de ta config demandée. Si rien ne s'affiche, ouvre l'onglet Ma config de RL Trainer "
                      "(rlt_menu) ou clique sur Relire ma config.");
        }
        report_.refreshPending = refreshPending_;
        report_.forcedReads = forcedReads_;
    } else if (cmd.name == "resume" || cmd.name == "summary") {
        if (!profile_.valid) {
            chat(out, "Ta config n'est pas encore lue.");
        } else {
            toast(out, "Ta config : " + report_.summary + adviceSuffix(), ToastKind::Info, 6.f);
            chat(out, "Ta config : " + report_.summary + ".");
            int shown = 0;
            const auto tell = [&](const std::string& text) {
                if (shown < 3 && !text.empty()) {
                    chat(out, "Conseil : " + text);
                    ++shown;
                }
            };
            if (cfg_.cameraAdvice)
                for (const auto& c : report_.camera)
                    if (c.status == pf::CheckStatus::Advice) tell(c.advice);
            if (cfg_.deadzoneAdvice)
                for (const auto& c : report_.controller)
                    if (c.status == pf::CheckStatus::Advice) tell(c.advice);
            if (cfg_.bindingAdvice)
                for (const auto& a : report_.audit)
                    if (a.status == pf::CheckStatus::Advice) tell(a.text);
        }
    } else if (cmd.name == "hotkeys") {
        if (report_.hotkeys.empty()) chat(out, "Aucun raccourci demandé (voir les réglages Raccourcis automatiques).");
        for (const auto& h : report_.hotkeys) {
            std::string line = h.label + " : ";
            if (h.state == pf::HotkeyState::NoFreeKey) line += "aucune touche libre";
            else line += h.keyText + (h.state == pf::HotkeyState::Applied ? " (en place)" : " (proposée)");
            chat(out, line);
        }
        if (report_.pendingProposals > 0) chat(out, "Applique-les depuis l'onglet Ma config de RL Trainer.");
        if (report_.hotkeyConflicts > 0)
            chat(out, "Attention : un ancien raccourci RL Trainer est sur une touche que le jeu utilise maintenant. "
                      "Retire-le depuis l'onglet Ma config (Retirer les conflits).");
    } else if (cmd.name == "bmbinds") {
        const std::vector<std::string> candidates = pf::hotkeyCandidates(true);
        for (size_t i = 0; i + 1 < args.size(); i += 2) {
            if (args[i].size() > kMaxKeyLength || !isCandidate(candidates, args[i])) continue;
            bakkes_[pf::bindMapKey(args[i])] = args[i + 1].substr(0, 256);
        }
        rebuildHotkeys();
    } else if (cmd.name == "applied") {
        std::vector<std::string> done;
        for (size_t i = 0; i + 1 < args.size(); i += 2) {
            const std::string& key = args[i];
            const std::string& command = args[i + 1];
            if (key.empty() || key.size() > kMaxKeyLength || !pf::isKnownHotkeyCommand(command)) continue;
            // Same key re-applied, or an older key of the same command whose bind is known to be gone.
            applied_.erase(std::remove_if(applied_.begin(), applied_.end(),
                                          [&](const pf::AppliedHotkey& a) {
                                              return sameName(a.key, key) ||
                                                     (a.command == command && pf::bakkesKnownFree(bakkes_, a.key));
                                          }),
                           applied_.end());
            applied_.push_back({key, command});
            if (applied_.size() > kMaxApplied) applied_.erase(applied_.begin());
            bakkes_[pf::bindMapKey(key)] = command;
            std::string label = command;
            for (const auto& c : pf::allHotkeyCommands())
                if (c.command == command) label = c.label;
            done.push_back(keyLabel(key, keyStyleFromIndex(cfg_.buttonStyle)) + " = " + label);
        }
        rebuildHotkeys();
        if (done.empty()) {
            toast(out, "Aucun raccourci installé : les touches ne sont plus libres.", ToastKind::Warning);
        } else {
            saveDirty_ = true;
            toast(out, "Raccourcis installés : " + joinComma(done) + ".", ToastKind::Ok);
        }
    } else if (cmd.name == "undone") {
        int removed = 0;
        for (const auto& key : args) {
            const auto before = applied_.size();
            applied_.erase(std::remove_if(applied_.begin(), applied_.end(),
                                          [&](const pf::AppliedHotkey& a) { return sameName(a.key, key); }),
                           applied_.end());
            if (applied_.size() != before) {
                ++removed;
                // The bind may have been kept (rebound by the player): unknown until the tab's "bmbinds" check.
                if (key.size() <= kMaxKeyLength) bakkes_.erase(pf::bindMapKey(key));
            }
        }
        rebuildHotkeys();
        if (removed > 0) {
            saveDirty_ = true;
            toast(out, removed > 1 ? "Raccourcis retirés." : "Raccourci retiré.", ToastKind::Info, 3.f);
        }
    } else if (cmd.name == "forget") {
        applied_.clear();
        saveDirty_ = true;
        rebuildHotkeys();
        chat(out, "Liste des raccourcis à annuler vidée (tes binds BakkesMod ne changent pas).");
    } else if (cmd.name == "resettoast") {
        announced_ = false;
        announcedDevices_ = 0;
        sig_ = {};
        saveDirty_ = true;
        if (profile_.valid) {
            sig_ = pf::profileSignature(profile_);
            announcedDevices_ = deviceBit(profile_);
            queueDetection();
        }
    } else {
        chat(out, "Commande inconnue. Essaie : refresh, resume, hotkeys, forget, resettoast.");
    }
    flush(out, false, ctx.game.mode);
}

void ProfileModule::draw(const ViewContext& view, const DrawContext& ctx, DrawList& out) {
    if (!cfg_.enabled || !cfg_.showHitbox || !view.valid || !view.carValid) return;
    const GameMode mode = ctx.game.mode;
    if (mode == GameMode::Replay || mode == GameMode::Menu || mode == GameMode::Online) return;
    const pf::HitboxMatch& hb = report_.hitbox;
    if (hb.quality == pf::MatchQuality::None) return;
    const Vec3 center = view.carVisualPos + view.carVisualBasis.toWorld(hb.offset);
    out.box3D(center, view.carVisualBasis, hb.size * 0.5f, cfg_.hitboxColor, cfg_.hitboxWidth);
}

// ---- Persistence -------------------------------------------------------------------------------------------
void ProfileModule::save(Store& store) const {
    store.setString("profile.sig.car", sig_.car);
    store.setString("profile.sig.bindings", sig_.bindings);
    store.setString("profile.sig.controller", sig_.controller);
    store.setString("profile.sig.camera", sig_.camera);
    store.setInt("profile.sig.devices", announcedDevices_);
    store.setBool("profile.announced", announced_);
    std::vector<std::string> list;
    for (const auto& a : applied_) list.push_back(a.key + "|" + a.command);
    store.setList("profile.hotkeys", list);
}

void ProfileModule::load(const Store& store) {
    const auto clip = [](std::string s) {
        if (s.size() > 256) s.resize(256);
        return s;
    };
    sig_.car = clip(store.getString("profile.sig.car"));
    sig_.bindings = clip(store.getString("profile.sig.bindings"));
    sig_.controller = clip(store.getString("profile.sig.controller"));
    sig_.camera = clip(store.getString("profile.sig.camera"));
    announcedDevices_ = static_cast<int>(store.getInt("profile.sig.devices", 0)) & (kDeviceKeyboard | kDeviceGamepad);
    announced_ = store.getBool("profile.announced", false);
    applied_.clear();
    for (const auto& entry : store.getList("profile.hotkeys")) {
        const auto bar = entry.find('|');
        if (bar == std::string::npos || bar == 0 || bar > kMaxKeyLength) continue;
        pf::AppliedHotkey a{entry.substr(0, bar), entry.substr(bar + 1)};
        if (!pf::isKnownHotkeyCommand(a.command)) continue;
        bool dup = false;
        for (const auto& b : applied_) dup = dup || sameName(b.key, a.key);
        if (dup) continue;
        applied_.push_back(std::move(a));
        if (applied_.size() >= kMaxApplied) break;
    }
    rebuild();
}

} // namespace rlt
