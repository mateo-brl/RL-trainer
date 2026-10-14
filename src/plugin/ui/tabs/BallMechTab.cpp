#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/ballmech/BallMechModule.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Tips.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

ImVec4 vec4(const Color& c) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }

Color verdictUiColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return {60, 230, 90, 255};
    case Verdict::Good: return {150, 230, 80, 255};
    case Verdict::Ok: return {255, 200, 60, 255};
    case Verdict::Fail: return {255, 90, 70, 255};
    default: return colors::White;
    }
}

std::string number(float v, int decimals) { return formatNumber(v, decimals); }

std::string metricValue(const Metric& m, SpeedUnit unit) {
    if (m.unit == "uu/s") return formatSpeed(m.value, unit, 0);
    if (m.unit == "s") return number(m.value, 2) + " s";
    if (m.unit == "°") return number(m.value, 0) + "°";
    if (m.unit == "%") return number(m.value, 0) + " %";
    if (m.unit == "uu") return number(m.value, 0) + " uu";
    if (m.unit.empty()) return number(m.value, m.value == static_cast<float>(static_cast<int>(m.value)) ? 0 : 2);
    return number(m.value, 1) + " " + m.unit;
}

std::string metricIdeal(const Metric& m, SpeedUnit unit) {
    if (!m.hasIdeal) return "";
    Metric i = m;
    i.value = m.ideal;
    return metricValue(i, unit);
}

struct FlickChoice {
    const char* id;
    const char* label;
};
const FlickChoice kFlickChoices[] = {{"any", "Tous les flicks"}, {"front", "Flick avant"}, {"45", "Flick 45°"},
                                     {"90", "Flick 90°"},        {"backflip", "Flick arrière"}, {"side", "Flick latéral"},
                                     {"reverse", "Flick inversé (Mawkzy)"}};

void tooltipWrapped(const std::string& text) {
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

const char* const kBeginnerAdvice =
    "Débutant : commence par la frappe puissante et le tir aérien, puis le flick et l'air roll shot. Le musty, le "
    "flip reset et le breezi viennent après.";

// Mechanics in learning order (easiest first), as indexes into st.mechs.
std::vector<size_t> byDifficulty(const BallMechModule::UiState& st) {
    std::vector<size_t> idx(st.mechs.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(),
                     [&st](size_t a, size_t b) { return st.mechs[a].order < st.mechs[b].order; });
    return idx;
}

// "level" command argument for a displayed level (1..10), as a fraction the module reads exactly.
std::string levelArg(int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(bm::levelOfPlayer(std::max(1, std::min(10, n)))));
    return buf;
}

