// "Première touche" tab: card of the last graded touch, session statistics with a sparkline of the last 50 scores,
// contact heatmap (same drawing as the HUD) with its explanation, drill launcher with the adaptive level of each
// drill, console commands and the module settings. Render thread: module state is copied under the engine lock,
// gameplay changes go through commands (ctx.post), settings through cvars.

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/common/Text.h"
#include "core/modules/firsttouch/FirstTouchModule.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

constexpr const char* kModule = "firsttouch";
const Color kAccent{0, 200, 255, 255};
const Color kDim{170, 170, 170, 255};

// Everything the tab shows, copied under the engine lock.
struct FtUiData {
    bool found = false;
    FirstTouchConfig cfg;
    FirstTouchModule::LastTouchView last;
    ft::TouchStats session;
    ft::TouchStats total;
    std::vector<float> scores;
    std::vector<ft::HeatPoint> heat;
    FirstTouchModule::DrillView drill;
    std::array<FirstTouchModule::DrillLevelView, ft::kDrillKinds> levels{};
    FirstTouchModule::DribbleView dribble;
    SpeedUnit unit = SpeedUnit::Kph;
    std::string trend;
    std::string persistent;
    bool pending = false;
    GameContext game;
    bool worldControl = false;
};

FtUiData copyState(const UiContext& ctx) {
    FtUiData d;
    auto lk = ctx.engine.lock();
    auto* mod = ctx.engine.get<FirstTouchModule>();
    if (!mod) return d;
    d.found = true;
    d.cfg = mod->config();
    d.last = mod->lastTouch();
    d.session = mod->sessionStats();
    d.total = mod->totalStats();
    d.scores = mod->recentScores();
    d.heat = mod->heatmap();
    d.drill = mod->drillStatus();
    for (int i = 0; i < ft::kDrillKinds; ++i) d.levels[static_cast<size_t>(i)] = mod->drillLevel(static_cast<ft::DrillType>(i));
    d.dribble = mod->dribble();
    d.unit = mod->displayUnit();
    d.trend = mod->trend();
    d.persistent = mod->persistentTipCode();
    d.pending = mod->analysisPending();
    d.game = ctx.engine.context();
    d.worldControl = ctx.engine.worldControlAllowed();
    return d;
}

ImVec4 toVec4(const Color& c) { return ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f); }
ImU32 toU32(const Color& c) { return ImGui::ColorConvertFloat4ToU32(toVec4(c)); }

Color scoreColor(const FirstTouchConfig& cfg, float score) {
    if (score >= 70.f) return cfg.goodColor;
    if (score >= 45.f) return cfg.okColor;
    return cfg.badColor;
}

Color zoneColor(const FirstTouchConfig& cfg, ft::SpeedZone z) {
    switch (z) {
    case ft::SpeedZone::Good: return cfg.goodColor;
    case ft::SpeedZone::Ok: return cfg.okColor;
    case ft::SpeedZone::Bad: return cfg.badColor;
    }
    return colors::White;
}

