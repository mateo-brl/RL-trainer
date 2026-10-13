#include "plugin/ui/Ui.h"

#include <string>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/common/Units.h"
#include "core/modules/shooting/ShootingModule.h"
#include "imgui.h"

namespace rlt {

namespace {

// Everything the tab shows, copied under the engine lock (render thread).
struct ShootingView {
    bool available = false;
    ShootingStatus status;
    shoot::ShootingStats stats;
    ShootingConfig cfg;
    PlayerProfile profile;
    KeyStyle style = KeyStyle::Auto; // button style chosen in "Ma config"
    bool worldControl = false;
    std::string activityOwner;
    std::string activityName;
};

ShootingView capture(UiContext& ctx) {
    ShootingView v;
    auto lk = ctx.engine.lock();
    auto* mod = ctx.engine.get<ShootingModule>();
    if (!mod) return v;
    v.available = true;
    v.status = mod->status();
    v.stats = mod->stats();
    v.cfg = mod->config();
    v.profile = ctx.engine.profile();
    v.style = keyStyleFromIndex(ctx.engine.settings().profile.buttonStyle);
    v.worldControl = ctx.engine.worldControlAllowed();
    v.activityOwner = ctx.engine.activityOwner();
    v.activityName = ctx.engine.activityName();
    return v;
}

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return colors::Cyan;
    case Verdict::Good: return colors::Green;
    case Verdict::Ok: return colors::Yellow;
    case Verdict::Fail: return colors::Red;
    default: return colors::White;
    }
}

std::string speedText(float uus, const ShootingView& v, int decimals = 1) {
    const SpeedUnit unit = resolveUnit(static_cast<SpeedUnit>(v.cfg.unit), v.profile.metric);
    return formatSpeed(uus, unit, decimals);
}

void statusSection(UiContext& ctx, const ShootingView& v) {
    const ShootingStatus& st = v.status;
    if (st.mode != ShootMode::None) {
        ui::textColored(colors::Green, std::string("Exercice en cours : ") + shootModeLabel(st.mode) + " (" +
                                           shootPhaseLabel(st.phase) + ")");
        if (st.mode == ShootMode::Flick) {
            ImGui::Text("Flick demandé : %s",
                        st.requestedFlick >= 0 ? shoot::flickTypeLabel(static_cast<shoot::FlickType>(st.requestedFlick))
                                               : "n'importe lequel");
            ImGui::Text("Niveau : %s", ui::percent(st.flickLevel).c_str());
        } else if (st.mode == ShootMode::Targets) {
            ImGui::Text("Cible : %s (%s)", shoot::zoneLabel(static_cast<shoot::Zone>(st.currentZone)),
                        shoot::setupLabel(static_cast<shoot::TargetSetup>(st.currentSetup)));
        }
        if (ImGui::Button("Arrêter##shooting")) ctx.post("shooting", "stop");
        ImGui::SameLine();
        if (ImGui::Button("Nouvel essai##shooting")) ctx.post("shooting", "next");
    } else {
        ImGui::TextDisabled("Aucun exercice de tir en cours.");
        if (!v.activityOwner.empty()) ImGui::TextDisabled("Exercice actif ailleurs : %s", v.activityName.c_str());
    }
    if (!v.worldControl)
        ui::textColored(colors::Orange,
                        "Exercices indisponibles ici : lance le freeplay. L'analyse et l'affichage restent actifs.");
    if (!st.message.empty()) ImGui::TextDisabled("%s", st.message.c_str());
    if (st.hasLastShot) ImGui::Text("Dernier tir : %s", speedText(st.lastShotSpeed, v).c_str());
    if (st.hasLastResult) {
        const AttemptResult& r = st.lastResult;
        ui::textColored(verdictColor(r.verdict),
                        r.title + " : " + verdictLabel(r.verdict) + " (" + formatNumber(r.score, 0) + "/100)");
        for (size_t i = 0; i < r.tips.size() && i < 2; ++i) ImGui::BulletText("%s", r.tips[i].text.c_str());
    }
}

