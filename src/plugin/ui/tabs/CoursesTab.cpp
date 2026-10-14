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
#include "core/modules/courses/Tutorials.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

// Text typed in the "course code" field. ImGui needs a buffer that lives across frames; it is only touched by
// the render thread, inside this tab. The preview (type, difficulty, real ring count) is computed only when the
// text changes: the generator clamps the ring count carried by the code (per type and difficulty).
struct CodeField {
    char text[64] = {};
    std::string previewFor;
    bool previewValid = false;
    std::string preview;
};

CoursesKind kindOfType(courses::CourseType t) {
    switch (t) {
    case courses::CourseType::GroundWall: return CoursesKind::RingsGround;
    case courses::CourseType::AirDribble: return CoursesKind::RingsAirDribble;
    case courses::CourseType::DribbleGates: return CoursesKind::Slalom;
    default: return CoursesKind::RingsAir;
    }
}

void refreshPreview(CodeField& f, const std::string& typed) {
    if (typed == f.previewFor) return;
    f.previewFor = typed;
    f.previewValid = false;
    f.preview.clear();
    const auto decoded = courses::parseCourseCode(typed);
    if (!decoded) return;
    const courses::Course c = courses::generateCourse(decoded->seed, decoded->type, decoded->difficultyPct, nullptr, decoded->version);
    if (c.rings.empty()) return;
    const bool gates = decoded->type == courses::CourseType::DribbleGates;
    const size_t n = c.rings.size();
    f.previewValid = true;
    f.preview = std::string("Code valide : ") + CoursesModule::kindTitle(kindOfType(decoded->type)) + ", niveau " +
                std::to_string(CoursesModule::levelNumber(decoded->difficultyPct)) + "/10, " + std::to_string(n) +
                (gates ? (n > 1 ? " portes" : " porte") : (n > 1 ? " anneaux" : " anneau")) +
                (decoded->version < courses::kGeneratorVersion ? " (ancien code : le parcours d'origine)" : "");
}

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
    int fullDifficulty[kCoursesKindCount] = {};
    bool fullMode[kCoursesKindCount] = {};
    int pickupNiveau = 1;
    PlayerProfile profile;
    KeyStyle keyStyle = KeyStyle::Auto;
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
    d.keyStyle = style;
    d.profile = ctx.engine.profile();
    d.resetPrompt = actionPrompt(d.profile, GameAction::ResetShot, style);
    d.ballCamKey = actionKeyLabel(d.profile, GameAction::BallCam, style);
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
    for (int i = 0; i < kCoursesKindCount; ++i) {
        const auto k = static_cast<CoursesKind>(i);
        d.difficulty[i] = mod->difficultyFor(k);
        d.fullDifficulty[i] = mod->fullDifficultyFor(k);
        d.fullMode[i] = mod->fullMode(k);
    }
    d.pickupNiveau = mod->pickupNiveau();
    return d;
}

constexpr Color kAccent{255, 199, 64, 255};

// Tutorial of one exercise: the steps (what to do, why, how to know it is right), the in-game panel, niveau 1, the
// "parcours complet", and the numbers under "Détails techniques".
void drawTutorial(const UiContext& ctx, const TabData& d, CoursesKind k) {
    const courses::Tutorial& t = courses::tutorial(k);
    auto fill = [&](const char* text) { return courses::fillButtons(text, d.profile, d.keyStyle); };
    ImGui::TextWrapped("%s", fill(t.intro).c_str());
    if (t.before && *t.before) {
        ui::textColored(kAccent, "Conseillé avant :");
        ImGui::SameLine();
        ImGui::TextWrapped("%s", t.before);
    }
    ui::textColored(kAccent, "Les étapes");
    for (const auto& st : t.steps) {
        ImGui::TextWrapped("%s", fill(st.todo).c_str());
        ImGui::Indent();
        ImGui::TextWrapped("Pourquoi : %s", fill(st.why).c_str());
        ImGui::TextWrapped("C'est bon quand : %s", fill(st.ok).c_str());
        ImGui::Unindent();
    }
    ui::textColored(kAccent, "Panneau en jeu");
    for (const char* line : t.panel) ImGui::BulletText("%s", line);
    ui::textColored(kAccent, "Niveau 1");
    ImGui::TextWrapped("%s", fill(t.level1).c_str());
    ui::textColored(kAccent, CoursesModule::kindFullTitle(k));
    ImGui::TextWrapped("%s", fill(t.full).c_str());
    for (size_t i = 0; i < t.fullSteps.size(); ++i) ImGui::BulletText("%d. %s", static_cast<int>(i + 1), t.fullSteps[i]);
    if (CoursesModule::kindUsesPickup(k)) {
        ui::textColored(kAccent, "Le ramassage");
        ImGui::TextWrapped("%s", courses::pickupText());
        ImGui::TextDisabled("Niveau du ramassage : %d/10", d.pickupNiveau);
        ImGui::SameLine();
        if (ImGui::SmallButton("Ramassage au niveau 1")) ctx.post("courses", "reset_level", {"pickup"});
    }
    const std::string techId = std::string("Détails techniques##co_tech_") + CoursesModule::kindKey(k);
    if (ImGui::CollapsingHeader(techId.c_str())) {
        ImGui::Indent();
        for (const char* line : t.tech) ImGui::TextWrapped("%s", line);
        if (CoursesModule::kindUsesPickup(k))
            for (const char* line : courses::pickupTech()) ImGui::TextWrapped("%s", line);
        ImGui::Unindent();
    }
}

