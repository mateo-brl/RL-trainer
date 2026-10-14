#include <algorithm>
#include <cstdio>
#include <string>

#include "core/common/Text.h"
#include "core/modules/prediction/PredictionModule.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

const Color kGood{90.f, 230.f, 120.f, 255.f};
const Color kWarn{255.f, 200.f, 60.f, 255.f};
const Color kBad{255.f, 90.f, 90.f, 255.f};

std::string secs(float s) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f s", static_cast<double>(std::max(0.f, s)));
    return buf;
}

std::string uu(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f uu", static_cast<double>(v));
    return buf;
}

const char* arenaLabel(pr::ArenaMode m) {
    switch (m) {
    case pr::ArenaMode::FloorOnly: return "Sol seulement (map workshop)";
    case pr::ArenaMode::None: return "Sans collision (anneaux, air dribble)";
    default: return "Terrain standard";
    }
}

const char* jumpLabel(pr::JumpKind k) {
    switch (k) {
    case pr::JumpKind::Jump: return "Saut";
    case pr::JumpKind::DoubleJump: return "Double saut";
    case pr::JumpKind::Aerial: return "Aérien";
    default: return "Pas besoin de sauter";
    }
}

// One "label | value" row of a two-column block.
void row(const char* label, const std::string& value, const Color* color = nullptr) {
    ImGui::TextUnformatted(label);
    ImGui::NextColumn();
    if (color) ui::textColored(*color, value);
    else ImGui::TextUnformatted(value.c_str());
    ImGui::NextColumn();
}

