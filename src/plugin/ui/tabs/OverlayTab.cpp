// "Affichage" tab: live values of the overlay (speed differential in its zone colour, session timer, touch zone
// state), action buttons, every setting grouped like First Touch Trainer, training time statistics and the console
// commands. Render thread: module state is copied under the engine lock, settings change through cvars only.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/Text.h"
#include "core/modules/overlay/OverlayModule.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

constexpr const char* kModule = "overlay";

void hoverTip(const char* text) {
    if (text && *text && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Colour widget that also turns "match the speed colour" off when the player picks a colour, like the original
// plugin did (otherwise the new colour would never show).
void colourTurningMatchOff(const UiContext& ctx, const SettingDef& def, const Settings& cur, const char* matchCvar) {
    Color c;
    parseColor(def.read ? def.read(cur) : def.defaultValue, c);
    float col[4] = {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
    const std::string label = def.label + "##" + def.cvar;
    if (ImGui::ColorEdit4(label.c_str(), col, ImGuiColorEditFlags_AlphaBar)) {
        ctx.setCvar(def.cvar, toHex(Color(col[0] * 255.f, col[1] * 255.f, col[2] * 255.f, col[3] * 255.f)));
        ctx.setCvar(matchCvar, "0");
    }
    hoverTip(def.tooltip.c_str());
}

// One schema section with the colour/match rule above, followed by the section reset button.
void sectionWithColourRule(const UiContext& ctx, const Settings& cur, const char* section, const char* colourCvar,
                           const char* matchCvar) {
    for (const SettingDef* d : ctx.engine.schema().forModule(kModule)) {
        if (d->section != section) continue;
        if (d->advanced && !ctx.showAdvanced) continue;
        if (d->cvar == colourCvar) colourTurningMatchOff(ctx, *d, cur, matchCvar);
        else ui::settingWidget(ctx, *d, cur);
    }
    const std::string id = std::string("Réinitialiser##ov_") + section;
    if (ImGui::SmallButton(id.c_str())) ui::resetToDefaults(ctx, kModule, section);
    hoverTip("Remet les réglages de cette section par défaut (les valeurs du plugin d'origine).");
}

void collapsingSection(const UiContext& ctx, const Settings& cur, const char* section, const char* colourCvar = nullptr,
                       const char* matchCvar = nullptr) {
    const std::string header = std::string(section) + "##ov_header";
    if (!ImGui::CollapsingHeader(header.c_str())) return;
    ImGui::Indent();
    if (colourCvar && matchCvar) sectionWithColourRule(ctx, cur, section, colourCvar, matchCvar);
    else ui::settingsSection(ctx, kModule, section);
    ImGui::Unindent();
}

Color zoneColourOf(const OverlayConfig& cfg, int zone) {
    return ovl::zoneColor(static_cast<ovl::SpeedZone>(std::clamp(zone, 0, 2)), cfg);
}

const char* zoneLabelOf(int zone) { return ovl::zoneLabel(static_cast<ovl::SpeedZone>(std::clamp(zone, 0, 2))); }

// Why the circle is (not) on the ball right now.
std::string zoneState(const OverlayConfig& cfg, const OverlayStatus& st) {
    if (!cfg.zoneEnabled) return "désactivé";
    if (!st.ballValid) return "pas de balle";
    if (cfg.onlyBallInAir && !st.ballInAir) return "caché : balle trop basse";
    if (cfg.onlyCarBelowBall && st.carKnown && !st.carBelowBall) return "caché : ta voiture est au-dessus de la balle";
    return st.zoneVisible ? "visible" : "caché";
}

void liveRow(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::NextColumn();
}

void renderLive(const OverlayConfig& cfg, const OverlayStatus& st) {
    ui::separatorText("En direct");
    ImGui::Columns(2, "##ov_live", false);
    ImGui::SetColumnWidth(0, 170.f);

    liveRow("Écart de vitesse");
    if (st.speedAvailable) {
        const Color col = zoneColourOf(cfg, st.zone);
        ui::textColored(col, ovl::speedText(st.diff, st.unit) + "   " + zoneLabelOf(st.zone));
        if (st.hasZ) {
            ImGui::SameLine();
            ui::textColored(col, "   " + ovl::zText(st.zDiff, st.unit));
        }
    } else {
        ImGui::TextDisabled("%s", st.replay ? "pas de voiture à toi dans un replay"
                                            : "lance une partie (freeplay, entraînement, map)");
    }
    ImGui::NextColumn();

    liveRow("Calcul");
    ImGui::Text("%s, %s", ovl::speedModeLabel(ovl::speedModeFromIndex(cfg.speedMode)), unitSuffix(st.unit));
    ImGui::NextColumn();

    liveRow("Dernier contact");
    if (st.lastTouchAvailable)
        ui::textColored(zoneColourOf(cfg, st.lastTouchZone),
                        ovl::speedText(st.lastTouchDiff, st.unit) + "   " + zoneLabelOf(st.lastTouchZone));
    else
        ImGui::TextDisabled("aucun contact pour l'instant");
    ImGui::NextColumn();

    liveRow("Chrono de session");
    ImGui::Text("%s", formatClock(st.sessionSeconds).c_str());
    ImGui::NextColumn();

    liveRow("Cercle de contact");
    {
        const ovl::ZoneBehavior configured = ovl::behaviorFromIndex(cfg.zoneBehavior);
        std::string beh = ovl::behaviorLabel(configured);
        if (configured == ovl::ZoneBehavior::Auto)
            beh += std::string(" : ") + ovl::behaviorLabel(ovl::behaviorFromIndex(st.effectiveBehavior));
        char tilt[32];
        std::snprintf(tilt, sizeof(tilt), ", inclinaison %.0f°", static_cast<double>(st.tiltDeg));
        ImGui::Text("%s%s", beh.c_str(), st.ballValid ? tilt : "");
        ImGui::TextDisabled("%s", zoneState(cfg, st).c_str());
    }
    ImGui::NextColumn();
    ImGui::Columns(1);

    // Legend of the zones in the chosen unit.
    const float good = std::max(0.f, cfg.goodThreshold);
    const float alright = std::max(good, cfg.alrightThreshold);
    ui::textColored(cfg.goodColor, std::string("Bon : jusqu'à ") + formatSpeed(good, st.unit));
    ImGui::SameLine();
    ui::textColored(cfg.alrightColor, std::string("   Correct : jusqu'à ") + formatSpeed(alright, st.unit));
    ImGui::SameLine();
    ui::textColored(cfg.badColor, "   Trop d'écart : au-delà");
    if (ovl::speedModeFromIndex(cfg.speedMode) == ovl::SpeedMode::Closing)
        ui::helpMarker("Les couleurs utilisent l'écart en valeur absolue. Le chiffre est positif quand ta voiture et "
                       "la balle se rapprochent, négatif quand elles s'éloignent. Pour un premier contact contrôlé, "
                       "arrive doucement sur elle.");
    else
        ui::helpMarker("Les couleurs utilisent l'écart en valeur absolue. Le chiffre est positif quand ta voiture va "
                       "plus vite que la balle, négatif quand la balle va plus vite. Pour un premier contact contrôlé, "
                       "arrive à peu près à sa vitesse.");
}

void renderActions(const UiContext& ctx) {
    if (ImGui::Button("Écart dans le chat")) ctx.post(kModule, "speed");
    hoverTip("Écrit l'écart de vitesse actuel et sa zone dans le chat (rlt overlay speed).");
    ImGui::SameLine();
    if (ImGui::Button("Remettre le chrono à zéro")) ctx.post(kModule, "reset_timer");
    hoverTip("Repart de 0:00 sans changer de map (rlt overlay reset_timer).");
    ImGui::SameLine();
    if (ImGui::Button("Préréglage d'origine")) ctx.post(kModule, "preset_original");
    hoverTip("Remet tous les réglages de l'onglet aux valeurs de First Touch Trainer (tout désactivé, taille 3, "
             "cercle rouge de 47, couleurs vert / jaune / rouge). Même effet que rlt overlay preset_original.");
}

void renderStats(const UiContext& ctx, const OverlayStatus& st, const std::vector<OverlayDay>& days) {
    if (!ImGui::CollapsingHeader("Temps d'entraînement##ov_stats")) return;
    ImGui::Indent();
    ImGui::Text("Aujourd'hui : %s", formatClock(st.todaySeconds).c_str());
    ImGui::Text("Au total : %s", formatClock(st.totalSeconds).c_str());
    ImGui::Text("Sessions de plus de 30 s : %d", st.sessions);
    ImGui::Text("Plus longue session : %s", formatClock(st.longestSession).c_str());
    if (!days.empty()) {
        std::vector<float> minutes;
        minutes.reserve(days.size());
        float top = 1.f;
        for (const auto& d : days) {
            minutes.push_back(d.seconds / 60.f);
            top = std::max(top, d.seconds / 60.f);
        }
        const std::string overlay = "Minutes par jour (du " + days.front().date + " au " + days.back().date + ")";
        ImGui::PlotHistogram("##ov_days", minutes.data(), static_cast<int>(minutes.size()), 0, overlay.c_str(), 0.f,
                             top * 1.1f, ImVec2(0.f, 70.f));
    } else {
        ImGui::TextDisabled("Rien d'enregistré pour l'instant : le temps compte dès que tu joues hors ligne.");
    }
    if (ImGui::SmallButton("Effacer les statistiques##ov_stats")) ctx.post(kModule, "reset_stats");
    hoverTip("Efface le temps total, l'historique par jour et les sessions (rlt overlay reset_stats).");
    ImGui::Unindent();
}

void renderCommands() {
    if (!ImGui::CollapsingHeader("Commandes console (F6)##ov_cmds")) return;
    ImGui::Indent();
    ImGui::BulletText("rlt overlay speed : écart de vitesse actuel et sa zone dans le chat.");
    ImGui::BulletText("rlt overlay reset_timer : remet le chrono de session à zéro.");
    ImGui::BulletText("rlt overlay reset_stats : efface le temps d'entraînement enregistré.");
    ImGui::BulletText("rlt overlay preset_original : remet tous les réglages de l'Affichage (cvars rlt_ov_*) aux "
                      "valeurs de First Touch Trainer, comme le bouton « Préréglage d'origine ».");
    ImGui::TextDisabled("Astuce : bind \"rlt overlay speed\" sur une touche pour vérifier ton écart en plein "
                        "exercice.");
    ImGui::Unindent();
}

} // namespace

void renderOverlayTab(UiContext& ctx) {
    OverlayStatus st;
    std::vector<OverlayDay> days;
    bool found = false;
    {
        auto lk = ctx.engine.lock();
        if (auto* mod = ctx.engine.get<OverlayModule>()) {
            st = mod->status();
            days = mod->recentDays(14);
            found = true;
        }
    }
    const Settings cur = ctx.settings();
    const OverlayConfig& cfg = cur.overlay;

    ImGui::TextWrapped("Les repères de First Touch Trainer, en mieux : écart de vitesse entre ta voiture et la "
                       "balle, chrono de session, cercle de contact peint sur la balle et sphère centrale. Actifs en "
                       "freeplay, entraînement, maps workshop et replays ; jamais en ligne.");
    if (!found) {
        ImGui::TextDisabled("Module Affichage indisponible.");
        return;
    }

    renderLive(cfg, st);
    ImGui::Spacing();
    renderActions(ctx);

    ui::separatorText("Affichages");
    ui::settingsSection(ctx, kModule, OverlayModule::kSectionActivation, false);

    ui::separatorText("Réglages");
    collapsingSection(ctx, cur, OverlayModule::kSectionSpeed);
    collapsingSection(ctx, cur, OverlayModule::kSectionTimer);
    collapsingSection(ctx, cur, OverlayModule::kSectionZone, OverlayModule::kCvarZoneColor,
                      OverlayModule::kCvarZoneMatch);
    collapsingSection(ctx, cur, OverlayModule::kSectionSphere, OverlayModule::kCvarSphereColor,
                      OverlayModule::kCvarSphereMatch);
    collapsingSection(ctx, cur, OverlayModule::kSectionColors);
    if (!ctx.showAdvanced)
        ImGui::TextDisabled("Réglages experts du cercle (inclinaison, vitesses, lissage) : coche « Afficher les "
                            "réglages avancés » dans l'onglet Réglages.");

    ImGui::Spacing();
    renderStats(ctx, st, days);
    renderCommands();
}

} // namespace rlt
