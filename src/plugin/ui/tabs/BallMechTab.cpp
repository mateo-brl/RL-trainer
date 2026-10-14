#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/ballmech/BallMechModule.h"
#include "core/modules/ballmech/Drills.h"
#include "core/modules/ballmech/Mechanics.h"
#include "core/modules/ballmech/Parcours.h"
#include "core/modules/ballmech/Tips.h"
#include "core/modules/ballmech/Tutorials.h"
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
    "Débutant : commence par la frappe puissante et le tir aérien, puis le flick et le pinch. L'air roll shot et le "
    "ceiling shot viennent ensuite, puis l'air dribble, le musty, le double tap, le flip reset et le breezi.";

const Color kAccent{255, 199, 64, 255};
const Color kGood{60, 230, 90, 255};

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
        ImGui::TextWrapped("Départ : %s", d.modeLabel.c_str());
        if (d.waitingThrottle) ui::textColored(kAccent, "Appuie sur accélérer quand tu es prêt.");
        if (d.full && !d.steps.empty()) {
            for (size_t i = 0; i < d.steps.size(); ++i) {
                const std::string& line = d.steps[i];
                if (line.find(" : OK") != std::string::npos) ui::textColored(kGood, line);
                else if (line.find(" : raté") != std::string::npos) ui::textColored({255, 110, 80, 255}, line);
                else if (static_cast<int>(i) == d.currentStep) ui::textColored(kAccent, line);
                else ImGui::TextUnformatted(line.c_str());
            }
            if (!d.missing.empty()) ui::textColored({255, 110, 80, 255}, d.missing);
        } else {
            ImGui::TextWrapped("À faire : %s", d.action.c_str());
        }
        ImGui::TextWrapped("%s", bm::fillButtons(d.hint, profile, st.keyStyle).c_str());
        if (d.timeLeft > 0.f) ImGui::Text("Temps restant : %d s", static_cast<int>(d.timeLeft + 0.99f));
        else ImGui::TextUnformatted(d.phase.c_str());
        ImGui::Text("Essais : %d | Réussis : %d", d.attempts, d.successes);
        if (!d.lastVerdict.empty()) ui::textColored(d.lastSuccess ? kGood : Color{255, 110, 80, 255}, d.lastVerdict);
        if (!d.tip.empty()) {
            ui::textColored({255, 220, 120, 255}, "Conseil :");
            ImGui::SameLine();
            ImGui::TextWrapped("%s", d.tip.c_str());
        }
        if (ImGui::Button("Essai suivant##bm")) ctx.post("ballmech", "next");
        ImGui::SameLine();
        if (ImGui::Button("Arrêter l'exercice##bm")) ctx.post("ballmech", "stop");
        // Switch the start of the running drill (applies from the next attempt).
        for (const auto& m : st.mechs) {
            if (m.id != d.mechId) continue;
            ImGui::SameLine();
            const bool full = m.mode == 1;
            const std::string label = std::string(full ? "Passer au départ court" : "Passer au parcours complet") + "##bm_mode";
            if (ImGui::Button(label.c_str())) ctx.post("ballmech", "mode", {m.id, full ? "court" : "parcours"});
        }
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
        ImGui::SameLine();
        if (ImGui::SmallButton("Niv. 1")) ctx.post("ballmech", "level", {m.id, levelArg(1)});
        tooltipWrapped("Revenir au niveau 1");
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
    ImGui::TextDisabled("Flick 45 : un huitième de tour avant le flip. Flick 90 : un quart de tour.");
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

// YouTube search of a mechanic (English names: most tutorials are in English).
const char* videoQuery(bm::Mech m) {
    switch (m) {
    case bm::Mech::FlipReset: return "flip reset rocket league tutorial";
    case bm::Mech::Ceiling: return "ceiling shot rocket league tutorial";
    case bm::Mech::Flick: return "flick rocket league tutorial";
    case bm::Mech::Musty: return "musty flick rocket league tutorial";
    case bm::Mech::Breezi: return "breezi flick rocket league tutorial";
    case bm::Mech::DoubleTap: return "double tap rocket league tutorial";
    case bm::Mech::AirDribble: return "air dribble rocket league tutorial";
    case bm::Mech::AirRoll: return "air roll shot rocket league tutorial";
    case bm::Mech::Aerial: return "aerial rocket league tutorial";
    case bm::Mech::Pinch: return "pinch rocket league tutorial";
    case bm::Mech::PowerShot: return "power shot rocket league tutorial";
    default: return "rocket league mechanics tutorial";
    }
}