void hoverTip(const char* text) {
    if (text && *text && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Two-column "label | value" row (inside Columns(2)).
void row(const char* label, const std::string& value, const Color* color = nullptr, const char* tip = nullptr) {
    ImGui::TextUnformatted(label);
    hoverTip(tip);
    ImGui::NextColumn();
    if (color) ui::textColored(*color, value);
    else ImGui::TextUnformatted(value.c_str());
    ImGui::NextColumn();
}

std::string percentText(float ratio) { return ui::percent(ratio); }

void subScoreBar(const char* label, float value01) {
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%s %d %%", label, static_cast<int>(std::lround(value01 * 100.f)));
    ImGui::ProgressBar(value01, ImVec2(ImGui::GetFontSize() * 11.f, 0.f), overlay);
}

// ---- Last touch -------------------------------------------------------------------------------------------
void renderLastTouch(const FtUiData& d) {
    const auto& l = d.last;
    if (!l.valid) {
        ImGui::TextDisabled(d.pending ? "Touche en cours d'analyse..." : "Pas encore de touche notée : touche la balle en freeplay !");
        return;
    }
    char head[160];
    std::snprintf(head, sizeof(head), "%s : %d/100  %s", ft::touchTypeTitle(l.type), static_cast<int>(l.score), l.headline.c_str());
    ui::textColored(scoreColor(d.cfg, l.score), head);
    if (l.inDrill) {
        ImGui::SameLine();
        ImGui::TextDisabled("(exercice)");
    }
    ImGui::SameLine();
    ui::textColored(l.success ? d.cfg.goodColor : d.cfg.badColor, l.success ? "Réussie" : "Pas réussie");
    hoverTip("Réussie = note au moins égale au seuil ET balle contrôlée pendant la fenêtre de contrôle.");

    subScoreBar("Vitesse", l.sSpeed);
    ImGui::SameLine();
    subScoreBar("Contact", l.sContact);
    ImGui::SameLine();
    subScoreBar("Contrôle", l.sControl);

    ImGui::Columns(2, "##ft_last_cols", false);
    ImGui::SetColumnWidth(0, ImGui::GetFontSize() * 14.f);
    const Color zc = zoneColor(d.cfg, l.zone);
    row("Écart de vitesse", ft::formatSpeedValue(l.speedDiff, d.unit) + "  " + ft::zoneTag(l.zone), &zc,
        "Vitesse de ta voiture moins vitesse de la balle (à plat), juste avant le contact. Positif = tu vas plus vite.");
    row("Vitesse relative (3D)", ft::formatSpeedValue(l.relSpeed, d.unit, 1, false), nullptr,
        "Ce que le jeu utilise vraiment : la balle se décolle à environ 0,65 x cette vitesse.");
    row("Dans l'axe / de travers",
        ft::formatSpeedValue(l.along, d.unit) + "  /  " + ft::formatSpeedValue(l.cross, d.unit, 1, false), nullptr,
        "Dans l'axe : + = tu rattrapes la balle trop vite. De travers : écart d'angle entre vos trajectoires.");
    std::string contact = std::string(ft::contactHeightLabel(l.height)) + " de la balle, " + ft::carPartLabel(l.part);
    if (l.fromHitInfo) contact += " (mesuré par le jeu)";
    row("Point de contact", contact);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f °", static_cast<double>(l.contactErrDeg));
    row("Écart au point idéal", buf, nullptr, "Angle entre ton point de contact et le point idéal pour ce type de touche.");
    std::snprintf(buf, sizeof(buf), "%+.0f uu", static_cast<double>(l.lateralOffset));
    row("Décentrage", buf, nullptr, "Position de la balle sur la largeur de ta voiture (+ = à droite).");
    row("Séparation", ft::formatSpeedValue(l.separation, d.unit, 1, false), nullptr,
        "Vitesse à laquelle la balle s'éloigne de toi juste après la touche.");
    row("Pop de la balle", ft::formatSpeedValue(l.ballPop, d.unit), nullptr,
        "Vitesse verticale de la balle par rapport à ta voiture après la touche.");
    std::snprintf(buf, sizeof(buf), "%.0f uu", static_cast<double>(l.popHeight));
    row("Hauteur du rebond", buf);
    const Color cc = l.controlled ? d.cfg.goodColor : d.cfg.badColor;
    row("Contrôle après la touche", percentText(l.controlFraction) + (l.controlled ? "  (gardée)" : "  (perdue)"), &cc);
    ImGui::Columns(1);

    if (l.tips.empty()) {
        ui::textColored(d.cfg.goodColor, "Rien à corriger sur cette touche.");
    } else {
        ui::separatorText("Conseils");
        for (size_t i = 0; i < l.tips.size(); ++i) {
            const Color c = i == 0 ? d.cfg.okColor : kDim;
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, toVec4(c));
            ImGui::TextWrapped("%s", l.tips[i].text.c_str());
            ImGui::PopStyleColor();
        }
    }
}

// ---- Session ----------------------------------------------------------------------------------------------
void renderSession(const UiContext& ctx, const FtUiData& d) {
    const auto& s = d.session;
    if (s.graded == 0 && s.dribbleContacts == 0) ImGui::TextDisabled("Aucune touche cette session.");
    ImGui::Columns(2, "##ft_session_cols", false);
    ImGui::SetColumnWidth(0, ImGui::GetFontSize() * 14.f);
    row("Touches notées", std::to_string(s.graded));
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.0f / 100 (meilleure %.0f)", static_cast<double>(s.averageScore()),
                  static_cast<double>(s.bestScore));
    row("Note moyenne", buf);
    row("Réussite", percentText(s.successRate()));
    row("Zone verte", percentText(s.goodZoneRate()), nullptr, "Part des touches avec un écart de vitesse dans la zone verte.");
    std::snprintf(buf, sizeof(buf), "%d (record %d)", s.streak, s.bestStreak);
    row("Série de réussites", buf);
    row("Écart moyen", ft::formatSpeedValue(s.meanSignedDiff(), d.unit), nullptr, "Positif : tu arrives en moyenne trop vite.");
    std::snprintf(buf, sizeof(buf), "%d (record %d touches, %.1f s)", s.dribbleContacts, s.bestChainContacts,
                  static_cast<double>(s.bestChainSeconds));
    row("Contacts de dribble", buf);
    if (d.dribble.contacts >= 2) {
        std::snprintf(buf, sizeof(buf), "%d touches, %.1f /s sur %.1f s", d.dribble.contacts, static_cast<double>(d.dribble.cadence),
                      static_cast<double>(d.dribble.seconds));
        row(d.dribble.active ? "Dribble en cours" : "Dernier dribble", buf, &kAccent);
    }
    row("Flip resets", std::to_string(s.flipResets));
    ImGui::Columns(1);

    if (!d.trend.empty()) ui::textColored(kAccent, d.trend);
    if (!d.persistent.empty()) ui::textColored(d.cfg.okColor, std::string("À travailler : ") + ft::tipShortLabel(d.persistent));

    if (!d.scores.empty()) {
        ui::separatorText("Dernières notes");
        char overlay[48];
        std::snprintf(overlay, sizeof(overlay), "%d dernières notes", static_cast<int>(d.scores.size()));
        ImGui::PlotLines("##ft_scores", d.scores.data(), static_cast<int>(d.scores.size()), 0, overlay, 0.f, 100.f,
                         ImVec2(-1.f, ImGui::GetFontSize() * 4.f));
    }

    ui::separatorText("Par type de touche");
    ImGui::Columns(3, "##ft_types", true);
    ImGui::TextDisabled("Type");
    ImGui::NextColumn();
    ImGui::TextDisabled("Session");
    ImGui::NextColumn();
    ImGui::TextDisabled("Depuis le début");
    ImGui::NextColumn();
    for (int i = 0; i < ft::kTouchTypeCount; ++i) {
        const auto t = static_cast<ft::TouchType>(i);
        const auto k = static_cast<size_t>(i);
        ImGui::TextUnformatted(ft::touchTypeTitle(t));
        ImGui::NextColumn();
        if (t == ft::TouchType::Dribble) {
            ImGui::Text("%d contacts", s.typeCount[k]);
            ImGui::NextColumn();
            ImGui::Text("%d contacts", d.total.typeCount[k]);
        } else {
            ImGui::Text("%d (moy. %.0f)", s.typeCount[k], static_cast<double>(s.typeAverage(t)));
            ImGui::NextColumn();
            ImGui::Text("%d (moy. %.0f)", d.total.typeCount[k], static_cast<double>(d.total.typeAverage(t)));
        }
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    std::snprintf(buf, sizeof(buf), "Depuis le début : %d touches, moyenne %.0f, meilleure série %d.", d.total.graded,
                  static_cast<double>(d.total.averageScore()), d.total.bestStreak);
    ImGui::TextDisabled("%s", buf);
    if (ImGui::Button("Remettre la session à zéro")) ctx.post(kModule, "reset_stats");
    hoverTip("Efface les stats de cette session, la carte des contacts et la courbe. Le cumul depuis le début est gardé.");
}

// ---- Heatmap ----------------------------------------------------------------------------------------------
void renderHeatmap(const FtUiData& d) {
    ImGui::TextWrapped("Vue de côté de la balle : ta voiture arrive par la gauche (flèche bleue). Chaque point est un de tes "
                       "40 derniers contacts, coloré selon sa note. Le dernier est entouré de blanc.");
    ImGui::BulletText("Réception au sol : vise juste sous le centre, du côté de ta voiture.");
    ImGui::BulletText("Amorti d'un lob et touche aérienne : vise le dessous de la balle.");
    ImGui::BulletText("Des points hauts = tu frappes la balle par-dessus : elle part loin de toi.");

    const float size = ImGui::GetFontSize() * 14.f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 c(origin.x + size * 0.55f, origin.y + size * 0.5f);
    const float R = size * 0.38f;
    dl->AddRectFilled(origin, ImVec2(origin.x + size * 1.1f, origin.y + size), IM_COL32(0, 0, 0, 120), 4.f);
    dl->AddCircleFilled(c, R, IM_COL32(255, 255, 255, 18), 48);
    dl->AddCircle(c, R, IM_COL32(255, 255, 255, 200), 48, 2.f);
    dl->AddLine(ImVec2(c.x - R, c.y), ImVec2(c.x + R, c.y), IM_COL32(255, 255, 255, 60));
    dl->AddLine(ImVec2(c.x, c.y - R), ImVec2(c.x, c.y + R), IM_COL32(255, 255, 255, 40));
    const float ay = c.y + R * 0.45f;
    const float ax1 = c.x - R - 8.f;
    const ImU32 accent = toU32(kAccent);
    dl->AddLine(ImVec2(ax1 - 24.f, ay), ImVec2(ax1, ay), accent, 3.f);
    dl->AddTriangleFilled(ImVec2(ax1 + 6.f, ay), ImVec2(ax1 - 3.f, ay - 6.f), ImVec2(ax1 - 3.f, ay + 6.f), accent);
    dl->AddText(ImVec2(c.x - 18.f, c.y - R - ImGui::GetFontSize() - 2.f), IM_COL32(200, 200, 200, 255), "dessus");
    dl->AddText(ImVec2(c.x - 22.f, c.y + R + 2.f), IM_COL32(200, 200, 200, 255), "dessous");
    const size_t n = d.heat.size();
    for (size_t i = 0; i < n; ++i) {
        const ft::HeatPoint& p = d.heat[i];
        const ImVec2 q(c.x + clampf(p.x, -1.f, 1.f) * R, c.y - clampf(p.z, -1.f, 1.f) * R);
        Color col = scoreColor(d.cfg, p.score);
        col.a = 110.f + 145.f * static_cast<float>(i + 1) / static_cast<float>(n);
        const bool newest = i + 1 == n;
        dl->AddCircleFilled(q, newest ? 6.f : 4.f, toU32(col), 12);
        if (newest) dl->AddCircle(q, 8.f, IM_COL32(255, 255, 255, 255), 16, 2.f);
    }
    ImGui::Dummy(ImVec2(size * 1.1f, size));
    if (n == 0) ImGui::TextDisabled("Pas encore de contact enregistré.");
}

// ---- Drills -----------------------------------------------------------------------------------------------
const char* phaseLabel(FirstTouchModule::DrillPhase p) {
    switch (p) {
    case FirstTouchModule::DrillPhase::Place: return "placement de la balle";
    case FirstTouchModule::DrillPhase::WaitTouch: return "à toi de jouer";
    case FirstTouchModule::DrillPhase::Evaluate: return "contrôle en cours d'analyse";
    case FirstTouchModule::DrillPhase::Pause: return "pause avant la balle suivante";
    case FirstTouchModule::DrillPhase::Idle: return "arrêté";
    }
    return "";
}

void renderDrills(const UiContext& ctx, const FtUiData& d) {
    const bool available = d.cfg.enabled && d.game.mode == GameMode::Freeplay && d.worldControl;
    if (d.drill.active) {
        std::string title = std::string("En cours : ") + ft::drillName(d.drill.type);
        if (d.drill.type == ft::DrillType::Mix) title += std::string(" (") + ft::drillName(d.drill.current) + ")";
        ui::textColored(kAccent, title);
        ImGui::Text("Niveau %d/100, %d/%d réussis, %s.", static_cast<int>(std::lround(d.drill.level * 100.f)), d.drill.successes,
                    d.drill.reps, phaseLabel(d.drill.phase));
        if (!d.drill.instruction.empty()) ImGui::TextWrapped("Consigne : %s", d.drill.instruction.c_str());
        if (!d.drill.lastOutcome.empty())
            ui::textColored(d.drill.lastSuccess ? d.cfg.goodColor : d.cfg.badColor, "Dernier essai : " + d.drill.lastOutcome);
        if (ImGui::Button("Arrêter l'exercice")) ctx.post(kModule, "stop");
        ImGui::SameLine();
        if (ImGui::Button("Balle suivante")) ctx.post(kModule, "skip");
        hoverTip("Replace une balle tout de suite, sans compter l'essai.");
        ImGui::Separator();
    } else if (!d.cfg.enabled) {
        ui::textColored(colors::Orange, "Active le module (réglages) pour lancer un exercice.");
    } else if (!available) {
        ui::textColored(colors::Orange, "Les exercices se lancent en freeplay. Ici, seule l'analyse des touches est active.");
    }
    ImGui::TextWrapped("Chaque exercice place la balle par rapport à ta voiture. Le niveau monte quand tu réussis et baisse "
                       "quand tu rates, pour viser environ %d %% de réussite : le bon niveau de défi pour progresser.",
                       static_cast<int>(std::lround(d.cfg.drillTarget * 100.f)));
    ImGui::TextWrapped("Sans aucune action pendant 3 balles de suite, l'exercice s'arrête : relance-le quand tu veux.");
    ImGui::Spacing();

    for (int i = 0; i <= static_cast<int>(ft::DrillType::Mix); ++i) {
        const auto t = static_cast<ft::DrillType>(i);
        ImGui::PushID(i);
        if (!available) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
        const bool clicked = ImGui::Button("Lancer");
        if (!available) ImGui::PopStyleVar();
        if (clicked && available) ctx.post(kModule, "drill", {ft::drillId(t)});
        ImGui::SameLine();
        ui::textColored(kAccent, ft::drillName(t));
        ImGui::SameLine();
        ImGui::TextDisabled("(rlt %s drill %s)", kModule, ft::drillId(t));
        ImGui::TextWrapped("%s", ft::drillDescription(t));
        if (t != ft::DrillType::Mix) {
            const auto& lv = d.levels[static_cast<size_t>(i)];
            char overlay[48];
            std::snprintf(overlay, sizeof(overlay), "Niveau %d/100", static_cast<int>(std::lround(lv.level * 100.f)));
            ImGui::ProgressBar(lv.level, ImVec2(ImGui::GetFontSize() * 12.f, 0.f), overlay);
            ImGui::SameLine();
            ImGui::TextDisabled("record %d, %d essais, %d/%d cette session", static_cast<int>(std::lround(lv.best * 100.f)),
                                lv.attempts, lv.sessionSuccesses, lv.sessionReps);
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (ImGui::SmallButton("Remettre les niveaux à zéro")) ctx.post(kModule, "reset_levels");
    hoverTip("Tous les exercices repartent du niveau 20/100.");
}

// ---- Commands ---------------------------------------------------------------------------------------------
void renderCommands() {
    ImGui::TextWrapped("Console BakkesMod (F6) ou touches liées avec \"bind\" :");
    ImGui::BulletText("rlt firsttouch drill <type> : lance un exercice (roll_in, roll_away, pass, lob, wall, high, mix).");
    ImGui::BulletText("rlt firsttouch stop : arrête l'exercice en cours.");
    ImGui::BulletText("rlt firsttouch skip : balle suivante sans compter l'essai.");
    ImGui::BulletText("rlt firsttouch reset_stats : remet la session à zéro.");
    ImGui::BulletText("rlt firsttouch reset_levels : remet les niveaux des exercices à zéro.");
    ImGui::BulletText("rlt firsttouch reset_all : efface toutes les stats de première touche.");
    ImGui::Spacing();
    ImGui::TextWrapped("Le log dans le chat (réglages, section Chat) écrit après chaque touche une ligne comme "
                       "\"[BON] Écart +3.2 km/h, note 84\", et \"[FLIP RESET]\" à chaque flip reset.");
}

} // namespace

void renderFirstTouchTab(UiContext& ctx) {
    const FtUiData d = copyState(ctx);
    if (!d.found) {
        ImGui::TextDisabled("Module Première touche indisponible.");
        return;
    }
    ImGui::TextWrapped("Chaque touche est analysée et notée sur 100 : vitesse au contact, point de contact sur la balle et "
                       "contrôle juste après. Les exercices adaptent leur difficulté à ton niveau.");
    if (!d.cfg.enabled) ui::textColored(colors::Orange, "Module désactivé (réglages > Analyse des touches).");
    else if (d.game.mode == GameMode::Online) ui::textColored(colors::Orange, "En ligne : le plugin est en pause.");
    else if (d.game.mode == GameMode::Workshop || d.game.mode == GameMode::CustomTraining)
        ImGui::TextDisabled("%s : analyse des touches active, exercices indisponibles.", gameModeLabel(d.game.mode));

    if (ImGui::BeginTabBar("##ft_tabs")) {
        if (ImGui::BeginTabItem("Dernière touche")) {
            renderLastTouch(d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Session")) {
            renderSession(ctx, d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Carte des contacts")) {
            renderHeatmap(d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Exercices")) {
            renderDrills(ctx, d);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Réglages")) {
            ui::moduleSettings(ctx, kModule);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Commandes")) {
            renderCommands();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace rlt
