// "Progression" tab: overview (today, streak, session), session goals, training plan with "Lancer" buttons,
// per-family skill table, history sparkline of the selected skill, frequent mistakes, past sessions,
// console commands, reset with confirmation and the coach settings.
// Render thread: module state is copied under the engine lock, actions go through ctx.post().

#include "plugin/ui/Ui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/Text.h"
#include "core/modules/coach/CoachCatalog.h"
#include "core/modules/coach/CoachGoals.h"
#include "core/modules/coach/CoachModule.h"
#include "core/modules/coach/CoachUtil.h"
#include "imgui.h"

namespace rlt {

namespace {

const Color kGold{255, 200, 60, 255};
const Color kSoft{170, 180, 195, 255};
const Color kUp{90, 220, 110, 255};
const Color kDown{255, 110, 90, 255};

// Widget state of this tab (selection and form inputs). Only the ImGui render thread touches it.
struct CoachTabState {
    std::string selectedSkill;
    int presetIndex = 0;  // 0 = focus, then coach::goalPresets()
    int customKind = 1;   // GoalKind
    std::string customScope = "any";  // scope argument (stable when the skill list changes)
    int customTarget = 10;
    int customMinScore = 70;
};

CoachTabState& tabState() {
    static CoachTabState state;
    return state;
}

// Copy of everything the tab shows, taken under the engine lock.
struct CoachUiData {
    bool present = false;
    CoachConfig cfg;
    CoachOverview overview;
    std::vector<Goal> goals;
    std::string summary;
    bool worldControl = false;
    // Heavier lists, copied again only when the module revision changes.
    unsigned revision = 0;
    const void* source = nullptr;  // module instance the lists come from (plugin reloads create a new one)
    std::vector<SkillView> skills;
    std::vector<PlanItem> plan;
    std::vector<MistakeView> mistakes;
    std::vector<SessionRecord> sessions;
    std::vector<DayPoint> days;
};

// Render-thread cache: refreshed every frame for the small parts, on revision change for the lists.
const CoachUiData& copyState(UiContext& ctx) {
    static CoachUiData d;
    auto lk = ctx.engine.lock();
    const auto* mod = ctx.engine.get<CoachModule>();
    d.present = mod != nullptr;
    if (!mod) return d;
    d.cfg = mod->config();
    d.overview = mod->overview();
    d.goals = mod->goals();
    d.summary = mod->currentSessionSummary();
    d.worldControl = ctx.engine.worldControlAllowed();
    if (d.source != mod || d.revision != mod->revision()) {
        d.revision = mod->revision();
        d.source = mod;
        d.skills = mod->skills();
        d.plan = mod->plan();
        d.mistakes = mod->topMistakes(6);
        d.sessions = mod->sessionHistory();
        d.days = mod->recentDays(14);
    }
    return d;
}

void tooltip(const std::string& text) {
    if (text.empty() || !ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void wrappedColored(const Color& c, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

std::string shorten(const std::string& s, size_t maxChars) {
    if (coach::utf8Length(s) <= maxChars) return s;
    std::string out;
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0xC0) != 0x80) {
            if (n == maxChars - 3) break;
            ++n;
        }
        out.push_back(s[i]);
    }
    return out + "...";
}

Color trendColor(Trend t) {
    switch (t) {
    case Trend::Up: return kUp;
    case Trend::Down: return kDown;
    default: return kSoft;
    }
}

void launchButton(UiContext& ctx, const LaunchSpec& l, const char* label) {
    if (ImGui::Button(label)) ctx.post(l.module, l.command, l.args);
    tooltip(l.label + "\nConsole : " + l.consoleLine());
}

// ---- Sections ----------------------------------------------------------------------------------------------
void renderOverview(UiContext& ctx, const CoachUiData& d) {
    const CoachOverview& o = d.overview;
    ui::separatorText("Aujourd'hui");
    ImGui::Columns(3, "##coach_overview", false);

    ui::textColored(kGold, "Aujourd'hui");
    ImGui::Text("%s", plural(o.todayAttempts, "essai noté", "essais notés").c_str());
    if (o.todayAttempts > 0) {
        ImGui::Text("Moyenne %s", coach::formatScore(o.todayAverage).c_str());
        ImGui::Text("Réussite %s",
                    ui::percent(static_cast<float>(o.todaySuccesses) / static_cast<float>(o.todayAttempts)).c_str());
    } else if (o.today.empty()) {
        ImGui::TextDisabled("Date inconnue pour l'instant.");
    }
    ImGui::NextColumn();

    ui::textColored(kGold, "Série");
    ImGui::Text("%s", plural(o.streak, "jour", "jours").c_str());
    ImGui::TextDisabled("Record : %s", plural(o.bestStreak, "jour", "jours").c_str());
    const int need = std::max(1, o.streakMin);
    const int done = std::min(o.todayAttempts, need);
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%d/%d aujourd'hui", done, need);
    ImGui::ProgressBar(static_cast<float>(done) / static_cast<float>(need), ImVec2(-1.f, 0.f), overlay);
    tooltip("Fais au moins " + plural(need, "essai noté", "essais notés") +
            " par jour pour prolonger ta série. S'entraîner un peu chaque jour vaut mieux "
            "qu'une longue séance par semaine.");
    ImGui::NextColumn();

    ui::textColored(kGold, o.sessionActive ? "Session n° " + std::to_string(o.sessionIndex) : std::string("Session"));
    if (o.sessionActive) {
        ImGui::Text("%s en %s", plural(o.sessionAttempts, "essai", "essais").c_str(),
                    formatClock(o.sessionSeconds).c_str());
        if (o.sessionAttempts > 0) ImGui::Text("Moyenne %s", coach::formatScore(o.sessionAverage).c_str());
        if (o.sessionRecords > 0) ui::textColored(kGold, plural(o.sessionRecords, "record battu", "records battus"));
    } else {
        ImGui::TextDisabled("Elle démarre à ton premier essai noté.");
    }
    ImGui::Columns(1);

    if (ImGui::Button("Nouvelle session")) ctx.post("coach", "new_session");
    tooltip("Termine la session en cours (résumé dans le chat) et en commence une nouvelle.");
    ImGui::SameLine();
    if (ImGui::Button("Résumé dans le chat")) ctx.post("coach", "summary");
    ImGui::SameLine();
    if (ImGui::Button("Aperçu de la carte")) ctx.post("coach", "preview");
    tooltip("Affiche une carte d'exemple en jeu pour régler sa position et sa taille.");
    if (!d.summary.empty()) wrappedColored(kSoft, d.summary);
    ImGui::TextDisabled("Total : %s, %s suivies.", plural(o.totalAttempts, "essai", "essais").c_str(),
                        plural(o.skillCount, "compétence", "compétences").c_str());
}

void renderGoals(UiContext& ctx, const CoachUiData& d) {
    CoachTabState& st = tabState();
    ui::separatorText("Objectifs de session");
    if (d.goals.empty())
        ImGui::TextWrapped(
            "Aucun objectif. Choisis-en un ou deux : un but précis motive bien plus que « fais de ton mieux ».");
    for (size_t i = 0; i < d.goals.size(); ++i) {
        const Goal& g = d.goals[i];
        ImGui::PushID(static_cast<int>(i));
        if (g.done) ui::textColored(kUp, g.label + " : atteint !");
        else ImGui::TextUnformatted(g.label.c_str());
        char overlay[32];
        std::snprintf(overlay, sizeof(overlay), "%d/%d", g.progress, g.target);
        ImGui::ProgressBar(g.ratio(), ImVec2(-90.f, 0.f), overlay);
        ImGui::SameLine();
        if (ImGui::SmallButton("Retirer")) ctx.post("coach", "goal_remove", {std::to_string(i + 1)});
        ImGui::PopID();
    }

    if (d.goals.size() < coach::kMaxGoals) {
        // Presets.
        const auto& presets = coach::goalPresets();
        const int count = static_cast<int>(presets.size()) + 1;
        st.presetIndex = std::clamp(st.presetIndex, 0, count - 1);
        auto presetLabel = [&](int i) {
            return i == 0 ? std::string("Focus : 10 réussites sur ta priorité du plan")
                          : std::string(presets[static_cast<size_t>(i - 1)].label);
        };
        ImGui::SetNextItemWidth(320.f);
        if (ImGui::BeginCombo("##coach_preset", presetLabel(st.presetIndex).c_str())) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(presetLabel(i).c_str(), i == st.presetIndex)) st.presetIndex = i;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Ajouter##coach_preset_add"))
            ctx.post("coach", "goal_add",
                     {st.presetIndex == 0 ? std::string("focus")
                                          : std::string(presets[static_cast<size_t>(st.presetIndex - 1)].id)});

        // Custom goal.
        if (ImGui::TreeNode("Objectif personnalisé")) {
            static const char* kinds[] = {"Essais", "Réussites", "Essais notés au moins...", "Records battus"};
            static const char* kindArgs[] = {"attempts", "success", "score", "record"};
            ImGui::SetNextItemWidth(220.f);
            ImGui::Combo("Type", &st.customKind, kinds, 4);
            st.customKind = std::clamp(st.customKind, 0, 3);

            // Scopes: everything, the families, then the practised skills.
            std::vector<std::pair<std::string, std::string>> scopes;  // (arg, label)
            scopes.push_back({"any", "Tous les exercices"});
            for (const char* f : {"first_touch", "mech", "ball", "kickoff", "course.rings", "course.control",
                                  "shoot.flick", "shoot.target"})
                scopes.push_back({f, coach::scopeLabel(f)});
            for (const auto& s : d.skills) scopes.push_back({s.id, "  " + s.title});
            size_t current = 0;
            for (size_t i = 0; i < scopes.size(); ++i)
                if (scopes[i].first == st.customScope) current = i;
            st.customScope = scopes[current].first;
            ImGui::SetNextItemWidth(220.f);
            if (ImGui::BeginCombo("Compétence", scopes[current].second.c_str())) {
                for (size_t i = 0; i < scopes.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(scopes[i].second.c_str(), i == current)) st.customScope = scopes[i].first;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("Nombre", &st.customTarget);
            st.customTarget = std::clamp(st.customTarget, 1, 1000);
            if (st.customKind == 2) {
                ImGui::SetNextItemWidth(220.f);
                ImGui::SliderInt("Note mini", &st.customMinScore, 0, 100);
            }
            if (ImGui::Button("Ajouter l'objectif")) {
                std::vector<std::string> args = {"custom", kindArgs[st.customKind], st.customScope,
                                                 std::to_string(st.customTarget)};
                if (st.customKind == 2) args.push_back(std::to_string(st.customMinScore));
                ctx.post("coach", "goal_add", std::move(args));
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("3 objectifs maximum : retires-en un pour en ajouter.");
    }
    if (!d.goals.empty() && ImGui::SmallButton("Effacer tous les objectifs")) ctx.post("coach", "goal_clear");
}

void renderPlan(UiContext& ctx, const CoachUiData& d) {
    ui::separatorText("Plan conseillé");
    ImGui::TextWrapped(
        "Les exercices les plus utiles maintenant : tes points faibles, ce que tu n'as pas travaillé "
        "depuis longtemps et tes erreurs qui reviennent, en variant les familles.");
    if (!d.worldControl)
        ui::textColored(kSoft, "Les exercices se lancent en freeplay (ici, le plugin analyse seulement).");
    for (size_t i = 0; i < d.plan.size(); ++i) {
        const PlanItem& it = d.plan[i];
        ImGui::PushID(static_cast<int>(i));
        launchButton(ctx, it.launch, "Lancer");
        ImGui::SameLine();
        ui::textColored(kGold, std::to_string(i + 1) + ". " + it.title + (it.starter ? "  (pour démarrer)" : ""));
        ImGui::Indent(58.f);
        wrappedColored(kSoft, it.reason);
        ImGui::Unindent(58.f);
        ImGui::PopID();
    }
    if (ImGui::SmallButton("Envoyer le plan dans le chat")) ctx.post("coach", "plan");
}

void renderSkillDetail(UiContext& ctx, const SkillView& s) {
    ui::separatorText("Historique : " + s.title);
    if (!s.scores.empty()) {
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "Moyenne récente %s", coach::formatScore(s.averageRecent).c_str());
        ImGui::PlotLines("##coach_spark", s.scores.data(), static_cast<int>(s.scores.size()), 0, overlay, 0.f, 100.f,
                         ImVec2(-1.f, 90.f));
        tooltip("Tes " + std::to_string(s.scores.size()) +
                " dernières notes (0 à 100), de la plus ancienne à la plus récente.");
    }
    ImGui::Text("Essais %d | réussite %s (10 derniers : %s) | meilleure note %s", s.attempts,
                ui::percent(s.successRate).c_str(), ui::percent(s.recentSuccess).c_str(),
                coach::formatScore(s.bestScore).c_str());
    ImGui::Text("Moyenne : %s (20 derniers), %s (total) | tendance ", coach::formatScore(s.averageRecent).c_str(),
                coach::formatScore(s.averageAll).c_str());
    ImGui::SameLine(0.f, 0.f);
    ui::textColored(trendColor(s.trend), trendLabel(s.trend));
    if (s.hasRecord)
        ui::textColored(kGold, "Record : " + s.recordText + (s.lowerIsBetter ? " (plus bas = mieux)" : ""));
    if (!s.lastDate.empty()) {
        std::string when = s.lastDate;
        if (s.daysSince == 0) when += " (aujourd'hui)";
        else if (s.daysSince > 0) when += " (il y a " + plural(s.daysSince, "jour", "jours") + ")";
        ImGui::TextDisabled("Dernier entraînement : %s, session n° %d", when.c_str(), s.lastSession);
    }
    if (s.mainMistake.valid()) {
        ui::textColored(colors::Orange, "Erreur principale (" + std::to_string(s.mainMistake.count) + " essais sur " +
                                            std::to_string(s.mainMistake.window) + ") :");
        ImGui::Indent();
        ImGui::TextWrapped("%s", s.mainMistake.text.empty() ? s.mainMistake.code.c_str() : s.mainMistake.text.c_str());
        ImGui::Unindent();
    }
    if (s.launchable) launchButton(ctx, s.launch, "Lancer l'exercice##coach_detail");
}

void renderSkills(UiContext& ctx, const CoachUiData& d) {
    CoachTabState& st = tabState();
    ui::separatorText("Compétences");
    if (d.skills.empty()) {
        ImGui::TextWrapped(
            "Aucun essai noté pour l'instant. Lance un exercice du plan conseillé ou joue en freeplay : "
            "chaque essai noté par les autres onglets apparaît ici.");
        return;
    }
    // Keep a valid selection (default: the first skill).
    const auto selected =
        std::find_if(d.skills.begin(), d.skills.end(), [&](const SkillView& s) { return s.id == st.selectedSkill; });
    if (selected == d.skills.end()) st.selectedSkill = d.skills.front().id;

    size_t i = 0;
    while (i < d.skills.size()) {
        const std::string family = d.skills[i].family;
        size_t end = i;
        int attempts = 0;
        while (end < d.skills.size() && d.skills[end].family == family) attempts += d.skills[end++].attempts;
        const std::string header = d.skills[i].familyLabel + " (" +
                                   plural(static_cast<int>(end - i), "compétence", "compétences") + ", " +
                                   plural(attempts, "essai", "essais") + ")##coach_fam_" + family;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const std::string colId = "##coach_cols_" + family;
            // Measure the full width BEFORE entering the columns (inside, the region is the first column).
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::Columns(7, colId.c_str(), true);
            if (w > 300.f) {
                ImGui::SetColumnWidth(0, w * 0.22f);
                ImGui::SetColumnWidth(1, w * 0.08f);
                ImGui::SetColumnWidth(2, w * 0.10f);
                ImGui::SetColumnWidth(3, w * 0.10f);
                ImGui::SetColumnWidth(4, w * 0.12f);
                ImGui::SetColumnWidth(5, w * 0.11f);
            }
            for (const char* h :
                 {"Compétence", "Essais", "Réussite", "Moyenne", "Record", "Tendance", "Erreur principale"}) {
                ImGui::TextDisabled("%s", h);
                ImGui::NextColumn();
            }
            ImGui::Separator();
            for (size_t k = i; k < end; ++k) {
                const SkillView& s = d.skills[k];
                const std::string label = s.title + "##coach_skill_" + s.id;
                if (ImGui::Selectable(label.c_str(), s.id == st.selectedSkill, ImGuiSelectableFlags_SpanAllColumns))
                    st.selectedSkill = s.id;
                tooltip("Clique pour voir l'historique. Identifiant : " + s.id);
                ImGui::NextColumn();
                ImGui::Text("%d", s.attempts);
                ImGui::NextColumn();
                ImGui::TextUnformatted(ui::percent(s.successRate).c_str());
                ImGui::NextColumn();
                ImGui::TextUnformatted(coach::formatScore(s.averageRecent).c_str());
                tooltip("Moyenne des 20 derniers essais. Depuis le début : " + coach::formatScore(s.averageAll) + ".");
                ImGui::NextColumn();
                ImGui::TextUnformatted(s.hasRecord ? s.recordText.c_str() : "-");
                ImGui::NextColumn();
                ui::textColored(trendColor(s.trend), trendLabel(s.trend));
                ImGui::NextColumn();
                if (s.mainMistake.valid()) {
                    const std::string text = s.mainMistake.text.empty() ? s.mainMistake.code : s.mainMistake.text;
                    ImGui::TextUnformatted(shorten(text, 42).c_str());
                    tooltip(text + "\n" + std::to_string(s.mainMistake.count) + " essais sur les " +
                            std::to_string(s.mainMistake.window) + " derniers.");
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::NextColumn();
            }
            ImGui::Columns(1);
        }
        i = end;
    }

    for (const auto& s : d.skills)
        if (s.id == st.selectedSkill) renderSkillDetail(ctx, s);
}

void renderMistakes(const CoachUiData& d) {
    if (d.mistakes.empty()) return;
    ui::separatorText("Erreurs fréquentes");
    ImGui::TextDisabled("Une seule correction à la fois : commence par la première.");
    for (const auto& m : d.mistakes) {
        ui::textColored(kGold, m.skillTitle);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d essais sur %d)", m.mistake.count, m.mistake.window);
        ImGui::Indent();
        ImGui::TextWrapped("%s", m.mistake.text.empty() ? m.mistake.code.c_str() : m.mistake.text.c_str());
        ImGui::Unindent();
    }
}

void renderHistory(const CoachUiData& d) {
    if (!ImGui::CollapsingHeader("Historique des jours et des sessions##coach_history")) return;
    if (!d.days.empty()) {
        std::vector<float> values;
        float maxV = 1.f;
        for (const auto& p : d.days) {
            values.push_back(static_cast<float>(p.attempts));
            maxV = std::max(maxV, static_cast<float>(p.attempts));
        }
        ImGui::PlotHistogram("##coach_days", values.data(), static_cast<int>(values.size()), 0,
                             "Essais par jour (14 derniers jours)", 0.f, maxV * 1.15f, ImVec2(-1.f, 70.f));
        tooltip("Du " + d.days.front().date + " au " + d.days.back().date + ".");
    }
    if (d.sessions.empty()) {
        ImGui::TextDisabled("Aucune session terminée pour l'instant.");
        return;
    }
    ImGui::Columns(7, "##coach_sessions", true);
    for (const char* h : {"N°", "Date", "Essais", "Moyenne", "Records", "Durée", "Point fort / à travailler"}) {
        ImGui::TextDisabled("%s", h);
        ImGui::NextColumn();
    }
    ImGui::Separator();
    for (auto it = d.sessions.rbegin(); it != d.sessions.rend(); ++it) {
        ImGui::Text("%d", it->index);
        ImGui::NextColumn();
        ImGui::TextUnformatted(it->date.empty() ? "-" : it->date.c_str());
        ImGui::NextColumn();
        ImGui::Text("%d", it->attempts);
        ImGui::NextColumn();
        ImGui::TextUnformatted(coach::formatScore(it->average).c_str());
        ImGui::NextColumn();
        ImGui::Text("%d", it->records);
        ImGui::NextColumn();
        ImGui::TextUnformatted(formatClock(it->seconds).c_str());
        ImGui::NextColumn();
        const std::string sw = (it->strong.empty() ? std::string("-") : it->strong) + " / " +
                               (it->weak.empty() ? std::string("-") : it->weak);
        ImGui::TextUnformatted(sw.c_str());
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void renderCommands() {
    if (!ImGui::CollapsingHeader("Commandes console##coach_commands")) return;
    ImGui::TextDisabled("Dans la console BakkesMod (F6) :");
    ImGui::BulletText("rlt coach new_session : termine la session (résumé) et en démarre une");
    ImGui::BulletText("rlt coach plan : écrit le plan conseillé dans le chat");
    ImGui::BulletText("rlt coach summary : résumé de la session en cours");
    ImGui::BulletText("rlt coach goal_add <objectif> : focus, ft70, speedflip10, kickoff_pb...");
    ImGui::BulletText(
        "rlt coach goal_add custom <attempts|success|score|record> <compétence|any> <nombre> [note mini]");
    ImGui::BulletText("rlt coach goal_remove <n> / rlt coach goal_clear");
    ImGui::BulletText("rlt coach preview : carte d'exemple pour régler sa position");
    ImGui::BulletText("rlt coach reset_all : efface toute la progression du coach");
}

void renderReset(UiContext& ctx) {
    ui::separatorText("Réinitialisation");
    if (ImGui::Button("Réinitialiser la progression")) ImGui::OpenPopup("Réinitialiser la progression ?##coach_reset");
    tooltip("Efface stats, records, séries et historique du coach. Tes réglages et tes objectifs sont gardés.");
    if (ImGui::BeginPopupModal("Réinitialiser la progression ?##coach_reset", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Toutes tes stats, records, séries et sessions du coach seront effacés.");
        ImGui::Text("Cette action est définitive.");
        ImGui::Spacing();
        if (ImGui::Button("Oui, tout effacer")) {
            ctx.post("coach", "reset_all");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace

void renderCoachTab(UiContext& ctx) {
    const CoachUiData& d = copyState(ctx);
    if (!d.present) {
        ImGui::TextDisabled("Module Coach indisponible.");
        return;
    }
    ImGui::TextWrapped(
        "Chaque essai noté dans les autres onglets arrive ici : stats, records, erreurs qui reviennent, "
        "séries de jours et un plan d'entraînement qui s'adapte à toi.");
    if (!d.cfg.enabled)
        ui::textColored(colors::Orange,
                        "Le coach est désactivé (réglages en bas de page) : aucun essai n'est enregistré.");

    renderOverview(ctx, d);
    renderGoals(ctx, d);
    renderPlan(ctx, d);
    renderSkills(ctx, d);
    renderMistakes(d);
    renderHistory(d);
    renderCommands();
    renderReset(ctx);

    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "coach");
}

} // namespace rlt