void flickSection(UiContext& ctx, const ShootingView& v) {
    ImGui::TextWrapped(
        "La balle est posée sur ton toit. Saute puis dodge : le plugin reconnaît le flick, mesure sa puissance, "
        "sa direction par rapport à la cible, la hauteur et ton timing, puis te donne un conseil.");
    ImGui::Text("Tes boutons : %s, %s", actionPrompt(v.profile, GameAction::Jump, v.style).c_str(),
                shoot::airRollPrompt(v.profile, v.style).c_str());
    if (ImGui::Button("Flicks libres (tous types)")) ctx.post("shooting", "flick", {"any"});
    ui::helpMarker("Fais le flick que tu veux : il est reconnu et noté. Les records sont gardés par type.");
    ImGui::Spacing();
    for (int i = 0; i < shoot::kRequestableFlickCount; ++i) {
        const auto type = static_cast<shoot::FlickType>(i);
        ImGui::PushID(i);
        const std::string header = std::string(shoot::flickTypeLabel(type)) + "##flicktype";
        if (ImGui::TreeNode(header.c_str())) {
            ImGui::PushTextWrapPos(0.f);
            ImGui::TextUnformatted(shoot::flickHowTo(type, v.profile, v.style, v.cfg.delayedSec).c_str());
            ImGui::PopTextWrapPos();
            const shoot::FlickRecord& rec = v.stats.flickAll[static_cast<size_t>(i)];
            if (rec.count > 0)
                ImGui::TextDisabled("Record : %s, %d essais (%d réussis)", speedText(rec.bestPower, v).c_str(),
                                    rec.count, rec.successes);
            if (ImGui::Button("Entraîner ce flick")) ctx.post("shooting", "flick", {shoot::flickTypeId(type)});
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("(?) dans un résultat = classification incertaine : proche d'un seuil (voir Avancé) "
                        "ou dodge non confirmé par le jeu.");
    ui::settingsSection(ctx, "shooting", "Flicks");
}

void targetsSection(UiContext& ctx, const ShootingView& v) {
    ImGui::TextWrapped(
        "Des zones sont dessinées dans le but adverse. Tire dans la zone en surbrillance : le point d'impact "
        "sur la ligne de but est mesuré, un marqueur montre l'impact prévu pendant que la balle vole. "
        "La taille des zones et la vitesse de la balle s'adaptent à ton niveau.");
    const char* zoneButtons[] = {"Lucarne gauche",   "Lucarne droite", "Poteau bas gauche",
                                 "Poteau bas droit", "Centre",         "Aléatoire"};
    const char* zoneArgs[] = {"tl", "tr", "bl", "br", "center", "random"};
    ImGui::Text("Zone visée (lance l'exercice) :");
    for (int i = 0; i < 6; ++i) {
        if (i % 3 != 0) ImGui::SameLine();
        const bool selected = v.status.zoneSelection == i;
        const std::string label = std::string(selected ? "> " : "") + zoneButtons[i] + "##zone" + std::to_string(i);
        if (ImGui::Button(label.c_str())) {
            // Saved choice (also the flick target "Zone choisie dans Cibles"), then start the drill.
            ctx.setCvar("rlt_sh_target_zone", std::to_string(i));
            ctx.post("shooting", "targets", {zoneArgs[i]});
        }
    }
    ImGui::Text("Mise en place :");
    const int setupSel = v.status.setupSelection;
    for (int i = 0; i < shoot::kTargetSetupCount; ++i) {
        if (i > 0) ImGui::SameLine();
        const std::string label = std::string(shoot::setupLabel(static_cast<shoot::TargetSetup>(i))) + "##setup";
        if (ImGui::RadioButton(label.c_str(), setupSel == i)) {
            ctx.setCvar("rlt_sh_target_setup", std::to_string(i));
            if (v.status.mode == ShootMode::Targets)
                ctx.post("shooting", "setup", {shoot::setupId(static_cast<shoot::TargetSetup>(i))});
        }
    }

    ui::separatorText("Précision par zone");
    ImGui::Columns(5, "shooting_zones", true);
    ImGui::TextUnformatted("Zone");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Session");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Précédente");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Total");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Score moyen");
    ImGui::NextColumn();
    ImGui::Separator();
    auto cell = [](const shoot::ZoneStats& z) {
        if (z.attempts == 0)
            ImGui::TextDisabled("-");
        else
            ImGui::Text("%d/%d (%s)", z.hits, z.attempts, ui::percent(z.hitRate()).c_str());
        ImGui::NextColumn();
    };
    for (int i = 0; i < shoot::kZoneCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        ImGui::TextUnformatted(shoot::zoneLabel(static_cast<shoot::Zone>(i)));
        ImGui::NextColumn();
        cell(v.stats.zoneSession[k]);
        cell(v.stats.zonePrev[k]);
        cell(v.stats.zoneAll[k]);
        if (v.stats.zoneAll[k].attempts > 0)
            ImGui::Text("%s / 100", formatNumber(v.stats.zoneAll[k].avgScore(), 0).c_str());
        else
            ImGui::TextDisabled("-");
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    ImGui::Text("Série actuelle : %d, record de la session : %d, record : %d", v.stats.streak,
                v.stats.bestStreakSession, v.stats.bestStreakAll);
    if (!v.stats.targetScores.empty())
        ImGui::PlotLines("Scores##targets", v.stats.targetScores.data(), static_cast<int>(v.stats.targetScores.size()),
                         0, "Scores de la session", 0.f, 100.f, ImVec2(0.f, 60.f));

    ui::separatorText("Niveau par mise en place");
    for (int i = 1; i < shoot::kTargetSetupCount; ++i) {
        const float lvl = v.status.setupLevels[static_cast<size_t>(i)];
        const std::string overlay =
            std::string(shoot::setupLabel(static_cast<shoot::TargetSetup>(i))) + " : " + ui::percent(lvl);
        ImGui::ProgressBar(lvl, ImVec2(-1.f, 0.f), overlay.c_str());
    }
    ui::settingsSection(ctx, "shooting", "Cibles dans le but");
}

void powerSection(UiContext& ctx, const ShootingView& v) {
    ImGui::TextWrapped(
        "La balle roule vers toi : frappe-la le plus fort possible. Astuce : boost à fond, puis flip avec le nez "
        "de la voiture dans la balle au dernier moment.");
    if (ImGui::Button("Lancer le défi puissance")) ctx.post("shooting", "power");
    ImGui::Text("Objectif : %s", speedText(v.cfg.powerGoalKph / kUusToKph, v, 0).c_str());
    if (v.stats.powerAll.best > 0.f) {
        ImGui::Text("Record : %s", speedText(v.stats.powerAll.best, v).c_str());
        ImGui::Text("Session : %s en %d tirs", speedText(v.stats.powerSession.best, v).c_str(),
                    v.stats.powerSession.count);
    } else {
        ImGui::TextDisabled("Pas encore de tir enregistré.");
    }
    if (!v.stats.shotPowersKph.empty())
        ImGui::PlotLines("km/h##power", v.stats.shotPowersKph.data(), static_cast<int>(v.stats.shotPowersKph.size()), 0,
                         "Tirs de la session (km/h)", 0.f, 180.f, ImVec2(0.f, 60.f));
    ui::settingsSection(ctx, "shooting", "Puissance");
}

void recordsSection(UiContext& ctx, const ShootingView& v) {
    ImGui::Columns(5, "shooting_flick_records", true);
    ImGui::TextUnformatted("Flick");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Record");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Session");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Essais (réussis)");
    ImGui::NextColumn();
    ImGui::TextUnformatted("Meilleure précision");
    ImGui::NextColumn();
    ImGui::Separator();
    bool any = false;
    for (int i = 0; i < shoot::kFlickTypeCount; ++i) {
        const size_t k = static_cast<size_t>(i);
        const shoot::FlickRecord& all = v.stats.flickAll[k];
        if (all.count == 0) continue;
        any = true;
        const shoot::FlickRecord& ses = v.stats.flickSession[k];
        ImGui::TextUnformatted(shoot::flickTypeLabel(static_cast<shoot::FlickType>(i)));
        ImGui::NextColumn();
        ImGui::TextUnformatted(all.bestPower > 0.f ? speedText(all.bestPower, v).c_str() : "-");
        ImGui::NextColumn();
        ImGui::TextUnformatted(ses.bestPower > 0.f ? speedText(ses.bestPower, v).c_str() : "-");
        ImGui::NextColumn();
        ImGui::Text("%d (%d)", all.count, all.successes);
        ImGui::NextColumn();
        ImGui::Text("%s %%", formatNumber(all.bestAccuracy, 0).c_str());
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
    if (!any) ImGui::TextDisabled("Aucun flick enregistré pour l'instant.");
    if (!v.stats.flickPowersKph.empty())
        ImGui::PlotLines("km/h##flicks", v.stats.flickPowersKph.data(), static_cast<int>(v.stats.flickPowersKph.size()),
                         0, "Flicks de la session (km/h)", 0.f, 160.f, ImVec2(0.f, 60.f));

    if (ImGui::Button("Effacer la session")) ctx.post("shooting", "reset_stats", {"session"});
    ImGui::SameLine();
    if (ImGui::Button("Effacer tous les records")) ImGui::OpenPopup("Effacer les records ?##shooting");
    if (ImGui::BeginPopupModal("Effacer les records ?##shooting", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Tous les records, statistiques et niveaux de Tirs & flicks seront effacés.");
        if (ImGui::Button("Oui, tout effacer")) {
            ctx.post("shooting", "reset_stats");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void commandsSection() {
    ImGui::TextWrapped("Dans la console BakkesMod (F6) ou dans un bind :");
    ImGui::BulletText("rlt shooting flick [type|any] : front, 45, 90, backflip, delayed, musty, breezi, reverse "
                      "(any = tous les flicks)");
    ImGui::BulletText("rlt shooting targets [tl|tr|bl|br|center|random] [mix|rolling|bouncing|pass|roof] "
                      "(zone et mise en place données sont sauvegardées)");
    ImGui::BulletText("rlt shooting power : défi de puissance");
    ImGui::BulletText("rlt shooting next : nouvel essai tout de suite");
    ImGui::BulletText("rlt shooting setup <mise en place> / zone <zone> : change et sauvegarde la situation ou la "
                      "cible (tout de suite en Cibles, sinon pour le prochain exercice)");
    ImGui::BulletText("rlt shooting stop : arrête l'exercice");
    ImGui::BulletText("rlt shooting reset_stats [session] : efface les statistiques (tout, ou la session)");
}

} // namespace

void renderShootingTab(UiContext& ctx) {
    const ShootingView v = capture(ctx);
    if (!v.available) {
        ImGui::TextDisabled("Module Tirs & flicks indisponible.");
        return;
    }
    ImGui::TextWrapped(
        "Flicks avec la balle sur le toit, tirs de précision dans les zones du but et défi de puissance. "
        "Chaque essai est noté avec un conseil, tes records sont sauvegardés.");
    if (!v.cfg.enabled) ui::textColored(colors::Orange, "Module désactivé (voir Exercices, Activer).");
    statusSection(ctx, v);

    if (ImGui::CollapsingHeader("Flicks##shooting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        flickSection(ctx, v);
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Cibles dans le but##shooting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        targetsSection(ctx, v);
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Puissance##shooting")) {
        ImGui::Indent();
        powerSection(ctx, v);
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Records de flicks##shooting")) {
        ImGui::Indent();
        recordsSection(ctx, v);
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Commandes console##shooting")) {
        ImGui::Indent();
        commandsSection();
        ImGui::Unindent();
    }
    ui::separatorText("Réglages");
    for (const char* section : {"Exercices", "Affichage", "Avancé"}) {
        bool visible = false;
        for (const SettingDef* d : ctx.engine.schema().forModule("shooting"))
            if (d->section == section && (!d->advanced || ctx.showAdvanced)) visible = true;
        if (!visible) continue;
        const std::string header = std::string(section) + "##shooting_settings";
        if (ImGui::CollapsingHeader(header.c_str())) {
            ImGui::Indent();
            ui::settingsSection(ctx, "shooting", section);
            ImGui::Unindent();
        }
    }
}

} // namespace rlt
