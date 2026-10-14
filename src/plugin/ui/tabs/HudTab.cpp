// "HUD" page: which panel the HUD shows right now, the general HUD options, the trajectories of every module (off by
// default, one place to turn them back on), the menu key, and the position, size and colours of every element drawn
// in game, each with its own reset. Render thread: engine state is copied under the engine lock, settings change
// through cvars only.
#include <algorithm>
#include <string>
#include <vector>

#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

constexpr Color kOk{90, 220, 120, 255};
constexpr Color kWarn{255, 170, 60, 255};

// One setting of a HUD element. `label` replaces the schema label so that every element reads the same way
// (nullptr: schema label).
struct HudSetting {
    const char* cvar;
    const char* label;
};

struct HudElement {
    const char* id;
    const char* title;
    std::vector<HudSetting> settings;  // show/hide, position, size, colours; names missing from the schema are skipped
    const char* note;                  // "" = none
};

const std::vector<HudElement>& hudElements() {
    static const std::vector<HudElement> elements = {
        {"firsttouch",
         "Première touche",
         {{"rlt_ft_hud", "Afficher"},
          {"rlt_ft_hud_x", "Position X"},
          {"rlt_ft_hud_y", "Position Y"},
          {"rlt_ft_hud_size", "Taille"},
          {"rlt_ft_color_good", nullptr},
          {"rlt_ft_color_ok", nullptr},
          {"rlt_ft_color_bad", nullptr}},
         ""},
        {"firsttouch_drill",
         "Exercice première touche",
         {{"rlt_ft_drill_hud_x", "Position X"}, {"rlt_ft_drill_hud_y", "Position Y"}},
         "Visible pendant un exercice de première touche. Sa taille suit celle du panneau Première touche."},
        {"mechanics",
         "Mécas mouvement",
         {{"rlt_me_hud", "Afficher"},
          {"rlt_me_hud_x", "Position X"},
          {"rlt_me_hud_y", "Position Y"},
          {"rlt_me_hud_scale", "Taille"}},
         ""},
        {"ballmech",
         "Mécas balle",
         {{"rlt_bm_hud", "Afficher"},
          {"rlt_bm_hud_x", "Position X"},
          {"rlt_bm_hud_y", "Position Y"},
          {"rlt_bm_hud_scale", "Taille"}},
         ""},
        {"ballmech_drill",
         "Exercice mécas balle",
         {{"rlt_bm_drill_x", "Position X"}, {"rlt_bm_drill_y", "Position Y"}},
         "Visible pendant un exercice de mécas balle. Sa taille suit celle du panneau Mécas balle."},
        {"kickoff",
         "Kickoff",
         {{"rlt_ko_hud", "Afficher"},
          {"rlt_ko_hud_x", "Position X"},
          {"rlt_ko_hud_y", "Position Y"},
          {"rlt_ko_hud_scale", "Taille"},
          {"rlt_ko_big_countdown", nullptr},
          {"rlt_ko_countdown_y", nullptr}},
         ""},
        {"courses",
         "Parcours",
         {{"rlt_co_hud", "Afficher"},
          {"rlt_co_hud_x", "Position X"},
          {"rlt_co_hud_y", "Position Y"},
          {"rlt_co_hud_scale", "Taille"}},
         ""},
        {"shooting",
         "Tirs & flicks",
         {{"rlt_sh_hud_mode", "Afficher"},
          {"rlt_sh_hud_x", "Position X"},
          {"rlt_sh_hud_y", "Position Y"},
          {"rlt_sh_hud_scale", "Taille"}},
         ""},
        {"prediction",
         "Prédiction",
         {{"rlt_pr_panel", "Afficher"},
          {"rlt_pr_panel_x", "Position X"},
          {"rlt_pr_panel_y", "Position Y"},
          {"rlt_pr_text_scale", "Taille"}},
         "Masqué quand « Un seul panneau à la fois » est activé. La taille sert aussi aux textes dans le terrain."},
        {"maps",
         "Maps",
         {{"rlt_map_hud", "Afficher"},
          {"rlt_map_hud_x", "Position X"},
          {"rlt_map_hud_y", "Position Y"},
          {"rlt_map_hud_scale", "Taille"},
          {"rlt_map_color_ahead", nullptr},
          {"rlt_map_color_behind", nullptr}},
         "Reste affiché sur une map perso, sauf pendant un exercice."},
        {"coach",
         "Carte du coach",
         {{"rlt_coach_card", "Afficher"},
          {"rlt_coach_card_x", "Position X"},
          {"rlt_coach_card_y", "Position Y"},
          {"rlt_coach_card_scale", "Taille"}},
         "Masquée quand « Un seul panneau à la fois » est activé."},
        {"speed",
         "Écart de vitesse",
         {{"rlt_ov_speed_enabled", "Afficher"},
          {"rlt_ov_speed_x", "Position X"},
          {"rlt_ov_speed_y", "Position Y"},
          {"rlt_ov_text_scale", "Taille"},
          {"rlt_ov_good_color", nullptr},
          {"rlt_ov_alright_color", nullptr},
          {"rlt_ov_bad_color", nullptr}},
         "Toujours affiché quand il est activé. Sa taille sert aussi au chrono."},
        {"timer",
         "Chrono",
         {{"rlt_ov_timer_enabled", "Afficher"},
          {"rlt_ov_timer_x", "Position X"},
          {"rlt_ov_timer_y", "Position Y"},
          {"rlt_ov_timer_color", nullptr}},
         "Toujours affiché quand il est activé. Sa taille suit celle de l'écart de vitesse."},
    };
    return elements;
}

