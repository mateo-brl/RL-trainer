#include "plugin/ui/Ui.h"

#include "imgui.h"
#include "plugin/Version.h"
#include "plugin/services/UpdateService.h"

namespace rlt {

void renderHomeTab(UiContext& ctx) {
    GameContext game;
    std::string owner, activity;
    bool worldControl = false;
    {
        auto lk = ctx.engine.lock();
        game = ctx.engine.context();
        owner = ctx.engine.activityOwner();
        activity = ctx.engine.activityName();
        worldControl = ctx.engine.worldControlAllowed();
    }

    ImGui::Text("RL Trainer %s", kPluginVersionString);
    ImGui::TextWrapped("Entraîne ta première touche, tes mécas, tes kickoffs et ton contrôle de balle. "
                       "Tout s'adapte automatiquement à ta config Rocket League.");

    ui::separatorText("Statut");
    ImGui::Text("Mode : %s", gameModeLabel(game.mode));
    if (!game.mapName.empty())
        ImGui::Text("Map : %s", game.mapTitle.empty() ? game.mapName.c_str() : game.mapTitle.c_str());
    if (game.mode == GameMode::Online)
        ui::textColored(colors::Orange, "En ligne : le plugin est entièrement désactivé.");
    else if (!worldControl)
        ui::textColored(colors::Grey, "Exercices indisponibles ici (analyse et affichage actifs). Va en freeplay pour les exercices.");

    if (!owner.empty()) {
        ImGui::Text("Exercice en cours : %s", activity.c_str());
        if (ImGui::Button("Arrêter l'exercice")) ctx.post("engine", "stop");
    } else {
        ImGui::TextDisabled("Aucun exercice en cours.");
    }

    if (ctx.updateService) {
        ui::separatorText("Mises à jour");
        const UpdateService::State st = ctx.updateService->state();
        using P = UpdateService::Phase;
        const Color col = st.phase == P::Available || st.phase == P::Ready ? colors::Green
                          : st.phase == P::Error                         ? colors::Orange
                                                                          : colors::Grey;
        if (!st.message.empty()) ui::textColored(col, st.message);
        if (st.phase == P::Downloading) ImGui::ProgressBar(st.progress, ImVec2(-1, 0));
        if (st.phase == P::Available) {
            if (ImGui::Button("Mettre à jour maintenant")) ctx.updateService->downloadAsync();
            ImGui::SameLine();
        }
        if (st.phase != P::Checking && st.phase != P::Downloading) {
            if (ImGui::Button("Vérifier les mises à jour")) ctx.updateService->checkAsync();
        }
        if (!st.pageUrl.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Copier le lien de la version")) ImGui::SetClipboardText(st.pageUrl.c_str());
        }
    }

    ui::separatorText("Pour commencer");
    ImGui::BulletText("Lance le freeplay : l'écart de vitesse et l'analyse des touches s'activent tout seuls.");
    ImGui::BulletText("Onglets Mécaniques, Kickoff et Parcours : démarre un exercice en un clic.");
    ImGui::BulletText("Onglet Progression : tes records, ton historique et le plan conseillé.");
    ImGui::BulletText("Console (F6) : \"rlt <module> <commande>\", par exemple \"rlt kickoff start\".");
}

void renderGeneralTab(UiContext& ctx) {
    ImGui::Checkbox("Afficher les réglages avancés", &ctx.showAdvanced);
    ui::moduleSettings(ctx, "general");
    ui::separatorText("Sauvegarde");
    if (ImGui::Button("Sauvegarder maintenant")) {
        ctx.post("engine", "save");
        ctx.runOnGame([cvars = ctx.cvars]() { cvars->executeCommand("writeconfig", false); });
    }
    ui::helpMarker("Tes réglages sont enregistrés dans le config.cfg de BakkesMod, ta progression dans "
                   "bakkesmod/data/rltrainer/progress.txt.");
}

} // namespace rlt