void drawLastResult(const BallMechModule::UiState& st, SpeedUnit unit) {
    ui::separatorText("Dernière méca détectée");
    if (!st.hasLast) {
        ImGui::TextDisabled("Rien pour l'instant : lance-toi en freeplay, chaque méca est notée automatiquement.");
        return;
    }
    const AttemptResult& r = st.last;
    const Color vc = verdictUiColor(r.verdict);
    ui::textColored(vc, r.title + " : " + verdictLabel(r.verdict) + (r.success ? " (réussi)" : " (raté)"));
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%d / 100", static_cast<int>(r.score + 0.5f));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, vec4(vc));
    ui::progressBar(std::max(0.f, std::min(1.f, r.score / 100.f)), overlay);
    ImGui::PopStyleColor();
    if (!r.tips.empty()) {
        ui::textColored({255, 220, 120, 255}, "Conseil :");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", r.tips.front().text.c_str());
    } else if (r.success) {
        ui::textColored({60, 230, 90, 255}, "Bien joué, continue comme ça !");
    }
    if (!ImGui::CollapsingHeader("Détails techniques##bm_last")) return;
    if (!r.metrics.empty()) {
        ImGui::Columns(3, "bm_last_metrics", false);
        ImGui::TextDisabled("Mesure");
        ImGui::NextColumn();
        ImGui::TextDisabled("Valeur");
        ImGui::NextColumn();
        ImGui::TextDisabled("Idéal");
        ImGui::NextColumn();
        for (const auto& m : r.metrics) {
            if (m.key == "source" || m.key == "dodge_forward" || m.key == "dodge_side" || m.key == "exit_kmh") continue;
            ImGui::TextUnformatted(m.label.c_str());
            ImGui::NextColumn();
            if (m.key == "on_target" || m.key == "goal" || m.key == "flip_shot" || m.key == "jumped_off" ||
                m.key == "used_double_jump" || m.key == "finished_on_target")
                ImGui::TextUnformatted(m.value >= 0.5f ? "oui" : "non");
            else
                ImGui::TextUnformatted(metricValue(m, unit).c_str());
            ImGui::NextColumn();
            const std::string ideal = (m.key == "on_target" || m.key == "flip_shot" || m.key == "finished_on_target")
                                          ? std::string(m.hasIdeal ? "oui" : "")
                                          : (m.key == "jumped_off" ? std::string("non") : metricIdeal(m, unit));
            ImGui::TextDisabled("%s", ideal.c_str());
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }
    if (r.tips.size() > 1) {
        ImGui::TextUnformatted("Autres conseils :");
        for (size_t i = 1; i < r.tips.size(); ++i) ImGui::BulletText("%s", r.tips[i].text.c_str());
    }
}

void drawDrillStatus(UiContext& ctx, const BallMechModule::UiState& st, const PlayerProfile& profile, bool worldControl,
                     const std::string& otherActivity) {
    const auto& d = st.drill;
    if (d.active) {
        ui::separatorText("Exercice en cours : " + d.title + " (niveau " + std::to_string(d.levelNumber) + "/10)");
        ImGui::TextWrapped("À faire : %s", d.action.c_str());
        ImGui::TextWrapped("%s", bm::fillButtons(d.hint, profile, st.keyStyle).c_str());
        if (d.timeLeft > 0.f) ImGui::Text("Temps restant : %d s", static_cast<int>(d.timeLeft + 0.99f));
        else ImGui::TextUnformatted(d.phase.c_str());
        ImGui::Text("Essais : %d | Réussis : %d", d.attempts, d.successes);
        if (!d.lastVerdict.empty()) ui::textColored(d.lastSuccess ? Color{60, 230, 90, 255} : Color{255, 110, 80, 255}, d.lastVerdict);
        if (!d.tip.empty()) {
            ui::textColored({255, 220, 120, 255}, "Conseil :");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", d.tip.c_str());
        }
        if (ImGui::Button("Essai suivant##bm")) ctx.post("ballmech", "next");
        ImGui::SameLine();
        if (ImGui::Button("Arrêter l'exercice##bm")) ctx.post("ballmech", "stop");
    } else if (!worldControl) {
        ui::textColored(colors::Grey, "Exercices disponibles en freeplay (la détection marche partout hors ligne).");
    } else if (!otherActivity.empty()) {
        ui::textColored(colors::Grey, "Un autre exercice tourne (" + otherActivity + ") : lancer un exercice ici l'arrêtera.");
    }
}

void drawMechGrid(UiContext& ctx, const BallMechModule::UiState& st, const PlayerProfile& profile, bool worldControl) {
    ui::separatorText("Tes mécas (des plus simples aux plus dures)");
    ImGui::TextWrapped("%s", kBeginnerAdvice);
    ImGui::Columns(7, "bm_grid", true);
    const char* heads[] = {"Méca", "Difficulté", "Essais", "Réussite", "Moy. / record", "Progression", "Exercice"};
    for (const char* h : heads) {
        ImGui::TextDisabled("%s", h);
        ImGui::NextColumn();
    }
    ImGui::Separator();
    for (size_t i : byDifficulty(st)) {
        const auto& m = st.mechs[i];
        ImGui::PushID(m.id.c_str());
        if (m.detectionEnabled) ImGui::TextUnformatted(m.title.c_str());
        else ImGui::TextDisabled("%s (désactivée)", m.title.c_str());
        tooltipWrapped(m.what + "\n\nLa clé : " + bm::fillButtons(m.key, profile, st.keyStyle));
        ImGui::NextColumn();
        ImGui::TextUnformatted(m.difficulty.c_str());
        ImGui::NextColumn();
        ImGui::Text("%d", m.attempts);
        if (m.sessionAttempts > 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(+%d)", m.sessionAttempts);
        }
        ImGui::NextColumn();
        if (m.attempts > 0) ImGui::TextUnformatted(ui::percent(static_cast<float>(m.successes) / static_cast<float>(m.attempts)).c_str());
        else ImGui::TextDisabled("-");
        ImGui::NextColumn();
        if (m.attempts > 0) ImGui::Text("%d / %d", static_cast<int>(m.average + 0.5f), static_cast<int>(m.best + 0.5f));
        else ImGui::TextDisabled("-");
        ImGui::NextColumn();
        if (m.recent.size() >= 2)
            ImGui::PlotLines("##recent", m.recent.data(), static_cast<int>(m.recent.size()), 0, nullptr, 0.f, 100.f,
                             ImVec2(-1.f, 18.f));
        else
            ImGui::TextDisabled("-");
        ImGui::NextColumn();
        if (worldControl) {
            if (ImGui::SmallButton("Lancer")) ctx.post("ballmech", "drill", {m.id});
            ImGui::SameLine();
        }
        ImGui::Text("niv. %d/10", m.levelNumber);
        ImGui::SameLine();
        if (ImGui::SmallButton("-")) ctx.post("ballmech", "level", {m.id, levelArg(m.levelNumber - 1)});
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) ctx.post("ballmech", "level", {m.id, levelArg(m.levelNumber + 1)});
        ImGui::NextColumn();
        ImGui::PopID();
    }
    ImGui::Columns(1);

    // Flick variant requested by the flick drill.
    int current = 0;
    for (int i = 0; i < static_cast<int>(sizeof(kFlickChoices) / sizeof(kFlickChoices[0])); ++i) {
        const std::string id = kFlickChoices[i].id;
        if ((id == "any" && st.flickVariant.empty()) || id == st.flickVariant) current = i;
    }
    const char* labels[sizeof(kFlickChoices) / sizeof(kFlickChoices[0])];
    for (size_t i = 0; i < sizeof(kFlickChoices) / sizeof(kFlickChoices[0]); ++i) labels[i] = kFlickChoices[i].label;
    ImGui::SetNextItemWidth(220.f);
    if (ImGui::Combo("Flick demandé dans l'exercice##bm", &current, labels, static_cast<int>(sizeof(labels) / sizeof(labels[0]))))
        ctx.post("ballmech", "variant", {kFlickChoices[current].id});
    ui::helpMarker("L'exercice Flick valide seulement ce flick (Tous = n'importe lequel).");
}