// Trajectory and prediction drawings of every module (ball path, landing and impact markers, guide lines), all off by
// default since v1.0.8: the player found them gadget-like and far from what a match looks like. Listed by cvar name;
// names missing from the schema are skipped.
const std::vector<const char*>& trajectoryCvars() {
    static const std::vector<const char*> names = {
        "rlt_pr_enabled",         // Prédiction: the whole module (path, bounces, landing, touch zone, interception)
        "rlt_ft_drill_path",      // Première touche: ball path during the get-ready hold
        "rlt_co_guide_line",      // Parcours: guide lines to the next ring and to the landing point
        "rlt_sh_show_prediction", // Tirs & flicks: predicted impact marker on the goal line
        "rlt_sh_free_analysis",   // Tirs & flicks: predicted impact of shots outside the exercises
        "rlt_sh_floor_marks",     // Tirs & flicks: next bounces, meeting point and run-up point on the floor
        "rlt_bm_draw_trajectory", // Mécas balle: path of the last graded shot
        "rlt_bm_draw_guide",      // Mécas balle: ceiling start guide (ball path and meeting point)
        "rlt_ko_show_path",       // Kickoff: lines to the ball (player and ghost)
        "rlt_me_guides",          // Mécas mouvement: 3D drill markers (line to the ball, lane, target)
    };
    return names;
}

// General HUD options, in display order. The menu key is not part of the HUD reset.
const std::vector<std::string>& generalHudCvars() {
    static const std::vector<std::string> names = {"rlt_hud_compact", "rlt_hud_focus",   "rlt_hud_text",
                                                   "rlt_hud_opacity", "rlt_hud_scale",   "rlt_hud_accents"};
    return names;
}

std::vector<std::string> elementCvars(const HudElement& e) {
    std::vector<std::string> out;
    for (const HudSetting& s : e.settings) out.emplace_back(s.cvar);
    return out;
}

