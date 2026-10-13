// "Ma config" tab: what RL Trainer detected about the player's own Rocket League configuration, advice,
// bindings, and the automatic hotkeys (applied only on click). Render thread: the module state is copied under
// the engine lock; every SDK access goes through ctx.runOnGame (game thread) and reports back with commands.
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/profile/ProfileModule.h"
#include "imgui.h"
#include "plugin/services/ProfileService.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

constexpr Color kOk{90, 220, 120, 255};
constexpr Color kAdvice{255, 170, 60, 255};
constexpr Color kInfo{150, 185, 255, 255};
constexpr float kBakkesRecheckSeconds = 10.f;
constexpr float kBakkesRetrySeconds = 2.f;

const char* statusLabel(pf::CheckStatus s) {
    switch (s) {
    case pf::CheckStatus::Ok: return "OK";
    case pf::CheckStatus::Advice: return "Conseil";
    default: return "Info";
    }
}

Color statusColor(pf::CheckStatus s) {
    switch (s) {
    case pf::CheckStatus::Ok: return kOk;
    case pf::CheckStatus::Advice: return kAdvice;
    default: return kInfo;
    }
}

void hoverText(const std::string& text) {
    if (text.empty() || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void wrapped(const std::string& text) { ImGui::TextWrapped("%s", text.c_str()); }

void wrappedColored(const Color& c, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string fmt1(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(v));
    return buf;
}

const char* styleName(KeyStyle s) {
    switch (s) {
    case KeyStyle::PlayStation: return "PlayStation (Croix, Rond, L1...)";
    case KeyStyle::Xbox: return "Xbox (A, B, LB...)";
    default: return "Auto : noms Xbox (A, B, LB...)";
    }
}

// ---- Game-thread actions (SDK through ProfileService, results posted back as module commands) --------------
void refreshNow(const UiContext& ctx) {
    ProfileService* ps = ctx.profileService;
    if (!ps) return;
    Engine* eng = &ctx.engine;
    std::shared_ptr<GameWrapper> gw = ctx.game;
    auto apply = ctx.applyActions;
    ctx.runOnGame([eng, ps, gw, apply]() {
        PlayerProfile previous;
        bool accents = false;
        {
            auto lk = eng->lock();
            previous = eng->profile();
            accents = eng->settings().general.hudAccents;
        }
        const PlayerProfile p = ps->read(previous);
        Actions a;
        eng->setProfile(p, a);
        if (apply) {
            apply(a);  // the glue executes everything (toasts, chat, saves, settings...)
        } else if (gw) {  // fallback: show the feedback produced by the modules here
            for (const auto& t : a.toasts)
                gw->Toast(hudText(t.title, accents), hudText(t.text, accents), "default", t.seconds,
                          static_cast<uint8_t>(t.kind));
            for (const auto& m : a.chat) gw->LogToChatbox(hudText(m.text, accents), hudText(m.sender, accents));
        }
        if (a.requestSave) eng->post(Command{"engine", "save", {}});
        eng->post(Command{"profile", "refresh", {"done"}});
    });
}

void checkBakkesBinds(const UiContext& ctx, const std::vector<std::string>& keys) {
    ProfileService* ps = ctx.profileService;
    if (!ps || keys.empty()) return;
    Engine* eng = &ctx.engine;
    ctx.runOnGame([eng, ps, keys]() {
        Command c{"profile", "bmbinds", {}};
        for (const auto& kv : ps->currentBinds(keys)) {
            c.args.push_back(kv.first);
            c.args.push_back(kv.second);
        }
        eng->post(std::move(c));
    });
}

void applyHotkeys(const UiContext& ctx, const std::vector<pf::HotkeyProposal>& proposals) {
    ProfileService* ps = ctx.profileService;
    if (!ps) return;
    std::vector<std::pair<std::string, std::string>> wanted;
    for (const auto& h : proposals)
        if (h.state == pf::HotkeyState::Proposed && !h.key.empty()) wanted.emplace_back(h.key, h.command);
    if (wanted.empty()) return;
    Engine* eng = &ctx.engine;
    std::shared_ptr<CVarManagerWrapper> cvars = ctx.cvars;
    ctx.runOnGame([eng, ps, cvars, wanted]() {
        Command c{"profile", "applied", {}};
        for (const auto& kv : wanted) {
            if (ps->bindIfFree(kv.first, kv.second)) {  // never overwrites a BakkesMod bind
                c.args.push_back(kv.first);
                c.args.push_back(kv.second);
            }
        }
        if (!c.args.empty() && cvars) cvars->executeCommand("writeconfig", false);  // saves binds.cfg
        eng->post(std::move(c));
    });
}

void undoHotkeys(const UiContext& ctx, const std::vector<pf::AppliedHotkey>& applied) {
    ProfileService* ps = ctx.profileService;
    if (!ps || applied.empty()) return;
    Engine* eng = &ctx.engine;
    std::shared_ptr<CVarManagerWrapper> cvars = ctx.cvars;
    ctx.runOnGame([eng, ps, cvars, applied]() {
        Command c{"profile", "undone", {}};
        bool removed = false;
        for (const auto& a : applied) {
            // Only removes binds that still run our command; a key rebound by the player is left alone.
            removed = ps->unbindIfMatches(a.key, a.command) || removed;
            c.args.push_back(a.key);
        }
        if (removed && cvars) cvars->executeCommand("writeconfig", false);
        // The module forgets what it knew about these keys: give it the real binds left after the undo.
        Command check{"profile", "bmbinds", {}};
        for (const auto& kv : ps->currentBinds(c.args)) {
            check.args.push_back(kv.first);
            check.args.push_back(kv.second);
        }
        eng->post(std::move(c));
        eng->post(std::move(check));
    });
}

// ---- Sections ------------------------------------------------------------------------------------------
void checksTable(const char* id, const std::vector<pf::SettingCheck>& checks, bool showAdvice) {
    ImGui::Columns(4, id, false);
    ImGui::SetColumnWidth(0, 170.f);
    ImGui::SetColumnWidth(1, 90.f);
    ImGui::SetColumnWidth(2, 110.f);
    ImGui::TextDisabled("Réglage");
    ImGui::NextColumn();
    ImGui::TextDisabled("Toi");
    ImGui::NextColumn();
    ImGui::TextDisabled("Repère");
    ImGui::NextColumn();
    ImGui::TextDisabled("Avis");
    ImGui::NextColumn();
    for (const auto& c : checks) {
        ImGui::TextUnformatted(c.label.c_str());
        ImGui::NextColumn();
        ImGui::TextUnformatted(c.valueText.c_str());
        ImGui::NextColumn();
        if (c.rangeText.empty()) ImGui::TextDisabled("-");
        else ImGui::TextUnformatted(c.rangeText.c_str());
        ImGui::NextColumn();
        if (c.status == pf::CheckStatus::Advice && !showAdvice) {
            ImGui::TextDisabled("-");
        } else {
            ui::textColored(statusColor(c.status), statusLabel(c.status));
            hoverText(c.advice);
        }
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    if (!showAdvice) return;
    for (const auto& c : checks) {
        if (c.status != pf::CheckStatus::Advice) continue;
        ImGui::Bullet();
        wrappedColored(kAdvice, c.label + " : " + c.advice);
    }
}

void renderSummary(const ProfileReport& r) {
    const PlayerProfile& p = r.profile;
    ui::separatorText("Appareil");
    ImGui::Text("Tu joues : %s", r.device.c_str());
    ImGui::Text("Noms des boutons : %s", styleName(r.style));
    ImGui::Text("Unité de vitesse du jeu : %s", p.metric ? "km/h" : "mph");
    ImGui::Text("Écran : %d x %d", static_cast<int>(p.screenWidth), static_cast<int>(p.screenHeight));
    if (!p.playerName.empty()) ImGui::Text("Joueur : %s", p.playerName.c_str());

    ui::separatorText("Voiture et hitbox");
    const pf::HitboxMatch& hb = r.hitbox;
    ImGui::Text("Voiture : %s", r.car.c_str());
    if (hb.quality != pf::MatchQuality::None) {
        if (const pf::HitboxPreset* preset = pf::findPreset(hb.family)) {
            ImGui::Text("Hitbox : %s", preset->name);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", preset->traits);
            ImGui::TextDisabled("Mêmes dimensions que : %s", preset->examples);
        }
        ImGui::Text("Dimensions : %s x %s x %s uu (longueur x largeur x hauteur)", fmt1(hb.size.x).c_str(),
                    fmt1(hb.size.y).c_str(), fmt1(hb.size.z).c_str());
        ImGui::Text("Toit à %s uu au-dessus du centre de la voiture, nez à %s uu devant", fmt1(hb.roofAboveOrigin).c_str(),
                    fmt1(hb.noseAhead).c_str());
        switch (hb.quality) {
        case pf::MatchQuality::Exact: ui::textColored(kOk, "Hitbox reconnue (lue dans le jeu)."); break;
        case pf::MatchQuality::Close:
            ui::textColored(kInfo, std::string("Hitbox proche de ") + pf::hitboxFamilyName(hb.family) + " (écart " +
                                       fmt1(hb.distance) + " uu).");
            break;
        default:
            ui::textColored(kAdvice, std::string("Hitbox non standard, la plus proche : ") +
                                         pf::hitboxFamilyName(hb.runnerUp) + ".");
            break;
        }
        if (p.carBodyId > 0) ImGui::TextDisabled("Identifiant de carrosserie : %d", p.carBodyId);
    } else {
        ImGui::TextDisabled("Hitbox pas encore lue : entre en freeplay.");
    }

    ui::separatorText("Caméra");
    checksTable("##pf_cam", r.camera, r.showCameraAdvice);

    ui::separatorText("Deadzones et sensibilités");
    checksTable("##pf_pad", r.controller, r.showControllerAdvice);
    wrapped(r.dodgeExplanation);
}

void renderBindings(const ProfileReport& r) {
    wrapped("Toutes les consignes du plugin utilisent tes vrais boutons, par exemple \"" +
            actionPrompt(r.profile, GameAction::Jump, r.style) + "\".");
    const bool pad = r.profile.usingGamepad;
    if (r.valid && !r.deviceBindingsReadable)
        wrappedColored(kInfo, std::string("Les touches de ") + (pad ? "ta manette" : "ton clavier et ta souris") +
                                  " ne sont pas lisibles pour l'instant : les consignes affichent le nom de l'action "
                                  "sans bouton.");
    ImGui::Columns(3, "##pf_binds", false);
    ImGui::SetColumnWidth(0, 170.f);
    ImGui::SetColumnWidth(1, 170.f);
    ImGui::TextDisabled("Action");
    ImGui::NextColumn();
    ImGui::TextDisabled("%s", pad ? "Ta manette" : "Ton clavier / ta souris");
    ImGui::NextColumn();
    ImGui::TextDisabled("%s", pad ? "Clavier / souris" : "Manette");
    ImGui::NextColumn();
    for (const auto& row : r.bindings) {
        ImGui::TextUnformatted(row.name.c_str());
        ImGui::NextColumn();
        if (row.bound) ui::textColored(colors::White, row.keys);
        else ImGui::TextDisabled("%s", r.deviceBindingsReadable ? "non assigné" : "non lu");
        ImGui::NextColumn();
        if (row.otherKeys.empty()) ImGui::TextDisabled("-");
        else ImGui::TextDisabled("%s", row.otherKeys.c_str());
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    const auto rawList = [&](const char* id, const std::vector<KeyBinding>& list) {
        if (list.empty()) {
            ImGui::TextDisabled("Aucune touche lue.");
            return;
        }
        ImGui::Columns(2, id, false);
        ImGui::SetColumnWidth(0, 220.f);
        size_t shown = 0;
        for (const auto& b : list) {
            if (++shown > 200) break;
            ImGui::TextUnformatted(rlActionLabel(b.action).c_str());
            ImGui::NextColumn();
            ImGui::TextUnformatted(keyLabel(b.key, r.style).c_str());
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    };
    if (ImGui::CollapsingHeader("Toutes tes touches manette")) rawList("##pf_raw_pad", r.profile.gamepadBindings);
    if (ImGui::CollapsingHeader("Toutes tes touches clavier et souris")) rawList("##pf_raw_pc", r.profile.pcBindings);
}

void renderAdvice(const ProfileReport& r) {
    if (!r.valid) {
        ImGui::TextDisabled("Ta config n'est pas encore lue.");
        return;
    }
    if (r.adviceCount == 0) ui::textColored(kOk, "Rien à signaler : ta config est dans les repères habituels.");
    else ImGui::Text("%d conseil%s. Ce sont des repères : garde ce qui te va.", r.adviceCount, r.adviceCount > 1 ? "s" : "");

    if (r.showBindingAdvice) {
        ui::separatorText("Tes touches");
        for (const auto& a : r.audit) {
            ImGui::Bullet();
            ui::textColored(statusColor(a.status), a.title);
            ImGui::SameLine();
            wrapped(": " + a.text);
        }
    }

    ui::separatorText("Mécaniques : ta config est-elle prête ?");
    if (r.mechanics.empty())
        wrappedColored(kInfo, "Les touches de ton appareil actuel ne sont pas lisibles : impossible de dire ce qui "
                              "est prêt pour l'instant.");
    for (const auto& m : r.mechanics) {
        ImGui::Bullet();
        ui::textColored(m.ready ? kOk : kAdvice, m.ready ? "Prêt" : "À préparer");
        ImGui::SameLine();
        wrapped(m.name + " : " + m.why);
    }

    if (r.showCameraAdvice || r.showControllerAdvice) {
        ui::separatorText("Caméra et manette");
        bool any = false;
        const auto list = [&](const std::vector<pf::SettingCheck>& checks) {
            for (const auto& c : checks) {
                if (c.status != pf::CheckStatus::Advice) continue;
                any = true;
                ImGui::Bullet();
                wrappedColored(kAdvice, c.label + " : " + c.advice);
            }
        };
        if (r.showCameraAdvice) list(r.camera);
        if (r.showControllerAdvice) list(r.controller);
        if (!any) ui::textColored(kOk, "Tout est dans les repères habituels.");
    }
}

void renderHotkeys(UiContext& ctx, const ProfileReport& r) {
    wrapped("Des raccourcis pour le plugin, sur des touches que ni Rocket League ni BakkesMod n'utilisent. "
            "Rien n'est appliqué sans ton clic, et tout s'annule en un clic.");

    // Keep the BakkesMod bind state fresh while this tab is visible (rate limited, game thread).
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID id = ImGui::GetID("pf_bakkes_check");
        const float now = static_cast<float>(ImGui::GetTime());
        const float last = storage->GetFloat(id, -1000.f);
        const float wait = r.bakkesChecked ? kBakkesRecheckSeconds : kBakkesRetrySeconds;
        if (ctx.profileService && now - last > wait) {
            storage->SetFloat(id, now);
            checkBakkesBinds(ctx, r.hotkeyCandidates);
        }
    }

    if (!r.hotkeysReady) ui::textColored(kInfo, "En attente de tes touches Rocket League (lance le freeplay).");
    if (r.hotkeys.empty()) {
        ImGui::TextDisabled("Aucun raccourci demandé : coche-les dans Réglages > Raccourcis automatiques.");
    } else {
        ImGui::Columns(3, "##pf_hotkeys", false);
        ImGui::SetColumnWidth(0, 200.f);
        ImGui::SetColumnWidth(1, 120.f);
        ImGui::TextDisabled("Commande");
        ImGui::NextColumn();
        ImGui::TextDisabled("Touche");
        ImGui::NextColumn();
        ImGui::TextDisabled("État");
        ImGui::NextColumn();
        for (const auto& h : r.hotkeys) {
            ImGui::TextUnformatted(h.label.c_str());
            hoverText("Commande BakkesMod : " + h.command);
            ImGui::NextColumn();
            if (h.key.empty()) ImGui::TextDisabled("-");
            else ImGui::TextUnformatted(h.keyText.c_str());
            ImGui::NextColumn();
            Color c = kAdvice;
            const char* state = "Aucune touche libre";
            if (h.state == pf::HotkeyState::Applied) {
                c = kOk;
                state = "En place";
            } else if (h.state == pf::HotkeyState::Proposed) {
                c = h.previousConflict ? kAdvice : kInfo;
                state = h.previousMissing ? "À réappliquer" : (h.previousConflict ? "Proposé (conflit)" : "Proposé");
            }
            ui::textColored(c, state);
            hoverText(h.note);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    if (r.bakkesChecked) ImGui::TextDisabled("Binds BakkesMod vérifiés.");
    else ImGui::TextDisabled("Vérification des binds BakkesMod...");

    if (!ctx.profileService) {
        ui::textColored(kAdvice, "Service indisponible : impossible d'appliquer les raccourcis.");
        return;
    }
    if (r.hotkeyConflicts > 0) {
        wrappedColored(kAdvice, r.hotkeyConflicts > 1
                                    ? "Des raccourcis RL Trainer sont sur des touches que le jeu utilise maintenant : "
                                      "une touche lance à la fois l'action du jeu et la commande du plugin."
                                    : "Un raccourci RL Trainer est sur une touche que le jeu utilise maintenant : elle "
                                      "lance à la fois l'action du jeu et la commande du plugin.");
    }
    if (r.hotkeysMissing > 0)
        wrappedColored(kInfo, "Un raccourci installé n'est plus dans BakkesMod (retiré ou non sauvegardé) : il est "
                              "proposé à nouveau.");

    if (r.pendingProposals > 0) {
        if (ImGui::Button("Appliquer")) applyHotkeys(ctx, r.hotkeys);
        hoverText("Crée les binds BakkesMod proposés. Une touche déjà prise entre-temps est ignorée.");
    } else {
        ImGui::TextDisabled("Rien à appliquer.");
    }
    if (!r.applied.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) undoHotkeys(ctx, r.applied);
        hoverText("Retire les binds installés par RL Trainer (seulement s'ils lancent encore la commande prévue).");
        if (r.hotkeyConflicts > 0) {
            ImGui::SameLine();
            if (ImGui::Button("Retirer les conflits")) {
                std::vector<pf::AppliedHotkey> conflicts;
                for (const auto& a : r.appliedStatus)
                    if (a.state == pf::AppliedState::GameConflict) conflicts.push_back({a.key, a.command});
                undoHotkeys(ctx, conflicts);
            }
            hoverText("Retire seulement les binds RL Trainer posés sur une touche que le jeu utilise maintenant.");
        }

        ui::separatorText("Installés par RL Trainer");
        for (const auto& a : r.appliedStatus) {
            ImGui::Bullet();
            ImGui::Text("%s : %s", keyLabel(a.key, r.style).c_str(), a.command.c_str());
            ImGui::SameLine();
            switch (a.state) {
            case pf::AppliedState::Active: ui::textColored(kOk, "(en place)"); break;
            case pf::AppliedState::Missing: ui::textColored(kInfo, "(plus dans BakkesMod)"); break;
            case pf::AppliedState::Rebound: ImGui::TextDisabled("(touche réassignée par toi, laissée telle quelle)"); break;
            case pf::AppliedState::GameConflict: ui::textColored(kAdvice, "(conflit : touche utilisée par le jeu)"); break;
            default: ImGui::TextDisabled("(vérification...)"); break;
            }
        }
        if (ImGui::SmallButton("Oublier cette liste")) ctx.post("profile", "forget");
        hoverText("Vide la liste sans toucher à tes binds BakkesMod.");
    }
}

void renderCommands(UiContext& ctx) {
    wrapped("Ces commandes marchent aussi dans la console BakkesMod (F6) :");
    ImGui::BulletText("rlt profile resume : résumé de ta config et de ses principaux conseils");
    ImGui::BulletText("rlt profile hotkeys : liste des raccourcis proposés dans le chat");
    ImGui::BulletText("rlt profile refresh : demande une relecture de ta config (faite dès que cet onglet est ouvert)");
    ImGui::BulletText("rlt profile forget : vide la liste des raccourcis à annuler");
    ImGui::BulletText("rlt profile resettoast : réaffiche la notification \"Config détectée\"");
    ImGui::BulletText("rlt_menu : ouvre ou ferme la fenêtre RL Trainer");
    ImGui::Spacing();
    if (ImGui::Button("Résumé dans le chat")) ctx.post("profile", "resume");
    ImGui::SameLine();
    if (ImGui::Button("Raccourcis dans le chat")) ctx.post("profile", "hotkeys");
    ImGui::SameLine();
    if (ImGui::Button("Revoir la notification")) ctx.post("profile", "resettoast");
}

} // namespace

void renderProfileTab(UiContext& ctx) {
    ProfileReport r;
    bool enabled = true;
    {
        auto lk = ctx.engine.lock();
        auto* mod = ctx.engine.get<ProfileModule>();
        if (!mod) {
            ImGui::TextDisabled("Module Ma config indisponible.");
            return;
        }
        r = mod->report();
        enabled = mod->config().enabled;
    }

    wrapped("RL Trainer lit ta config Rocket League (touches, caméra, deadzones, voiture) et adapte toutes ses "
            "consignes à tes boutons. Les conseils sont des repères : rien n'est changé sans ton accord.");
    if (!enabled) ui::textColored(colors::Orange, "Module désactivé : l'analyse reste visible, sans notification ni hitbox.");

    if (ctx.profileService) {
        // A "refresh" typed in the console is served here while the window is open (rate limited).
        if (r.refreshPending) {
            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID id = ImGui::GetID("pf_pending_refresh");
            const float now = static_cast<float>(ImGui::GetTime());
            if (now - storage->GetFloat(id, -1000.f) > 3.f) {
                storage->SetFloat(id, now);
                refreshNow(ctx);
            }
        }
        if (ImGui::Button("Relire ma config")) refreshNow(ctx);
        hoverText("Relit tout de suite tes touches, ta caméra, tes deadzones et ta voiture. Sinon, la lecture est "
                  "automatique dès qu'un changement est détecté (toutes les 3 s).");
        ImGui::SameLine();
    }
    if (!r.valid) ImGui::TextDisabled("Config pas encore lue.");
    else if (r.refreshPending) ImGui::TextDisabled("Relecture demandée...");
    else ImGui::TextDisabled("Lue %d fois%s.", r.reads, r.forcedReads > 0 ? " (dont à la demande)" : "");
    if (r.valid) {
        ImGui::Text("Détecté : %s", r.summary.c_str());
        if (r.adviceCount > 0) {
            ImGui::SameLine();
            ui::textColored(kAdvice, "(" + std::to_string(r.adviceCount) + (r.adviceCount > 1 ? " conseils)" : " conseil)"));
        }
        if (!r.lastChange.empty()) ImGui::TextDisabled("Dernier changement : %s", r.lastChange.c_str());
    }

    if (ImGui::BeginTabBar("##pf_tabs")) {
        if (ImGui::BeginTabItem("Ma config")) {
            renderSummary(r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Touches")) {
            renderBindings(r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Conseils")) {
            renderAdvice(r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Raccourcis")) {
            renderHotkeys(ctx, r);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Commandes")) {
            renderCommands(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Réglages")) {
            ui::moduleSettings(ctx, "profile");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace rlt
