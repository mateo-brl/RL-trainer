// "Parcours" tab: start the virtual courses (rings, control drills), live status, course codes, records, settings.
// Render thread: module state is copied under the engine lock, actions go through commands (ctx.post).

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/modules/courses/CoursesModule.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

// Text typed in the "course code" field. ImGui needs a buffer that lives across frames; it is only touched by
// the render thread, inside this tab.
struct CodeField {
    char text[64] = {};
};

CodeField& codeField() {
    static CodeField field;
    return field;
}

struct TabData {
    bool found = false;
    CoursesStatus status;
    CoursesLastAttempt last;
    std::vector<CoursesPbView> pbs;
    std::vector<CoursesLevelView> levels;
    std::string code;
    CoursesConfig cfg;
    float dribbleBest = 0.f;
    float catchBest = 0.f;
    int difficulty[kCoursesKindCount] = {};
    GameContext game;
    bool worldControl = false;
    std::string owner;
    std::string resetPrompt;
    std::string ballCamKey;
};

TabData readData(const UiContext& ctx) {
    TabData d;
    auto lk = ctx.engine.lock();
    auto* mod = ctx.engine.get<CoursesModule>();
    d.game = ctx.engine.context();
    d.worldControl = ctx.engine.worldControlAllowed();
    d.owner = ctx.engine.activityOwner();
    const KeyStyle style = keyStyleFromIndex(ctx.engine.settings().profile.buttonStyle);
    d.resetPrompt = actionPrompt(ctx.engine.profile(), GameAction::ResetShot, style);
    d.ballCamKey = actionKeyLabel(ctx.engine.profile(), GameAction::BallCam, style);
    if (!mod) return d;
    d.found = true;
    d.status = mod->status();
    d.last = mod->lastAttempt();
    d.pbs = mod->personalBests();
    d.levels = mod->levels();
    d.code = mod->currentCode();
    d.cfg = mod->config();
    d.dribbleBest = mod->dribbleBest();
    d.catchBest = mod->catchBest();
    for (int i = 0; i < kCoursesKindCount; ++i) d.difficulty[i] = mod->difficultyFor(static_cast<CoursesKind>(i));
    return d;
}

void quickSetting(const UiContext& ctx, const Settings& current, const char* cvar) {
    if (const SettingDef* def = ctx.engine.schema().find(cvar)) ui::settingWidget(ctx, *def, current);
}

void exerciseButton(const UiContext& ctx, const TabData& d, CoursesKind k, const char* command) {
    ImGui::PushID(static_cast<int>(k));
    if (ImGui::Button(CoursesModule::kindTitle(k), ImVec2(190.f, 0.f))) ctx.post("courses", command, {CoursesModule::kindKey(k)});
    ImGui::SameLine();
    ImGui::TextDisabled("niv. %d", d.difficulty[static_cast<int>(k)]);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", CoursesModule::kindHelp(k));
    ImGui::PopID();
}

const char* phaseColorLabel(const CoursesStatus& s) { return s.phaseLabel.c_str(); }

} // namespace

