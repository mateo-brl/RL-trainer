#include "plugin/ui/Ui.h"

#include <string>

#include "imgui.h"
#include "plugin/Version.h"
#include "plugin/services/UpdateService.h"

namespace rlt {

namespace {

// Bullet whose text wraps at the page edge (BulletText never wraps), lines aligned after the bullet.
void bullet(const std::string& text) {
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::TextWrapped("%s", text.c_str());
}

void wrappedColored(const Color& c, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void renderHomeTab(UiContext& ctx) {
    GameContext game;
    std::string owner, activity;
    bool worldControl = false;
    GeneralConfig general;
    {
        auto lk = ctx.engine.lock();
        game = ctx.engine.context();
        owner = ctx.engine.activityOwner();
        activity = ctx.engine.activityName();
        worldControl = ctx.engine.worldControlAllowed();
        general = ctx.engine.settings().general;
    }

    ImGui::Text("RL Trainer %s", kPluginVersionString);
    ImGui::TextWrapped("Entraîne ta première touche, tes mécas, tes kickoffs et ton contrôle de balle. "
                       "Tout s'adapte automatiquement à ta config Rocket League.");

    ui::separatorText("Menu et HUD");
    // The glue knows whether the key really opens the menu (it may already be used in BakkesMod).
    const MenuKeyStatus key = ctx.menuKeyStatus ? ctx.menuKeyStatus() : MenuKeyStatus{};
    const std::string chooseKey = "Choisis une touche pour ouvrir ce menu dans la page HUD.";
    if (key.checked) bullet(key.bound ? key.key + " ouvre ce menu." : chooseKey);
    else bullet(general.menuKey.empty() ? chooseKey : general.menuKey + " ouvre ce menu.");
    if (general.hudFocus)
        bullet("En jeu, le HUD montre un seul panneau à la fois : celui de l'exercice en cours, sinon celui de ton "
               "dernier essai.");
    else
        bullet("En jeu, tous les panneaux s'affichent ensemble (« Un seul panneau à la fois » est désactivé).");
    if (general.hudCompact) bullet("HUD compact : chaque panneau garde l'essentiel, le détail de tes essais reste ici.");
    bullet("Les trajectoires (prédiction de la balle, lignes guides) sont coupées par défaut : active-les dans la page "
           "HUD, section Trajectoires.");
    if (ImGui::SmallButton("Régler le HUD")) ui::goToPage("hud");

    ui::separatorText("Statut");
    ImGui::Text("Mode : %s", gameModeLabel(game.mode));
    if (!game.mapName.empty())
        ImGui::Text("Map : %s", game.mapTitle.empty() ? game.mapName.c_str() : game.mapTitle.c_str());
    if (game.mode == GameMode::Online)
        wrappedColored(colors::Orange, "En ligne : le plugin est entièrement désactivé.");
    else if (!worldControl)
        wrappedColored(colors::Grey,
                       "Exercices indisponibles ici (analyse et affichage actifs). Va en freeplay pour les exercices.");

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
        if (!st.message.empty()) wrappedColored(col, st.message.c_str());
        if (st.phase == P::Downloading) ui::progressBar(st.progress, ui::percent(st.progress));
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
    bullet("Lance le freeplay : l'analyse de tes touches et de tes mécas démarre toute seule.");
    bullet("Pages Mécas, Kickoff et Parcours (colonne de gauche) : démarre un exercice en un clic.");
    bullet("Page Progression : tes records, ton historique et le plan conseillé.");
    bullet("Console (F6) : \"rlt <module> <commande>\", par exemple \"rlt kickoff start\".");
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