void quickSetting(const UiContext& ctx, const Settings& current, const char* cvar) {
    if (const SettingDef* def = ctx.engine.schema().find(cvar)) ui::settingWidget(ctx, *def, current);
}

// Difficulty tag colours (light enough to read on the dark menu).
Color tierColor(CoursesKind k) {
    switch (CoursesModule::kindTier(k)) {
    case 0: return Color(90, 230, 120, 255);
    case 1: return Color(255, 200, 80, 255);
    default: return Color(255, 110, 90, 255);
    }
}

void exerciseButton(const UiContext& ctx, const TabData& d, CoursesKind k) {
    const int i = static_cast<int>(k);
    ImGui::PushID(i);
    const char* command = CoursesModule::kindIsRings(k) ? "rings" : "control";
    if (ImGui::Button(CoursesModule::kindTitle(k), ImVec2(190.f, 0.f))) ctx.post("courses", command, {CoursesModule::kindKey(k)});
    ImGui::SameLine();
    ui::textColored(tierColor(k), CoursesModule::kindTierLabel(k));
    ImGui::SameLine();
    const int level = d.fullMode[i] ? d.fullDifficulty[i] : d.difficulty[i];
    ImGui::TextDisabled("niveau %d/10", CoursesModule::levelNumber(level));
    ImGui::SameLine();
    if (ImGui::SmallButton("Revenir au niveau 1")) ctx.post("courses", "reset_level", {CoursesModule::kindKey(k)});
    ImGui::TextWrapped("%s", CoursesModule::kindHelp(k));
    // Start: the exercise alone, or with every step before it ("parcours complet").
    const char* modes[2] = {"Départ normal", CoursesModule::kindFullTitle(k)};
    int mode = d.fullMode[i] ? 1 : 0;
    ImGui::SetNextItemWidth(320.f);
    if (ImGui::Combo("Départ", &mode, modes, 2))
        ctx.post("courses", "mode", {CoursesModule::kindKey(k), mode == 1 ? "parcours" : "normal"});
    ui::helpMarker("Parcours complet : toutes les étapes avant l'exercice (décoller, monter au mur, ramasser la balle...), "
                   "vérifiées une par une. Il a son propre niveau.");
    // Soft prerequisites: the slalom and the air dribble rings come after the roof dribble.
    if ((k == CoursesKind::Slalom || k == CoursesKind::RingsAirDribble) &&
        CoursesModule::levelNumber(d.difficulty[static_cast<int>(CoursesKind::Dribble)]) < 3)
        ui::textColored(colors::Orange, "Conseil : monte d'abord le Dribble sur le toit au niveau 3.");
    if (ImGui::CollapsingHeader("Tutoriel")) {
        ImGui::Indent();
        drawTutorial(ctx, d, k);
        ImGui::Unindent();
    }
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
    ui::textColored(kAccent, courses::progressRule());
    ImGui::TextWrapped("%s", courses::pathAdvice());
    if (ImGui::CollapsingHeader("Lexique##co_lexicon")) {
        ImGui::Indent();
        ImGui::TextWrapped("%s", courses::lexicon());
        ImGui::Unindent();
    }

    const bool workshopCourse = d.game.category == MapCategory::Rings || d.game.category == MapCategory::Dribble ||
                                d.game.category == MapCategory::AirDribble;
    if (workshopCourse)
        ui::textColored(colors::Cyan, "Map workshop de parcours détectée : le module Parcours reste en pause ici, profite "
                                      "de la map ! Ses stats sont dans la page Maps.");
    else if (!d.worldControl)
        ui::textColored(colors::Orange, "Exercices indisponibles ici : lance le freeplay pour jouer les parcours.");
    if (!d.cfg.enabled) ui::textColored(colors::Orange, "Module désactivé : active-le dans les réglages en bas de la page.");

    // ---- Live status --------------------------------------------------------------------------------------------
    ui::separatorText("En cours");
    const CoursesStatus& s = d.status;
    if (s.active) {
        ImGui::Text("%s : %s", s.title.c_str(), phaseColorLabel(s));
        if (!s.stepLine.empty()) ui::textColored(kAccent, "Étape " + s.stepLine);
        ImGui::Text("Niveau %d/10%s", CoursesModule::levelNumber(s.difficulty), s.fromCode ? " (fixé par le code)" : "");
        if (!s.code.empty()) {
            ImGui::Text("Code : %s", s.code.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copier##live")) ImGui::SetClipboardText(s.code.c_str());
        }
        if (s.total > 0) {
            ImGui::Text("%s %d/%d   Temps %s   Ratés %d", s.kind == CoursesKind::Slalom ? "Porte" : "Anneau",
                        std::min(s.next + 1, s.total), s.total, formatTime(s.time).c_str(), s.misses);
            ui::progressBar(static_cast<float>(s.next) / static_cast<float>(s.total), "");
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
            // Title above the chart, not over it: light text on the amber bars is unreadable.
            ImGui::TextUnformatted(d.last.kind == CoursesKind::Slalom ? "Temps par porte (s)" : "Temps par anneau (s)");
            ImGui::PlotHistogram("##segments", d.last.segments.data(), static_cast<int>(d.last.segments.size()), 0,
                                 nullptr, 0.f, FLT_MAX, ImVec2(-1.f, 70.f));
            ImGui::TextDisabled("Les barres hautes montrent où tu perds du temps.");
        }
    }

    // ---- Exercises (easiest first) ------------------------------------------------------------------------------
    ui::separatorText("Exercices");
    ImGui::TextDisabled("Chaque essai commence voiture bloquée : appuie sur accélérer quand tu es prêt, puis 3, 2, 1.");
    for (CoursesKind k : CoursesModule::kindsByTier()) exerciseButton(ctx, d, k);
    ImGui::TextDisabled("Astuce : coupe la caméra balle%s pour les anneaux en voiture.",
                        d.ballCamKey.empty() ? "" : (" (" + d.ballCamKey + ")").c_str());
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
            ui::progressBar(static_cast<float>(l.levelNumber) / 10.f, std::to_string(l.levelNumber) + "/10");
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
    refreshPreview(field, typed);
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
        if (field.previewValid) {
            ui::textColored(colors::Green, field.preview);
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
        const char* heads[6] = {"Code", "Type", "Niveau", "Record", "Essais", ""};
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
            ImGui::Text("%d/10", CoursesModule::levelNumber(pb.difficulty));
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
        ImGui::BulletText("Plus d'un quart des anneaux ratés : l'essai compte comme raté, même arrivé au bout.");
        ImGui::BulletText("Record : pas de record avec des anneaux ratés si la pénalité est réglée sous 1 s.");
        ImGui::BulletText("Le panneau en haut à gauche montre ton temps et ton écart avec ton record à chaque anneau.");
        ImGui::BulletText("Médailles : or sous le temps cible, argent jusqu'à +25 %%, bronze jusqu'à +50 %%.");
        ImGui::BulletText("Air dribble et slalom : c'est la balle qui doit passer, et elle ne doit pas toucher le sol.");
        ImGui::BulletText("Slalom : balle sur le toit, et passage propre au centre de chaque porte (sans frôler les poteaux).");
        ImGui::BulletText("Rattrapage : suis le cercle au sol, amortis la balle et garde-la sur le toit.");
        ImGui::BulletText("Parcours complet : chaque étape passe au vert sur le panneau. S'il en manque une, le panneau dit "
                          "laquelle et comment la réussir.");
        ImGui::BulletText("Revenir au niveau 1 : remet l'exercice (et son parcours complet) au niveau 1.");
    }
    if (ImGui::CollapsingHeader("Commandes console##courses_cmds")) {
        ImGui::BulletText("rlt courses rings <air|ground|airdribble> [code]");
        ImGui::BulletText("rlt courses control <dribble|slalom|catch>");
        ImGui::BulletText("rlt courses play <code>");
        ImGui::BulletText("rlt courses restart  /  next  /  stop");
        ImGui::BulletText("rlt courses code (affiche le code dans le chat)");
        ImGui::BulletText("rlt courses clearpb [code] (efface un record)");
        ImGui::BulletText("rlt courses mode <air|ground|airdribble|dribble|slalom|catch> <normal|parcours>");
        ImGui::BulletText("rlt courses reset_level <exercice|pickup> (revenir au niveau 1)");
    }

    // ---- Settings -----------------------------------------------------------------------------------------------
    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "courses");
}

} // namespace rlt
