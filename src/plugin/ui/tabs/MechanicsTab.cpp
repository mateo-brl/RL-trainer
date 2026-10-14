#include "plugin/ui/Ui.h"

#include <algorithm>
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

const Color kTitle(0, 220, 255, 255), kTip(255, 210, 60, 255), kGood(80, 230, 80, 255), kHeading(255, 220, 120, 255);

// Families from the simplest to the hardest (stable inside a tier).
std::vector<const MechanicsModule::StatRow*> byTier(const std::vector<MechanicsModule::StatRow>& rows) {
    std::vector<const MechanicsModule::StatRow*> v;
    for (const auto& r : rows) v.push_back(&r);
    std::stable_sort(v.begin(), v.end(), [](const auto* a, const auto* b) { return a->tier < b->tier; });
    return v;
}

void numberedSteps(const std::vector<std::string>& steps, int current) {
    for (size_t k = 0; k < steps.size(); ++k) {
        const std::string text = std::to_string(k + 1) + ". " + steps[k];
        if (static_cast<int>(k) == current) {
            ImGui::PushStyleColor(ImGuiCol_Text, toImVec(kTip));
            ImGui::TextWrapped("%s  <- maintenant", text.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s", text.c_str());
        }
    }
}

void renderStatus(UiContext& ctx, const MechanicsModule::Status& st, const std::vector<MechanicsModule::StatRow>& rows,
                  bool worldControl) {
    ui::separatorText("Statut");
    if (!st.enabled) {
        ui::textColored(colors::Orange, "Module désactivé : active-le dans les réglages plus bas.");
        return;
    }
    if (st.focus == mech::Focus::None) {
        ImGui::TextUnformatted("Mode libre : toutes les mécas activées sont détectées et notées.");
    } else {
        ui::textColored(kTitle, std::string(st.drillActive ? "Exercice : " : "Focus : ") + mech::focusInfo(st.focus).title);
        ImGui::SameLine();
        ImGui::TextDisabled("(seule cette méca est notée)");
    }
    if (st.drillActive) {
        ImGui::Text("Niveau %d/10  |  essais réussis : %d/%d", st.levelNumber, st.drillSuccesses, st.drillAttempts);
        if (!st.goal.empty()) ImGui::TextWrapped("%s", st.goal.c_str());
        ui::textColored(kHeading, "À faire, dans l'ordre :");
        numberedSteps(st.steps, st.currentStep);
        if (!st.message.empty()) ui::textColored(kTip, st.message);
        ImGui::Text("Essai : %s", st.phase.c_str());
        if (st.timeLeft > 0.f) {
            ImGui::SameLine();
            ImGui::Text("|  temps restant : %s s", frNum(st.timeLeft, 1).c_str());
        }
        if (ImGui::CollapsingHeader("Détails techniques##mech_status")) {
            ImGui::Indent();
            ImGui::TextUnformatted("Pour réussir à ce niveau :");
            for (const auto& c : st.checks) ImGui::BulletText("%s", c.c_str());
            if (!st.variant.empty()) ImGui::Text("Variante : %s", st.variant.c_str());
            ImGui::Unindent();
        }
    } else if (st.focus != mech::Focus::None) {
        for (const auto& r : rows) {
            if (r.focus != st.focus) continue;
            ui::textColored(kHeading, "À faire, dans l'ordre :");
            numberedSteps(r.steps, -1);
        }
    }
    if (st.gameSpeed < 0.999f) ImGui::Text("Ralenti : x%s", frNum(st.gameSpeed, 2).c_str());
    if (st.speedWaiting)
        ImGui::TextDisabled("Un autre exercice est en cours : le changement de vitesse s'appliquera quand il se terminera.");
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
    if (last.drillAttempt) head += r.success ? " : exercice réussi" : " : exercice raté";
    else head += std::string(" : ") + verdictLabel(r.verdict);
    ui::textColored(last.drillAttempt ? (r.success ? kGood : Color(255, 80, 60, 255)) : mech::verdictColor(r.verdict), head);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.0f / 100", static_cast<double>(r.score));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, toImVec(mech::verdictColor(r.verdict)));
    ImGui::ProgressBar(clamp01(r.score / 100.f), ImVec2(-1.f, 0.f), overlay);
    ImGui::PopStyleColor();
    // The one thing to work on, in words.
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec(r.tips.empty() ? kGood : kTip));
    ImGui::TextWrapped("%s", last.headline.c_str());
    ImGui::PopStyleColor();
    if (!ImGui::CollapsingHeader("Détails techniques##mech_last")) return;
    ImGui::Indent();
    if (r.tips.size() > 1) {
        ImGui::TextUnformatted("Autres points :");
        for (size_t i = 1; i < r.tips.size(); ++i) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", r.tips[i].text.c_str());
        }
    }
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
    ImGui::Unindent();
}