void hoverText(const char* text) {
    if (!ImGui::IsItemHovered()) return;
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

void wrappedDisabled(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// Draws the widget of `cvar` with an optional label. False when the cvar does not exist.
bool cvarWidget(const UiContext& ctx, const Settings& current, const char* cvar, const char* label = nullptr) {
    const SettingDef* def = ctx.engine.schema().find(cvar);
    if (!def) return false;
    if (label) {
        SettingDef copy = *def;
        copy.label = label;
        ui::settingWidget(ctx, copy, current);
    } else {
        ui::settingWidget(ctx, *def, current);
    }
    return true;
}

std::string moduleTitle(const Engine& engine, const std::string& id) {
    if (id == "overlay") return "Écart de vitesse";
    const Module* m = engine.find(id);
    return m ? m->title() : id;
}

void renderCurrentPanel(const UiContext& ctx, const Settings& current) {
    std::string shown;
    {
        auto lk = ctx.engine.lock();
        const std::string owner = ctx.engine.activityOwner();
        const std::string id = owner.empty() ? ctx.engine.hudFocusModule() : owner;
        shown = id.empty() ? "aucun" : moduleTitle(ctx.engine, id);
    }
    if (current.general.hudFocus) {
        ImGui::Text("Panneau affiché en ce moment : %s", shown.c_str());
        wrappedDisabled("Pendant un exercice, seul son panneau reste à l'écran. Sinon, celui de ton dernier essai. "
                        "L'écart de vitesse, le chrono et le panneau des maps restent toujours affichés.");
    } else {
        ImGui::TextUnformatted("Panneau affiché en ce moment : tous les panneaux activés");
        wrappedDisabled("« Un seul panneau à la fois » est désactivé : tous les panneaux s'affichent ensemble.");
    }
}

void renderTrajectories(const UiContext& ctx, const Settings& current) {
    std::vector<const SettingDef*> defs;
    for (const char* name : trajectoryCvars())
        if (const SettingDef* d = ctx.engine.schema().find(name)) defs.push_back(d);
    if (defs.empty()) return;
    std::vector<std::string> labels;
    {
        auto lk = ctx.engine.lock();
        for (const SettingDef* d : defs) labels.push_back(moduleTitle(ctx.engine, d->module) + " : " + d->label);
    }
    int shown = 0;
    for (const SettingDef* d : defs) {
        bool on = false;
        if (d->read) parseBoolValue(d->read(current), on);
        shown += on ? 1 : 0;
    }
    const int total = static_cast<int>(defs.size());

    ui::separatorText("Trajectoires");
    bool all = shown == total;
    if (ImGui::Checkbox("Afficher les trajectoires##hud_traj_all", &all))
        for (const SettingDef* d : defs) ctx.setCvar(d->cvar, boolToCvar(all));
    hoverText("Active ou coupe d'un coup toutes les trajectoires de la liste.");
    ImGui::SameLine();
    if (shown == 0) ImGui::TextDisabled("(toutes coupées)");
    else ImGui::TextDisabled("(%d sur %d affichées)", shown, total);
    wrappedDisabled("Chemin prévu de la balle, points d'impact et lignes guides tracés sur le terrain. Coupés par "
                    "défaut : coche ceux qui t'aident.");
    ImGui::PushID("hud_traj");  // the same checkboxes are also in each module's page
    ImGui::Indent();
    for (size_t i = 0; i < defs.size(); ++i) cvarWidget(ctx, current, defs[i]->cvar.c_str(), labels[i].c_str());
    ImGui::Unindent();
    // Targets the exercise grades the player on reaching: never switched by "Afficher les trajectoires" (not in
    // trajectoryCvars), the catch point keeps its own checkbox here.
    wrappedDisabled("Gardés car l'exercice te demande d'y aller : point de chute (Parcours, rattrapage) et cercles de "
                    "Première touche.");
    cvarWidget(ctx, current, "rlt_co_catch_marker", "Parcours : point de chute");
    ImGui::PopID();
}

void renderMenuKey(const UiContext& ctx, const Settings& current) {
    ui::separatorText("Touche du menu");
    cvarWidget(ctx, current, "rlt_menu_key");
    wrappedDisabled("Tape le nom BakkesMod de la touche puis Entrée : F3, F7, NumPadOne... Une touche déjà utilisée "
                    "dans BakkesMod ou dans Rocket League n'est jamais prise, et l'ancienne touche reste active.");
    if (!ctx.menuKeyStatus) return;
    const MenuKeyStatus st = ctx.menuKeyStatus();
    if (!st.checked) ImGui::TextDisabled("Vérification de la touche...");
    else if (st.bound) ui::textColored(kOk, st.text);
    else if (st.key.empty()) ImGui::TextDisabled("%s", st.text.c_str());
    else ui::textColored(kWarn, st.text);
}

void renderElements(const UiContext& ctx, const Settings& current) {
    ui::separatorText("Éléments du HUD");
    wrappedDisabled("Positions en fraction de l'écran. X : 0 = bord gauche, 1 = bord droit. Y : 0 = haut, 1 = bas. "
                    "Les panneaux qui se chevauchent sont empilés tout seuls, hors de la jauge de boost.");
    for (const HudElement& e : hudElements()) {
        bool exists = false;
        for (const HudSetting& s : e.settings) exists = exists || ctx.engine.schema().find(s.cvar) != nullptr;
        if (!exists) continue;
        const std::string header = std::string(e.title) + "##hud_" + e.id;
        if (!ImGui::CollapsingHeader(header.c_str())) continue;
        ImGui::Indent();
        if (*e.note) wrappedDisabled(e.note);
        for (const HudSetting& s : e.settings) cvarWidget(ctx, current, s.cvar, s.label);
        const std::string reset = std::string("Réinitialiser##hud_reset_") + e.id;
        if (ImGui::SmallButton(reset.c_str())) ui::resetCvars(ctx, elementCvars(e));
        hoverText("Remet l'affichage, la position, la taille et les couleurs de cet élément par défaut.");
        ImGui::Unindent();
        ImGui::Spacing();
    }
}

void renderResetAll(const UiContext& ctx) {
    constexpr const char* kPopup = "Réinitialiser le HUD ?##rlt_hud_reset_all";
    ImGui::Spacing();
    if (ImGui::Button("Réinitialiser tout le HUD")) ImGui::OpenPopup(kPopup);
    hoverText("Remet par défaut les réglages généraux du HUD et chaque élément ci-dessus.");
    if (ImGui::BeginPopupModal(kPopup, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
        ImGui::TextUnformatted("Les réglages généraux du HUD, et l'affichage, la position, la taille et les couleurs "
                               "de chaque élément reviennent à leur valeur par défaut. La touche du menu ne change pas.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Oui, réinitialiser")) {
            std::vector<std::string> names = generalHudCvars();
            for (const HudElement& e : hudElements())
                for (const HudSetting& s : e.settings) names.emplace_back(s.cvar);
            ui::resetCvars(ctx, names);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace

void renderHudTab(UiContext& ctx) {
    const Settings current = ctx.settings();
    // Sliders at a readable width, labels on their right.
    ImGui::PushItemWidth(std::min(320.f, ImGui::GetContentRegionAvail().x * 0.55f));

    renderCurrentPanel(ctx, current);

    ui::separatorText("Réglages généraux");
    for (const std::string& name : generalHudCvars()) cvarWidget(ctx, current, name.c_str());

    renderTrajectories(ctx, current);
    renderMenuKey(ctx, current);
    renderElements(ctx, current);
    renderResetAll(ctx);

    ImGui::PopItemWidth();
}

} // namespace rlt