void drawTutorial(UiContext& ctx, const BallMechModule::UiState& st, const BallMechModule::MechView& m, bm::Mech mech,
                  const PlayerProfile& profile, bool worldControl, SpeedUnit unit) {
    const bm::Tutorial& t = bm::tutorial(mech);
    auto fill = [&](const char* text) { return bm::fillButtons(text, profile, st.keyStyle); };
    ImGui::TextWrapped("%s", m.what.c_str());
    ui::videoSearchButton("Voir des tutos vidéo", videoQuery(mech));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Démo automatique : disponible pour les mécas de déplacement seulement, ici regarde les tutos "
                       "vidéo.");
    ImGui::PopStyleColor();
    if (t.before && *t.before) {
        ui::textColored(kAccent, "Conseillé avant :");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", t.before);
    }
    ui::textColored(kAccent, "Le principe");
    ImGui::TextWrapped("%s", fill(t.principle).c_str());
    if (t.goodToKnow && *t.goodToKnow) ImGui::TextWrapped("Bon à savoir : %s", fill(t.goodToKnow).c_str());

    // Start of the drill, launch, back to niveau 1.
    const char* modes[] = {m.modeShort.c_str(), m.modeFull.c_str()};
    int mode = m.mode;
    ImGui::SetNextItemWidth(260.f);
    const std::string comboId = "Départ##mode_" + m.id;
    if (ImGui::Combo(comboId.c_str(), &mode, modes, 2)) ctx.post("ballmech", "mode", {m.id, mode == 1 ? "parcours" : "court"});
    ui::helpMarker("Parcours complet : toutes les étapes avant la méca (se placer, ramasser la balle, monter au mur...). "
                   "Départ court : la méca seule, la voiture et la balle déjà placées.");
    if (worldControl) {
        const std::string launchId = "Lancer l'exercice##guide_launch_" + m.id;
        if (ImGui::Button(launchId.c_str())) ctx.post("ballmech", "drill", {m.id});
        ImGui::SameLine();
    }
    const std::string resetId = "Revenir au niveau 1##guide_lvl1_" + m.id;
    if (ImGui::Button(resetId.c_str())) ctx.post("ballmech", "level", {m.id, levelArg(1)});
    ImGui::SameLine();
    ImGui::Text("Ton niveau : %d/10", m.levelNumber);

    ui::textColored(kAccent, "Les étapes");
    for (size_t k = 0; k < t.steps.size(); ++k) {
        const bm::TutorialStep& step = t.steps[k];
        ImGui::TextWrapped("Étape %d : %s", static_cast<int>(k + 1), step.title);
        ImGui::Indent();
        ImGui::TextWrapped("À faire : %s", fill(step.todo).c_str());
        if (step.note && *step.note) ui::textColored({255, 160, 90, 255}, fill(step.note));
        ImGui::TextWrapped("Pourquoi : %s", fill(step.why).c_str());
        ImGui::TextWrapped("C'est bon quand : %s", fill(step.ok).c_str());
        ImGui::Unindent();
    }
    const bool pickup = mech == bm::Mech::Flick || mech == bm::Mech::Musty || mech == bm::Mech::Breezi || mech == bm::Mech::AirDribble;
    if (pickup) {
        ui::textColored(kAccent, bm::pickupBlock().title);
        ImGui::TextWrapped("%s", fill(bm::pickupBlock().text).c_str());
        ImGui::TextDisabled("Niveau du ramassage : %d/10", st.pickupLevel);
    }
    for (const auto& b : t.blocks) {
        ui::textColored(kAccent, b.title);
        ImGui::TextWrapped("%s", fill(b.text).c_str());
    }
    if (!t.errors.empty()) {
        ui::textColored(kAccent, "Erreurs fréquentes");
        for (const auto& e : t.errors) ImGui::BulletText("« %s » : %s", e.name, fill(e.fix).c_str());
    }
    ui::textColored(kAccent, "Niveau 1");
    ImGui::TextWrapped("%s", fill(t.level1).c_str());
    ImGui::TextWrapped("%s", m.progression.c_str());

    // What the HUD shows at the player's level, for the chosen start.
    ui::textColored(kAccent, "Panneau en jeu");
    if (m.mode == 1) {
        for (const auto& line : bm::parcoursLabels(mech, m.levelNumber, st.flickVariant)) ImGui::BulletText("%s", line.c_str());
    } else {
        ImGui::BulletText("%s", bm::drillAction(mech, m.drillLevel, mech == bm::Mech::Flick ? st.flickVariant : "").c_str());
    }

    // Numbers for those who want them.
    const std::string techId = "Détails techniques##tech_" + m.id;
    if (ImGui::CollapsingHeader(techId.c_str())) {
        ImGui::Indent();
        for (const char* line : t.tech) ImGui::TextWrapped("%s", line);
        if (pickup)
            for (const char* line : bm::pickupTech()) ImGui::TextWrapped("%s", line);
        auto criteria = [&](float level) { return bm::drillCriteria(mech, level, st.successScore, st.pinchSpeed, unit); };
        ImGui::TextWrapped("Niveau 1 : %s.", criteria(0.f).c_str());
        ImGui::TextWrapped("Niveau 10 : %s.", criteria(1.f).c_str());
        ImGui::TextWrapped("Ton niveau (%d/10) : %s.", m.levelNumber, criteria(m.drillLevel).c_str());
        const float base = bm::mechInfo(mech).drillTimeout * std::max(0.5f, st.timeoutScale);
        ImGui::Text("Temps par essai (départ court) : %d s au niveau 1, %d s au niveau 10.",
                    static_cast<int>(base * bm::drillTimeFactor(0.f) + 0.5f),
                    static_cast<int>(base * bm::drillTimeFactor(1.f) + 0.5f));
        ImGui::TextWrapped("Tes mesures détaillées (en ms, degrés, vitesses) sont dans Tableau de bord > "
                           "Dernière méca détectée > Détails techniques.");
        ImGui::Unindent();
    }
}