void renderGrid(UiContext& ctx, const std::vector<MechanicsModule::StatRow>& rows, const MechanicsModule::Status& st,
                bool worldControl) {
    ui::separatorText("Mécaniques");
    ui::textColored(kGood, mech::tierAdvice());
    ImGui::TextDisabled("Survole un nom pour l'explication. Focus : seule cette méca est notée. Exercice : placement automatique.");
    ImGui::Columns(7, "##mech_grid", true);
    const char* headers[7] = {"Méca", "Essais", "Réussite", "Moyenne", "Record", "Niveau", "Entraîner"};
    for (const char* h : headers) {
        ImGui::TextDisabled("%s", h);
        ImGui::NextColumn();
    }
    ImGui::Separator();
    int tier = -1;
    for (const auto* rp : byTier(rows)) {
        const auto& r = *rp;
        if (static_cast<int>(r.tier) != tier) {
            tier = static_cast<int>(r.tier);
            ui::textColored(Color(255, 200, 60, 255), mech::tierLabel(r.tier));
            for (int i = 0; i < 7; ++i) ImGui::NextColumn();
        }
        ImGui::PushID(r.id.c_str());
        if (r.enabled) ImGui::TextUnformatted(r.title.c_str());
        else ImGui::TextDisabled("%s (désactivé)", r.title.c_str());
        const char* offNote = "\n\nNon notée en mode libre (réglage désactivé). Le focus et l'exercice la notent quand même.";
        tooltipText(r.title + " (" + mech::tierLabel(r.tier) + ")\n" + r.what + "\n" + r.key + (r.enabled ? "" : offNote) +
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
        ImGui::Text("%d/10", r.levelNumber);
        tooltipText("Niveau de l'exercice : il monte quand tu réussis, baisse quand tu rates.");
        ImGui::NextColumn();
        const bool focused = st.focus == r.focus;
        if (focused && !st.drillActive) {
            if (ImGui::SmallButton("Focus off")) ctx.post("mechanics", "focus", {"off"});
        } else if (ImGui::SmallButton("Focus")) {
            ctx.post("mechanics", "focus", {r.id});
        }
        tooltipText("Seule cette méca est notée, l'action à faire reste affichée en jeu.");
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

void renderGuides(const std::vector<MechanicsModule::StatRow>& rows) {
    ui::separatorText("Guides");
    ImGui::TextWrapped("Ouvre une méca pour savoir quoi faire, dans l'ordre. Elles sont rangées de la plus simple à la "
                       "plus dure.");
    for (const auto* rp : byTier(rows)) {
        const auto& r = *rp;
        const std::string header = r.title + " (" + mech::tierLabel(r.tier) + ")##mech_guide_" + r.id;
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        ImGui::PushID(r.id.c_str());
        ImGui::Indent();
        ImGui::TextWrapped("%s", r.what.c_str());
        ui::textColored(kHeading, "Ce qu'il faut faire :");
        numberedSteps(r.steps, -1);
        ui::textColored(kHeading, "La clé pour réussir :");
        ImGui::TextWrapped("%s", r.key.c_str());
        ui::textColored(kHeading, "L'exercice :");
        ImGui::TextWrapped("Ton niveau : %d/10. Il monte quand tu réussis et baisse quand tu rates. %s", r.levelNumber,
                           r.progression.c_str());
        if (!r.topErrorTip.empty()) ui::textColored(kTip, "Ton erreur la plus fréquente : " + r.topErrorTip);
        if (r.recent.size() >= 2) {
            ImGui::PlotLines("##recent", r.recent.data(), static_cast<int>(r.recent.size()), 0, "Derniers scores", 0.f, 100.f,
                             ImVec2(0.f, 40.f));
        }
        // Numbers for those who want them: the ideal values, then what a drill success needs per level.
        if (ImGui::CollapsingHeader("Détails techniques")) {
            ImGui::Indent();
            ImGui::TextWrapped("%s", r.tech.c_str());
            auto list = [](const char* title, const std::vector<std::string>& checks) {
                ImGui::TextUnformatted(title);
                for (const auto& c : checks) ImGui::BulletText("%s", c.c_str());
            };
            list("Exercice, niveau 1 :", r.checksFirst);
            const std::string now = "Exercice, ton niveau (" + std::to_string(r.levelNumber) + "/10) :";
            if (r.levelNumber > 1 && r.levelNumber < 10) list(now.c_str(), r.checksNow);
            list("Exercice, niveau 10 :", r.checksLast);
            ImGui::Unindent();
        }
        ImGui::Unindent();
        ImGui::PopID();
    }
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
    ImGui::BulletText("rlt mechanics restore_speed : remettre la vitesse du jeu à 1 (attend la fin de l'exercice d'un autre module)");
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
    }
    if (!found) {
        ImGui::TextDisabled("Module Mécas mouvement indisponible.");
        return;
    }
    ImGui::TextWrapped("Joue normalement : tes mécas de déplacement (réceptions, powerslides, wavedashes, speedflips...) "
                       "sont détectées et notées toutes seules, avec un conseil à chaque fois. Pour t'entraîner sur une "
                       "méca, lance son exercice : la voiture est placée pour toi et le niveau s'adapte à tes essais.");
    renderStatus(ctx, st, rows, worldControl);
    renderLastAttempt(last);
    renderGrid(ctx, rows, st, worldControl);
    renderGuides(rows);
    renderCommands(ctx);
    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "mechanics");
}

} // namespace rlt