void renderHowTo() {
    if (!ImGui::CollapsingHeader("Comment s'en servir##pr_howto", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Indent();
    ImGui::BulletText("Cercle blanc au sol : là où la balle va retomber, avec le temps restant.");
    ImGui::TextWrapped("    Place-toi dessus avant elle : tu la reçois face à toi au lieu de lui courir après.");
    ImGui::BulletText("Partie ambre et épaisse de la ligne : la zone de première touche.");
    ImGui::TextWrapped("    La balle y redescend à une hauteur facile à prendre (réglable). C'est là que ton contrôle "
                       "sera le plus propre.");
    ImGui::BulletText("Repère violet : le premier endroit que ta voiture peut atteindre ('Interception dans 1.2 s').");
    ImGui::TextWrapped("    S'il est en hauteur, le texte te dit quel saut faire, avec ton bouton.");
    ImGui::BulletText("'Arrive à X km/h' : la vitesse de la balle à ce moment-là.");
    ImGui::TextWrapped("    Arrive à la même vitesse qu'elle et dans la même direction : la différence de vitesse est "
                       "faible, la touche est douce et la balle reste collée à toi. Trop vite = elle rebondit loin.");
    ImGui::BulletText("Ligne verte : la balle va marquer pour toi. Ligne rouge : elle file dans ton but, va défendre !");
    ImGui::BulletText("Cercles jaunes : rebonds sur le sol, les murs ou le plafond. Anneaux : une marque toutes les 0,5 s.");
    ImGui::BulletText("Balle figée en l'air (pack d'entraînement) : rien ne s'affiche tant qu'elle ne bouge pas.");
    ImGui::Unindent();
}

void renderLive(UiContext& ctx, const PredictionStatus& st, const PredictionConfig& cfg) {
    ui::separatorText("En direct");
    const Color zoneColor = cfg.zoneColor.withAlpha(255.f);
    const Color interceptColor = cfg.interceptColor.withAlpha(255.f);
    if (!st.enabled) {
        ui::textColored(colors::Grey, "Prédiction coupée (coche « Activer la prédiction » en haut de la page).");
    } else if (st.hidden) {
        ui::textColored(kWarn, "Prédiction masquée. Clique sur 'Afficher' ou tape 'rlt prediction toggle'.");
    } else if (st.frozen) {
        ImGui::TextDisabled("Balle figée en l'air (pack d'entraînement) : la prédiction apparaît dès qu'elle bouge.");
    } else if (!st.active) {
        ImGui::TextDisabled("En attente de la balle (freeplay, match hors ligne, workshop ou replay).");
    } else {
        ImGui::Columns(2, "pr_live_cols", false);
        ImGui::SetColumnWidth(0, 190.f);
        const char* source = !st.fromView ? "Physique du jeu (120 Hz)"
                                          : (st.replay ? "Caméra (replay)" : "Caméra (voiture détruite ou spectateur)");
        row("Source", source);
        row("Terrain simulé", arenaLabel(st.arena));
        {
            std::string ball = st.ballAirborne ? "En l'air, à " + uu(st.ballHeight) : std::string("Au sol");
            ball += ", " + PredictionModule::formatBallSpeed(st.ballSpeed, st.unit);
            row("Balle", ball);
        }
        row("Atterrissage", st.hasLanding ? "dans " + secs(st.landingIn) : std::string("-"));
        row("Sommet", st.hasApex ? "dans " + secs(st.apexIn) + " à " + uu(st.apexHeight) : std::string("-"));
        if (st.hasZone)
            row("Zone de touche", "dans " + secs(st.zoneIn) + " (hauteur " + uu(st.zoneHeight) + ")", &zoneColor);
        else row("Zone de touche", "-");
        row("Rebonds à venir", std::to_string(st.bounces));
        if (st.hasIntercept) {
            row("Interception", "dans " + secs(st.interceptIn), &interceptColor);
            row("Arrive à", PredictionModule::formatBallSpeed(st.interceptBallSpeed, st.unit), &interceptColor);
            row("Ta vitesse estimée", PredictionModule::formatBallSpeed(st.interceptCarSpeed, st.unit) + " (plein gaz)");
            row("Hauteur / saut", uu(st.interceptHeight) + ", " + jumpLabel(st.interceptJump));
        } else {
            row("Interception", st.fromView ? "indisponible ici (pas de voiture)" : "hors de portée");
        }
        if (st.goalSide == 0) {
            row("But", "non");
        } else if (st.goalForPlayer > 0) {
            row("But", "pour toi dans " + secs(st.goalIn), &kGood);
        } else if (st.goalForPlayer < 0) {
            row("But", "contre ton camp dans " + secs(st.goalIn), &kBad);
        } else {
            row("But", std::string(st.goalSide > 0 ? "but orange" : "but bleu") + " dans " + secs(st.goalIn), &kWarn);
        }
        ImGui::Columns(1);
    }

    if (!st.enabled) return;  // hide / recompute mean nothing while the module is off
    if (ImGui::Button(st.hidden ? "Afficher la prédiction##pr" : "Masquer la prédiction##pr")) ctx.post("prediction", "toggle");
    ImGui::SameLine();
    if (ImGui::Button("Recalculer##pr")) ctx.post("prediction", "recompute");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Refait la prédiction tout de suite (utile après un changement de map).");
}

void renderAccuracy(UiContext& ctx, const PredictionStatus& st, const PredictionAccuracy& acc) {
    ui::separatorText("Précision mesurée");
    ImGui::TextWrapped("Pour chaque balle libre, le plugin compare la position prévue 1 s à l'avance avec la vraie "
                       "position. Ça vérifie la physique sur ta version du jeu.");
    if (acc.count == 0) {
        ImGui::TextDisabled("Pas encore de mesure : lance des balles en l'air en freeplay sans les toucher.");
    } else {
        const Color& c = acc.meanError < 30.f ? kGood : (acc.meanError < 80.f ? kWarn : kBad);
        const char* verdict = acc.meanError < 30.f ? "excellent" : (acc.meanError < 80.f ? "correct" : "approximatif");
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Écart moyen à 1 s : %.0f uu (%s), %d mesures, max %.0f uu",
                      static_cast<double>(acc.meanError), verdict, acc.count, static_cast<double>(acc.maxError));
        ui::textColored(c, buf);
        if (!acc.recent.empty()) {
            const float top = std::max(50.f, *std::max_element(acc.recent.begin(), acc.recent.end()) * 1.1f);
            ImGui::PlotLines("##pr_err_plot", acc.recent.data(), static_cast<int>(acc.recent.size()), 0,
                             "Écart des dernières mesures (uu)", 0.f, top, ImVec2(0.f, 70.f));
        }
        if (acc.disturbed > 0)
            ImGui::TextDisabled("%d mesure(s) ignorée(s) : balle touchée par une autre voiture, reset...", acc.disturbed);
        if (acc.meanError >= 80.f)
            ImGui::TextWrapped("Écart élevé : terrain non standard (Throwback Stadium, map workshop) ou mutateur de "
                               "gravité / vitesse de balle ? Essaie un autre 'Terrain simulé' dans les réglages avancés.");
    }
    if (ImGui::SmallButton("Remettre la précision à zéro##pr")) ctx.post("prediction", "reset_stats");
    ImGui::TextDisabled("Calculs : %lld en jeu (dont %lld après un imprévu), %lld depuis la caméra", st.recomputes,
                        st.forcedRecomputes, st.viewRecomputes);
}

void renderCommands() {
    if (!ImGui::CollapsingHeader("Commandes console (F6)##pr_cmds")) return;
    ImGui::Indent();
    ImGui::BulletText("rlt prediction toggle : masque ou affiche la prédiction.");
    ImGui::TextWrapped("    Astuce : lie-la à une touche avec  bind F3 \"rlt prediction toggle\"");
    ImGui::BulletText("rlt prediction show / rlt prediction hide : affiche / masque.");
    ImGui::BulletText("rlt prediction recompute : refait la prédiction tout de suite.");
    ImGui::BulletText("rlt prediction reset_stats : remet la précision mesurée à zéro.");
    ImGui::Unindent();
}

} // namespace

void renderPredictionTab(UiContext& ctx) {
    PredictionStatus st;
    PredictionAccuracy acc;
    PredictionConfig cfg;
    bool available = false;
    {
        auto lk = ctx.engine.lock();
        auto* mod = ctx.engine.get<PredictionModule>();
        if (mod) {
            st = mod->status();
            acc = mod->accuracy();
            cfg = mod->config();
            available = true;
        }
    }
    if (!available) {
        ImGui::TextDisabled("Module Prédiction indisponible.");
        return;
    }

    ImGui::TextWrapped("Vois où va la balle avant elle : trajectoire, rebonds, point de chute, zone de première touche "
                       "et le meilleur moment pour l'intercepter. Marche en freeplay, en match hors ligne, sur les maps "
                       "workshop et dans les replays.");
    // Off by default, like every trajectory: the switch comes first so it is found without opening the settings.
    const Settings current = ctx.settings();
    if (const SettingDef* def = ctx.engine.schema().find("rlt_pr_enabled")) {
        ImGui::PushID("pr_top");  // the same checkbox is also in the settings below
        ui::settingWidget(ctx, *def, current);
        ImGui::PopID();
    }
    if (!cfg.enabled)
        ImGui::TextDisabled("Coupée par défaut, comme toutes les trajectoires (page HUD, section Trajectoires).");
    renderHowTo();
    renderLive(ctx, st, cfg);
    renderAccuracy(ctx, st, acc);
    renderCommands();
    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "prediction");
}

} // namespace rlt