void drawGuide(UiContext& ctx, const BallMechModule::UiState& st, const PlayerProfile& profile, bool worldControl,
               SpeedUnit unit) {
    ImGui::TextWrapped("Chaque méca est détectée toute seule pendant que tu joues. Ouvre une méca pour savoir quoi "
                       "faire, étape par étape. Elles sont rangées de la plus simple à la plus dure.");
    ImGui::TextWrapped("%s", kBeginnerAdvice);
    for (size_t i : byDifficulty(st)) {
        const auto& m = st.mechs[i];
        const std::string header = m.title + " (" + m.difficulty + ")##guide_" + m.id;
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        bm::Mech mech;
        if (!bm::mechFromId(m.id, mech)) continue;
        ImGui::PushID(m.id.c_str());
        ImGui::Indent();
        drawTutorial(ctx, st, m, mech, profile, worldControl, unit);
        ImGui::Unindent();
        ImGui::PopID();
    }
    ui::separatorText("Tes boutons");
    ImGui::Text("Saut : %s", actionPrompt(profile, GameAction::Jump, st.keyStyle).c_str());
    ImGui::Text("Boost : %s", actionPrompt(profile, GameAction::Boost, st.keyStyle).c_str());
    ImGui::Text("Air roll : %s", bm::airRollPrompt(profile, st.keyStyle).c_str());
    ImGui::Text("Glissade : %s", actionPrompt(profile, GameAction::Powerslide, st.keyStyle).c_str());

    if (!ImGui::CollapsingHeader("Commandes console (F6)##bm_console")) return;
    ImGui::BulletText("rlt ballmech drill <méca> [variante] [court|parcours] : lance un exercice");
    ImGui::TextDisabled("    mécas : flipreset, ceiling, flick, musty, breezi, doubletap, airdribble, airroll, aerial, "
                        "pinch, powershot");
    ImGui::TextDisabled("    variantes du flick : front, 45, 90, backflip, side, reverse, any");
    ImGui::BulletText("rlt ballmech mode <méca> <court|parcours> : départ de l'exercice");
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
    ui::textColored(kAccent, bm::progressRule());
    if (ImGui::CollapsingHeader("Lexique##bm_lexicon")) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", bm::lexicon());
        ImGui::Unindent();
    }
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
            drawGuide(ctx, st, profile, worldControl, unit);
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
