#include "plugin/ui/Ui.h"

#include <cstdio>
#include <string>
#include <vector>

#include "core/modules/mechanics/MechanicsModule.h"
#include "imgui.h"

namespace rlt {

namespace {

ImVec4 toImVec(const Color& c) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }

void tooltipText(const std::string& text) {
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

// French decimal separator ("2,5 s").
std::string frNum(float v, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, static_cast<double>(v));
    std::string s = buf;
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

std::string pct(float ratio) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(ratio * 100.f + 0.5f));
    return buf;
}

void renderStatus(UiContext& ctx, const MechanicsModule::Status& st, bool worldControl, bool activityRunning) {
    ui::separatorText("Statut");
    if (!st.enabled) {
        ui::textColored(colors::Orange, "Module désactivé : active-le dans les réglages plus bas.");
        return;
    }
    if (st.focus == mech::Focus::None) {
        ImGui::TextUnformatted("Mode libre : toutes les mécas activées sont détectées et notées.");
    } else {
        ui::textColored(Color(0, 220, 255, 255), std::string(st.drillActive ? "Exercice : " : "Focus : ") +
                                                     mech::focusInfo(st.focus).title);
        ImGui::SameLine();
        ImGui::TextDisabled("(seule cette méca est notée)");
    }
    if (st.drillActive) {
        ImGui::Text("Étape : %s", st.phase.c_str());
        if (!st.variant.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", st.variant.c_str());
        }
        ImGui::Text("Niveau %d/10  |  essais réussis : %d/%d", static_cast<int>(st.level * 10.f + 0.5f), st.drillSuccesses,
                    st.drillAttempts);
        if (st.timeLeft > 0.f) ImGui::Text("Temps restant : %s s", frNum(st.timeLeft, 1).c_str());
        if (!st.goal.empty()) ImGui::TextWrapped("Objectif : %s", st.goal.c_str());
        if (!st.message.empty()) ui::textColored(Color(255, 210, 60, 255), st.message);
    }
    if (st.gameSpeed < 0.999f) ImGui::Text("Ralenti : x%s", frNum(st.gameSpeed, 2).c_str());
    if (st.chainDashes > 0)
        ImGui::Text("Chaîne de wavedash en cours : %d %s", st.chainDashes, st.chainDashes > 1 ? "dashes" : "dash");
    if (st.sessionAttempts > 0) ImGui::Text("Mécas notées pendant la session : %d", st.sessionAttempts);

    if (st.drillActive || st.focus != mech::Focus::None) {
        if (ImGui::Button("Arrêter le focus / l'exercice")) ctx.post("mechanics", "stop");
        if (st.drillActive) {
            ImGui::SameLine();
            if (ImGui::Button("Recommencer l'essai")) ctx.post("mechanics", "retry");
        }
    }
    if (!worldControl)
        ui::textColored(colors::Grey, "Exercices indisponibles ici (freeplay uniquement). Le focus et l'analyse marchent partout.");
    // The slowed-down drill was stopped from outside (Home stop button, another exercise, map change): the game
    // may still run slowly. Offered only when no exercise runs (taking the activity would stop it).
    if (st.speedRestorePending && worldControl && !activityRunning) {
        ui::textColored(Color(255, 210, 60, 255), "Le jeu est peut-être resté au ralenti après l'arrêt de l'exercice.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remettre la vitesse normale")) ctx.post("mechanics", "restore_speed");
    }
}