void renderCoursesTab(UiContext& ctx) {
    const TabData d = readData(ctx);
    if (!d.found) {
        ImGui::TextDisabled("Module Parcours indisponible.");
        return;
    }

    ImGui::TextWrapped("Des maps d'entraînement intégrées au freeplay, sans rien télécharger : anneaux à traverser en vol, "
                       "arches au sol et sur les murs, air dribble, et exercices de contrôle de balle. Chaque parcours a "
                       "un code à partager, et ton record est gardé pour chaque code.");

    const bool workshopCourse = d.game.category == MapCategory::Rings || d.game.category == MapCategory::Dribble ||
                                d.game.category == MapCategory::AirDribble;
    if (workshopCourse)
        ui::textColored(colors::Cyan, "Map workshop de parcours détectée : le module Parcours reste en pause ici, profite "
                                      "de la map ! Ses stats sont dans l'onglet Maps.");
    else if (!d.worldControl)
        ui::textColored(colors::Orange, "Exercices indisponibles ici : lance le freeplay pour jouer les parcours.");
    if (!d.cfg.enabled) ui::textColored(colors::Orange, "Module désactivé : active-le dans les réglages en bas de l'onglet.");

    // ---- Live status --------------------------------------------------------------------------------------------
    ui::separatorText("En cours");
    const CoursesStatus& s = d.status;
    if (s.active) {
        ImGui::Text("%s : %s", s.title.c_str(), phaseColorLabel(s));
        ImGui::Text("Difficulté %d%s", s.difficulty, s.fromCode ? " (fixée par le code)" : "");
        if (!s.code.empty()) {
            ImGui::Text("Code : %s", s.code.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copier##live")) ImGui::SetClipboardText(s.code.c_str());
        }
        if (s.total > 0) {
            ImGui::Text("Anneau %d/%d   Temps %s   Ratés %d", std::min(s.next + 1, s.total), s.total, formatTime(s.time).c_str(),
                        s.misses);
            ImGui::ProgressBar(static_cast<float>(s.next) / static_cast<float>(s.total), ImVec2(-1.f, 0.f));
            if (s.hasPb) ImGui::Text("Record : %s", formatTime(s.pbTime).c_str());
            else ImGui::TextDisabled("Pas encore de record sur ce parcours.");
            if (s.parTime > 0.f) {
                ImGui::SameLine();
                ImGui::TextDisabled("  Temps cible : %s (médaille d'or)", formatTime(s.parTime).c_str());
            }
            if (s.estBoost > 1.f && !d.cfg.unlimitedBoost)
                ImGui::TextDisabled("Boost estimé pour ce parcours : %d%s", static_cast<int>(s.estBoost + 0.5f),
                                    d.cfg.boostRefills ? " (avec les anneaux de recharge)" : " (sans recharge : économise-le)");
        } else {
            ImGui::Text("Temps %s", formatTime(s.time).c_str());
        }
        if (ImGui::Button("Rejouer")) ctx.post("courses", "restart");
        ImGui::SameLine();
        if (ImGui::Button(CoursesModule::kindUsesCourse(s.kind) ? "Nouveau parcours" : "Nouvel essai")) ctx.post("courses", "next");
        ImGui::SameLine();
        if (ImGui::Button("Arrêter")) ctx.post("courses", "stop");
        ImGui::TextDisabled("En jeu : %s pour recommencer l'essai.", d.resetPrompt.c_str());
    } else {
        ImGui::TextDisabled("Aucun parcours en cours. Choisis un exercice ci-dessous.");
        if (!d.owner.empty() && d.owner != "courses")
            ImGui::TextDisabled("Un autre exercice tourne : lancer un parcours l'arrêtera.");
    }

    // ---- Last attempt -------------------------------------------------------------------------------------------
    if (d.last.valid) {
        ui::separatorText("Dernier essai");
        ui::textColored(d.last.success ? colors::Green : colors::Red, d.last.title + " : " + d.last.headline);
        ImGui::Text("Score %d - %s", static_cast<int>(d.last.score + 0.5f), d.last.verdict.c_str());
        if (d.last.segments.size() >= 2) {
            ImGui::PlotHistogram("##segments", d.last.segments.data(), static_cast<int>(d.last.segments.size()), 0,
                                 "Temps par anneau (s)", 0.f, FLT_MAX, ImVec2(-1.f, 70.f));
            ImGui::TextDisabled("Les barres hautes montrent où tu perds du temps.");
        }
    }

    // ---- Exercises ----------------------------------------------------------------------------------------------
    ui::separatorText("Anneaux");
    exerciseButton(ctx, d, CoursesKind::RingsAir, "rings");
    exerciseButton(ctx, d, CoursesKind::RingsGround, "rings");
    exerciseButton(ctx, d, CoursesKind::RingsAirDribble, "rings");
    ImGui::TextDisabled("Astuce : coupe la caméra balle%s pour les anneaux en voiture.",
                        d.ballCamKey.empty() ? "" : (" (" + d.ballCamKey + ")").c_str());

    ui::separatorText("Contrôle de balle");
    exerciseButton(ctx, d, CoursesKind::Dribble, "control");
    exerciseButton(ctx, d, CoursesKind::Slalom, "control");
    exerciseButton(ctx, d, CoursesKind::Catch, "control");
    if (d.dribbleBest > 0.f) ImGui::Text("Record de dribble : %s s", formatNumber(d.dribbleBest, 1).c_str());
    if (d.catchBest > 0.f) {
        if (d.dribbleBest > 0.f) ImGui::SameLine();
        ImGui::Text("   Rattrapage le plus rapide : %s s", formatNumber(d.catchBest, 2).c_str());
    }

    // ---- Difficulty ---------------------------------------------------------------------------------------------
    ui::separatorText("Difficulté");
    {
        const Settings current = ctx.settings();
        ImGui::PushID("courses_quick");
        quickSetting(ctx, current, "rlt_co_difficulty_mode");
        if (current.courses.difficultyMode == 5) quickSetting(ctx, current, "rlt_co_difficulty");
        quickSetting(ctx, current, "rlt_co_ring_count");
        quickSetting(ctx, current, "rlt_co_miss_policy");
        ImGui::PopID();
    }
    if (d.cfg.difficultyMode == 0) {
        ImGui::TextDisabled("Mode Auto : le niveau monte quand tu réussis, baisse quand tu échoues (75 %% de réussite visée).");
        ImGui::Columns(4, "##courses_levels", true);
        ImGui::Text("Exercice");
        ImGui::NextColumn();
        ImGui::Text("Niveau");
        ImGui::NextColumn();
        ImGui::Text("Essais");
        ImGui::NextColumn();
        ImGui::Text("Réussis");
        ImGui::NextColumn();
        ImGui::Separator();
        for (const auto& l : d.levels) {
            ImGui::Text("%s", l.title.c_str());
            ImGui::NextColumn();
            ImGui::ProgressBar(static_cast<float>(l.level) / 100.f, ImVec2(-1.f, 0.f), std::to_string(l.level).c_str());
            ImGui::NextColumn();
            ImGui::Text("%d", l.attempts);
            ImGui::NextColumn();
            ImGui::Text("%d", l.successes);
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    // ---- Course code --------------------------------------------------------------------------------------------
    ui::separatorText("Code de parcours");
    ImGui::TextWrapped("Un code redonne exactement le même parcours (même difficulté, même nombre d'anneaux). "
                       "Partage-le pour comparer vos temps.");
    CodeField& field = codeField();
    ImGui::PushItemWidth(260.f);
    const bool enter = ImGui::InputText("##courses_code", field.text, sizeof(field.text),
                                        ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    const std::string typed = field.text;
    const auto decoded = courses::parseCourseCode(typed);
    if ((ImGui::Button("Jouer ce code") || enter) && !typed.empty()) ctx.post("courses", "play", {typed});
    ImGui::SameLine();
    if (ImGui::Button("Coller")) {
        const char* clip = ImGui::GetClipboardText();
        if (clip) std::snprintf(field.text, sizeof(field.text), "%s", clip);
    }
    ImGui::SameLine();
    if (ImGui::Button("Copier le code") && !d.code.empty()) ImGui::SetClipboardText(d.code.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copie le code du parcours actuel (ou du dernier joué).");
    if (!typed.empty()) {
        if (decoded) {
            const int kind = decoded->type == courses::CourseType::GroundWall     ? 1
                             : decoded->type == courses::CourseType::AirDribble   ? 2
                             : decoded->type == courses::CourseType::DribbleGates ? 4
                                                                                  : 0;
            const int rings = courses::seedRingCount(decoded->seed);
            ui::textColored(colors::Green, std::string("Code valide : ") + CoursesModule::kindTitle(static_cast<CoursesKind>(kind)) +
                                               ", difficulté " + std::to_string(decoded->difficultyPct) +
                                               (rings > 0 ? ", " + std::to_string(rings) + " anneaux" : std::string()));
        } else {
            ui::textColored(colors::Orange, "Code invalide : vérifie chaque caractère (format RLT1-A050-XXXXXXXXXXXXX-X).");
        }
    }
    if (!d.code.empty()) ImGui::TextDisabled("Dernier code joué : %s", d.code.c_str());

    // ---- Records ------------------------------------------------------------------------------------------------
    ui::separatorText("Records (20 derniers parcours)");
    if (d.pbs.empty()) {
        ImGui::TextDisabled("Termine un parcours pour enregistrer ton premier record.");
    } else {
        ImGui::Columns(6, "##courses_pbs", true);
        const char* heads[6] = {"Code", "Type", "Niv.", "Record", "Essais", ""};
        for (const char* h : heads) {
            ImGui::Text("%s", h);
            ImGui::NextColumn();
        }
        ImGui::Separator();
        int row = 0;
        for (const auto& pb : d.pbs) {
            ImGui::PushID(row++);
            ImGui::Text("%s", pb.code.c_str());
            ImGui::NextColumn();
            ImGui::Text("%s", pb.typeLabel.c_str());
            ImGui::NextColumn();
            ImGui::Text("%d", pb.difficulty);
            ImGui::NextColumn();
            if (pb.time > 0.f) ImGui::Text("%s", formatTime(pb.time).c_str());
            else ImGui::TextDisabled("-");
            ImGui::NextColumn();
            ImGui::Text("%d (%d fini%s)", pb.attempts, pb.finishes, pb.finishes > 1 ? "s" : "");
            ImGui::NextColumn();
            if (ImGui::SmallButton("Jouer")) ctx.post("courses", "play", {pb.code});
            ImGui::SameLine();
            if (ImGui::SmallButton("Copier")) ImGui::SetClipboardText(pb.code.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Effacer")) ctx.post("courses", "clearpb", {pb.code});
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // ---- Help ---------------------------------------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Comment ça marche##courses_help")) {
        ImGui::BulletText("Passe les anneaux dans l'ordre : le prochain est en vert, épais, avec son numéro.");
        ImGui::BulletText("Anneau raté : pénalité de temps, retour obligatoire ou échec, selon le réglage.");
        ImGui::BulletText("Le panneau en haut à gauche montre ton temps et ton écart avec ton record à chaque anneau.");
        ImGui::BulletText("Médailles : or sous le temps cible, argent jusqu'à +25 %%, bronze jusqu'à +50 %%.");
        ImGui::BulletText("Air dribble et slalom : c'est la balle qui doit passer, et elle ne doit pas toucher le sol.");
        ImGui::BulletText("Rattrapage : suis le cercle au sol, amortis la balle et garde-la sur le toit.");
    }
    if (ImGui::CollapsingHeader("Commandes console##courses_cmds")) {
        ImGui::BulletText("rlt courses rings <air|ground|airdribble> [code]");
        ImGui::BulletText("rlt courses control <dribble|slalom|catch>");
        ImGui::BulletText("rlt courses play <code>");
        ImGui::BulletText("rlt courses restart  /  next  /  stop");
        ImGui::BulletText("rlt courses code (affiche le code dans le chat)");
        ImGui::BulletText("rlt courses clearpb [code] (efface un record)");
    }

    // ---- Settings -----------------------------------------------------------------------------------------------
    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "courses");
}

} // namespace rlt