void drawRecords(UiContext& ctx, const BallMechModule::UiState& st, SpeedUnit unit) {
    ui::separatorText("Records");
    ImGui::Text("Flip resets : %d cette session, %d au total", st.sessionResets, st.totalResets);
    ImGui::Text("Flick le plus fort : %s", st.hardestFlick > 0.f ? formatSpeed(st.hardestFlick, unit, 0).c_str() : "-");
    ImGui::Text("Plus long air dribble : %s", st.bestAirDribble > 0.f ? (number(st.bestAirDribble, 2) + " s").c_str() : "-");
    ImGui::Text("Pinch le plus rapide : %s", st.bestPinch > 0.f ? formatSpeed(st.bestPinch, unit, 0).c_str() : "-");
    ImGui::Text("Frappe la plus forte : %s", st.bestPowerShot > 0.f ? formatSpeed(st.bestPowerShot, unit, 0).c_str() : "-");
    if (ImGui::Button("Nouvelle session##bm")) ctx.post("ballmech", "reset_session");
    ui::helpMarker("Remet à zéro les compteurs de la session (flip resets, essais du jour). Tes stats restent.");
    ImGui::SameLine();
    if (ImGui::Button("Réinitialiser les stats##bm")) ImGui::OpenPopup("Réinitialiser les stats ?##bm_reset_stats");
    if (ImGui::BeginPopupModal("Réinitialiser les stats ?##bm_reset_stats", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Effacer toutes les stats, records et niveaux des mécas balle ?");
        if (ImGui::Button("Oui, tout effacer")) {
            ctx.post("ballmech", "reset_stats");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void drawGuide(const BallMechModule::UiState& st, const PlayerProfile& profile, SpeedUnit unit) {
    ImGui::TextWrapped("Chaque méca est détectée toute seule pendant que tu joues. Ouvre une méca pour savoir quoi "
                       "faire, dans l'ordre. Elles sont rangées de la plus simple à la plus dure.");
    ImGui::TextWrapped("%s", kBeginnerAdvice);
    for (size_t i : byDifficulty(st)) {
        const auto& m = st.mechs[i];
        const std::string header = m.title + " (" + m.difficulty + ")##guide_" + m.id;
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        ImGui::Indent();
        ImGui::TextWrapped("%s", m.what.c_str());
        ui::textColored({255, 220, 120, 255}, "Ce qu'il faut faire :");
        for (size_t k = 0; k < m.steps.size(); ++k) {
            const std::string step = bm::fillButtons(m.steps[k], profile, st.keyStyle);
            ImGui::TextWrapped("%d. %s", static_cast<int>(k + 1), step.c_str());
        }
        ui::textColored({255, 220, 120, 255}, "La clé pour réussir :");
        ImGui::TextWrapped("%s", bm::fillButtons(m.key, profile, st.keyStyle).c_str());
        ui::textColored({255, 220, 120, 255}, "L'exercice :");
        ImGui::TextWrapped("%s Ton niveau : %d/10. Il monte quand tu réussis et baisse quand tu rates.",
                           bm::fillButtons(m.drillHint, profile, st.keyStyle).c_str(), m.levelNumber);
        ImGui::TextWrapped("%s", m.progression.c_str());

        // Numbers for those who want them: what a success needs at each level, time per attempt.
        bm::Mech mech;
        const std::string techId = "Détails techniques##tech_" + m.id;
        if (bm::mechFromId(m.id, mech) && ImGui::CollapsingHeader(techId.c_str())) {
            ImGui::Indent();
            auto criteria = [&](float level) {
                return bm::drillCriteria(mech, level, st.successScore, st.pinchSpeed, unit);
            };
            ImGui::TextWrapped("Niveau 1 : %s.", criteria(0.f).c_str());
            ImGui::TextWrapped("Niveau 10 : %s.", criteria(1.f).c_str());
            ImGui::TextWrapped("Ton niveau (%d/10) : %s.", m.levelNumber, criteria(m.drillLevel).c_str());
            const float base = bm::mechInfo(mech).drillTimeout * std::max(0.5f, st.timeoutScale);
            ImGui::Text("Temps par essai : %d s au niveau 1, %d s au niveau 10.",
                        static_cast<int>(base * bm::drillTimeFactor(0.f) + 0.5f),
                        static_cast<int>(base * bm::drillTimeFactor(1.f) + 0.5f));
            ImGui::TextWrapped("Tes mesures détaillées (en ms, degrés, vitesses) sont dans Tableau de bord > "
                               "Dernière méca détectée > Détails techniques.");
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }
    ui::separatorText("Tes boutons");
    ImGui::Text("Saut : %s", actionPrompt(profile, GameAction::Jump, st.keyStyle).c_str());
    ImGui::Text("Boost : %s", actionPrompt(profile, GameAction::Boost, st.keyStyle).c_str());
    ImGui::Text("Air roll : %s", bm::airRollPrompt(profile, st.keyStyle).c_str());
    ImGui::Text("Glissade : %s", actionPrompt(profile, GameAction::Powerslide, st.keyStyle).c_str());

    if (!ImGui::CollapsingHeader("Commandes console (F6)##bm_console")) return;
    ImGui::BulletText("rlt ballmech drill <méca> [variante] : lance un exercice");
    ImGui::TextDisabled("    mécas : flipreset, ceiling, flick, musty, breezi, doubletap, airdribble, airroll, aerial, "
                        "pinch, powershot");
    ImGui::TextDisabled("    variantes du flick : front, 45, 90, backflip, side, reverse, any");
    ImGui::BulletText("rlt ballmech stop : arrête l'exercice");
    ImGui::BulletText("rlt ballmech next : passe à l'essai suivant");
    ImGui::BulletText("rlt ballmech level <méca> <0-100> : règle le niveau d'un exercice");
    ImGui::TextDisabled("    0 = niveau 1, 100 = niveau 10");
    ImGui::BulletText("rlt ballmech variant <flick> : flick demandé dans l'exercice Flick");
    ImGui::BulletText("rlt ballmech reset_session / reset_stats : remet la session / tout à zéro");
}

} // namespace

void renderBallMechTab(UiContext& ctx) {
    BallMechModule::UiState st;
    bool found = false;
    GameContext game;
    PlayerProfile profile;
    bool worldControl = false;
    std::string owner, activity;
    {
        auto lk = ctx.engine.lock();
        if (auto* mod = ctx.engine.get<BallMechModule>()) {
            st = mod->uiState();
            found = true;
        }
        game = ctx.engine.context();
        profile = ctx.engine.profile();
        worldControl = ctx.engine.worldControlAllowed();
        owner = ctx.engine.activityOwner();
        activity = ctx.engine.activityName();
    }
    if (!found) {
        ImGui::TextDisabled("Module Mécas balle indisponible.");
        return;
    }
    const SpeedUnit unit = resolveUnit(SpeedUnit::Auto, profile.metric);

    ImGui::TextWrapped("Joue normalement en freeplay : tes frappes, tirs aériens, flicks, pinchs, air dribbles, double "
                       "taps, ceiling shots et flip resets sont reconnus et notés tout seuls, avec un conseil à chaque "
                       "fois. Pour travailler une méca, lance son exercice.");
    if (!st.enabled) ui::textColored(colors::Orange, "Module désactivé : active-le dans Réglages.");
    else if (game.mode == GameMode::Online) ui::textColored(colors::Orange, "En ligne : rien n'est analysé.");
    else if (!st.detecting) ui::textColored(colors::Grey, "Pas d'analyse dans ce mode de jeu pour le moment.");
    else ui::textColored({60, 230, 90, 255}, std::string("Analyse active (") + gameModeLabel(game.mode) + ")");
    if (st.resetAvailable) {
        ImGui::SameLine();
        ui::textColored({60, 230, 90, 255}, "| Flip reset disponible !");
    }

    drawDrillStatus(ctx, st, profile, worldControl, owner.empty() || owner == "ballmech" ? std::string() : activity);

    if (ImGui::BeginTabBar("bm_tabs")) {
        if (ImGui::BeginTabItem("Tableau de bord")) {
            drawLastResult(st, unit);
            drawMechGrid(ctx, st, profile, worldControl);
            drawRecords(ctx, st, unit);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Guide des mécas")) {
            drawGuide(st, profile, unit);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Réglages")) {
            ui::moduleSettings(ctx, "ballmech");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace rlt