void renderLastAttempt(const MechanicsModule::LastAttempt& last) {
    ui::separatorText("Dernière tentative");
    if (!last.valid) {
        ImGui::TextDisabled("Aucune méca notée pour l'instant : va en freeplay et joue !");
        return;
    }
    const AttemptResult& r = last.result;
    std::string head = r.title;
    if (!last.variant.empty()) head += " (" + last.variant + ")";
    head += std::string(" : ") + verdictLabel(r.verdict);
    if (last.drillAttempt) head += r.success ? "  [exercice réussi]" : "  [exercice raté]";
    ui::textColored(mech::verdictColor(r.verdict), head);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f / 100", static_cast<double>(r.score));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, toImVec(mech::verdictColor(r.verdict)));
    ImGui::ProgressBar(clamp01(r.score / 100.f), ImVec2(-1.f, 0.f), overlay);
    ImGui::PopStyleColor();
    if (!last.lines.empty()) {
        ImGui::Columns(3, "##mech_last_metrics", false);
        ImGui::TextDisabled("Mesure");
        ImGui::NextColumn();
        ImGui::TextDisabled("Mesuré");
        ImGui::NextColumn();
        ImGui::TextDisabled("Idéal");
        ImGui::NextColumn();
        for (const auto& l : last.lines) {
            ImGui::TextUnformatted(l.label.c_str());
            ImGui::NextColumn();
            ui::textColored(mech::qualityColor(l.quality), l.value);
            ImGui::NextColumn();
            ImGui::TextUnformatted(l.ideal.empty() ? "-" : l.ideal.c_str());
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
    if (r.tips.empty()) {
        ui::textColored(Color(80, 230, 80, 255), last.headline);
    } else {
        for (size_t i = 0; i < r.tips.size(); ++i) {
            ImGui::Bullet();
            ImGui::SameLine();
            if (i == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, toImVec(Color(255, 210, 60, 255)));
                ImGui::TextWrapped("%s", r.tips[i].text.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextWrapped("%s", r.tips[i].text.c_str());
            }
        }
    }
}

void renderGrid(UiContext& ctx, const std::vector<MechanicsModule::StatRow>& rows, const MechanicsModule::Status& st,
                bool worldControl) {
    ui::separatorText("Mécaniques");
    ImGui::TextDisabled("Survole un nom pour l'explication. Focus : seule cette méca est notée. Exercice : placement automatique.");
    ImGui::Columns(7, "##mech_grid", true);
    const char* headers[7] = {"Méca", "Essais", "Réussite", "Moyenne", "Record", "Niveau", "Entraîner"};
    for (const char* h : headers) {
        ImGui::TextDisabled("%s", h);
        ImGui::NextColumn();
    }
    ImGui::Separator();
    const char* group = "";
    for (const auto& r : rows) {
        if (r.group != group) {
            group = r.group.c_str();
            ui::textColored(Color(255, 200, 60, 255), r.group);
            for (int i = 0; i < 7; ++i) ImGui::NextColumn();
        }
        ImGui::PushID(r.id.c_str());
        if (r.enabled) ImGui::TextUnformatted(r.title.c_str());
        else ImGui::TextDisabled("%s (désactivé)", r.title.c_str());
        const char* offNote = "\n\nNon notée en mode libre (réglage désactivé). Le focus et l'exercice la notent quand même.";
        tooltipText(r.what + "\n" + r.key + (r.enabled ? "" : offNote) +
                    (r.topErrorTip.empty() ? "" : "\n\nTon erreur la plus fréquente : " + r.topErrorTip));
        ImGui::NextColumn();
        ImGui::Text("%d", r.attempts);
        if (r.sessionAttempts > 0) tooltipText("Cette session : " + std::to_string(r.sessionSuccesses) + "/" +
                                               std::to_string(r.sessionAttempts) + " réussis");
        ImGui::NextColumn();
        if (r.attempts > 0) ImGui::TextUnformatted(pct(static_cast<float>(r.successes) / static_cast<float>(r.attempts)).c_str());
        else ImGui::TextDisabled("-");
        ImGui::NextColumn();
        if (r.attempts > 0) ImGui::Text("%.0f (max %.0f)", static_cast<double>(r.average), static_cast<double>(r.best));
        else ImGui::TextDisabled("-");
        ImGui::NextColumn();
        if (r.hasPb) {
            ImGui::TextUnformatted(r.pbText.c_str());
            tooltipText(r.pbLabel);
        } else {
            ImGui::TextDisabled("-");
        }
        ImGui::NextColumn();
        ImGui::Text("%d/10", static_cast<int>(r.level * 10.f + 0.5f));
        ImGui::NextColumn();
        const bool focused = st.focus == r.focus;
        if (focused && !st.drillActive) {
            if (ImGui::SmallButton("Focus off")) ctx.post("mechanics", "focus", {"off"});
        } else if (ImGui::SmallButton("Focus")) {
            ctx.post("mechanics", "focus", {r.id});
        }
        tooltipText("Seule cette méca est notée, objectifs affichés en jeu.");
        ImGui::SameLine();
        if (worldControl) {
            if (ImGui::SmallButton("Exercice")) ctx.post("mechanics", "drill", {r.id});
            tooltipText("Place la voiture automatiquement et relance après chaque essai (freeplay).");
        } else {
            ImGui::TextDisabled("Exercice");
            tooltipText("Exercices disponibles en freeplay uniquement.");
        }
        ImGui::NextColumn();
        ImGui::PopID();
    }
    ImGui::Columns(1);
}

void renderExplanations(const std::vector<MechanicsModule::StatRow>& rows) {
    if (!ImGui::CollapsingHeader("Explications et progression")) return;
    ImGui::Indent();
    for (const auto& r : rows) {
        ImGui::PushID(r.id.c_str());
        ui::textColored(Color(0, 220, 255, 255), r.title);
        ImGui::TextWrapped("%s", r.what.c_str());
        ImGui::TextWrapped("%s", r.key.c_str());
        if (!r.topErrorTip.empty()) ui::textColored(Color(255, 210, 60, 255), "Erreur la plus fréquente : " + r.topErrorTip);
        if (r.recent.size() >= 2) {
            ImGui::PlotLines("##recent", r.recent.data(), static_cast<int>(r.recent.size()), 0, "Derniers scores", 0.f, 100.f,
                             ImVec2(0.f, 40.f));
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
    ImGui::Unindent();
}

void renderCommands(UiContext& ctx) {
    if (!ImGui::CollapsingHeader("Commandes console et stats")) return;
    ImGui::Indent();
    ImGui::TextWrapped("Dans la console BakkesMod (F6) :");
    ImGui::BulletText("rlt mechanics focus <méca> : ne noter que cette méca (\"off\" pour revenir au mode libre)");
    ImGui::BulletText("rlt mechanics drill <méca> : lancer l'exercice (freeplay)");
    ImGui::BulletText("rlt mechanics retry : replacer la voiture pour un nouvel essai");
    ImGui::BulletText("rlt mechanics stop : arrêter le focus et l'exercice (vitesse du jeu remise à 1)");
    ImGui::BulletText("rlt mechanics reset_stats [méca] : remettre les stats à zéro");
    ImGui::BulletText("rlt mechanics restore_speed : remettre la vitesse du jeu à 1 si elle est restée au ralenti");
    ImGui::TextWrapped("Mécas : %s", mech::focusIdList().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Remettre toutes les stats à zéro")) ImGui::OpenPopup("##mech_reset_confirm");
    if (ImGui::BeginPopupModal("##mech_reset_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Effacer toutes tes stats et niveaux de mécas ?");
        if (ImGui::Button("Oui, tout effacer")) {
            ctx.post("mechanics", "reset_stats");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::Unindent();
}

} // namespace

void renderMechanicsTab(UiContext& ctx) {
    MechanicsModule::Status st;
    std::vector<MechanicsModule::StatRow> rows;
    MechanicsModule::LastAttempt last;
    bool found = false;
    bool worldControl = false;
    bool activityRunning = false;
    {
        auto lk = ctx.engine.lock();
        auto* mod = ctx.engine.get<MechanicsModule>();
        if (mod) {
            found = true;
            st = mod->status();
            rows = mod->statRows();
            last = mod->lastAttempt();
        }
        worldControl = ctx.engine.worldControlAllowed();
        activityRunning = !ctx.engine.activityOwner().empty();
    }
    if (!found) {
        ImGui::TextDisabled("Module Mécas mouvement indisponible.");
        return;
    }
    ImGui::TextWrapped("Joue normalement : speedflips, wavedashes, half-flips, fast aerials, réceptions... sont détectés et "
                       "notés automatiquement, avec tes vrais boutons dans les conseils. Choisis une méca pour le focus ou "
                       "lance un exercice pour t'entraîner à fond.");
    renderStatus(ctx, st, worldControl, activityRunning);
    renderLastAttempt(last);
    renderGrid(ctx, rows, st, worldControl);
    renderExplanations(rows);
    renderCommands(ctx);
    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "mechanics");
}

} // namespace rlt
