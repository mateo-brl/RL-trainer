#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

#include "core/modules/kickoff/KickoffModule.h"
#include "imgui.h"
#include "plugin/ui/Ui.h"

namespace rlt {

namespace {

// Everything the tab shows, copied under the engine lock.
struct KickoffUiData {
    bool found = false;
    KickoffStatusView status;
    KickoffResultView last;
    std::array<KickoffSpawnStats, kKoSpawnCount> stats{};
    KickoffConfig cfg;
    bool worldControl = false;
    std::string activityOwner;
};

const Color kOk{90, 230, 120, 255};
const Color kBad{255, 110, 90, 255};
const Color kWarn{255, 200, 80, 255};
const Color kDim{170, 170, 170, 255};

std::string fmt(const char* format, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), format, v);
    return buf;
}

std::string secs(float s) { return s > 0.f ? fmt("%.2f s", s) : std::string("-"); }

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return kOk;
    case Verdict::Good: return Color(170, 230, 90, 255);
    case Verdict::Ok: return kWarn;
    default: return kBad;
    }
}

const char* phaseText(KickoffPhase p) {
    switch (p) {
    case KickoffPhase::Placing: return "placement";
    case KickoffPhase::Countdown: return "décompte";
    case KickoffPhase::Live: return "kickoff en cours";
    case KickoffPhase::Done: return "résultat";
    default: return "à l'arrêt";
    }
}

void startButtons(const UiContext& ctx, const KickoffUiData& d) {
    ImGui::Text("Position :");
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const KickoffSpawnInfo& info = kickoffSpawnInfo(kickoffSpawnFromIndex(i));
        if (i > 0) ImGui::SameLine();
        ImGui::PushID(i);
        if (ImGui::Button(info.name)) ctx.post("kickoff", "start", {info.key});
        ImGui::PopID();
    }
    ImGui::Text("Modes :");
    if (ImGui::Button("Aléatoire")) ctx.post("kickoff", "start", {"random"});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Une des 5 positions au hasard à chaque essai.");
    ImGui::SameLine();
    if (ImGui::Button("Série des 5")) ctx.post("kickoff", "start", {"sequence"});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Les 5 positions à la suite, puis ta note moyenne.");
    ImGui::SameLine();
    if (ImGui::Button("Ma faiblesse")) ctx.post("kickoff", "start", {"weakness"});
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toujours la position où ta note récente est la plus basse.");
    if (d.status.running) {
        ImGui::SameLine();
        if (ImGui::Button("Essai suivant")) ctx.post("kickoff", "next");
        ImGui::SameLine();
        if (ImGui::Button("Arrêter")) ctx.post("kickoff", "stop");
    }
}

void statusBlock(const KickoffUiData& d) {
    const KickoffStatusView& s = d.status;
    if (!s.enabled) {
        ui::textColored(kWarn, "Module désactivé (réglage \"Activer\" ci-dessous).");
        return;
    }
    if (s.running) {
        std::string line = std::string("En cours : ") + kickoffSpawnInfo(s.spawn).name +
                           (s.team == KickoffTeam::Orange ? " (orange)" : "") + ", " + phaseText(s.phase);
        if (s.phase == KickoffPhase::Countdown) line += " " + std::to_string(s.countdown);
        if (s.phase == KickoffPhase::Live || s.phase == KickoffPhase::Done) line += " (" + secs(s.liveTime) + ")";
        ui::textColored(kOk, line);
        std::string mode = std::string("Mode : ") + kickoffModeName(s.mode);
        if (s.mode == KickoffMode::Sequence)
            mode += "  (" + std::to_string(s.sequenceIndex) + "/" + std::to_string(s.sequenceTotal) + ")";
        ImGui::TextUnformatted(mode.c_str());
    } else if (!d.activityOwner.empty()) {
        ImGui::TextDisabled("Un autre exercice est en cours (%s) : lancer le kickoff l'arrêtera.",
                            d.activityOwner.c_str());
    } else {
        ImGui::TextDisabled("Aucun exercice kickoff en cours.");
    }
    if (!d.worldControl)
        ui::textColored(kDim,
                        "Ici le plugin ne peut pas déplacer la voiture : lance le freeplay pour l'exercice. "
                        "L'analyse de tes kickoffs (Reset en freeplay, matchs hors ligne) reste active.");
    if (s.sessionAttempts > 0)
        ImGui::Text("Session : %d essais, note moyenne %.0f/100", s.sessionAttempts,
                    static_cast<double>(s.sessionAvgScore));
    if (!s.message.empty()) ImGui::TextDisabled("%s", s.message.c_str());
}

void ghostBlock(const UiContext& ctx, const KickoffUiData& d) {
    const int current = std::max(0, std::min(d.cfg.ghostLevel, kKoGhostModeCount - 1));
    const char* preview = kickoffGhostModeName(static_cast<KickoffGhostMode>(current));
    ImGui::PushItemWidth(220.f);
    if (ImGui::BeginCombo("Adversaire fantôme##ko_ghost", preview)) {
        for (int i = 0; i < kKoGhostModeCount; ++i) {
            const bool selected = i == current;
            if (ImGui::Selectable(kickoffGhostModeName(static_cast<KickoffGhostMode>(i)), selected))
                ctx.setCvar("rlt_ko_ghost", std::to_string(i));
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ui::helpMarker(
        "Une voiture fantôme part de la position miroir (celle de l'adversaire) et arrive sur la balle au "
        "temps du niveau choisi. Débutant 2,40 s / Standard 2,07 s / Pro 1,95 s / Élite 1,88 s en diagonale. "
        "Mon record : ton meilleur essai sur cette position. Adaptatif : il accélère quand tu gagnes.");
    if (d.status.ghostOverridden)
        ImGui::TextDisabled("Choisi par la console : %s (jusqu'au prochain changement ici).",
                            kickoffGhostModeName(d.status.ghost));
    if (d.status.ghostFallback)
        ui::textColored(kWarn, "Pas encore de record sur cette position : fantôme Pro en attendant.");
    if (current == static_cast<int>(KickoffGhostMode::Adaptive) || d.status.ghost == KickoffGhostMode::Adaptive) {
        const std::string overlay = fmt("%.0f %%", static_cast<double>(d.status.adaptiveLevel * 100.f));
        ImGui::ProgressBar(d.status.adaptiveLevel, ImVec2(220.f, 0.f), overlay.c_str());
        ImGui::SameLine();
        ImGui::TextUnformatted("niveau du fantôme adaptatif (0 = Débutant, 100 = Élite)");
    }
}

void lastResultBlock(const KickoffUiData& d) {
    const KickoffResultView& r = d.last;
    const KickoffMetrics& m = r.metrics;
    std::string head = std::string(r.passive ? "Kickoff analysé : " : "Essai : ") + kickoffSpawnInfo(m.spawn).name;
    ImGui::TextUnformatted(head.c_str());
    if (m.touched) {
        ImGui::Text("Temps jusqu'à la balle : %.2f s", static_cast<double>(m.timeToTouch));
    } else if (m.estimated) {
        ImGui::Text("Temps estimé : ~%.2f s (l'adversaire a touché avant toi)", static_cast<double>(m.timeToTouch));
    } else {
        ui::textColored(kBad, "Balle pas touchée.");
    }
    if (r.newPb) {
        ImGui::SameLine();
        ui::textColored(kOk, "Nouveau record !");
    } else if (r.previousBest > 0.f) {
        ImGui::SameLine();
        ImGui::TextDisabled("(record %.2f s)", static_cast<double>(r.previousBest));
    }
    const Verdict v = verdictFromScore(static_cast<float>(r.grade.total));
    const std::string overlay = std::to_string(r.grade.total) + "/100  " + verdictLabel(v);
    const Color vc = verdictColor(v);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(vc.r / 255.f, vc.g / 255.f, vc.b / 255.f, 1.f));
    ImGui::ProgressBar(static_cast<float>(r.grade.total) / 100.f, ImVec2(260.f, 0.f), overlay.c_str());
    ImGui::PopStyleColor();
    std::string kind = std::string("Type : ") + kickoffTypeName(m.type);
    if (r.hasDuel) kind += std::string("   |   ") + kickoffDuelName(r.duel.outcome) + " contre " + r.opponent;
    ImGui::TextUnformatted(kind.c_str());
    ImGui::TextDisabled("Temps %.0f  Vitesse %.0f  Contact %.0f  Issue %.0f  Exécution %.0f (sur 100)",
                        static_cast<double>(r.grade.sTime), static_cast<double>(r.grade.sSpeed),
                        static_cast<double>(r.grade.sContact), static_cast<double>(r.grade.sOutcome),
                        static_cast<double>(r.grade.sExec));

    if (!r.splits.empty()) {
        ImGui::Columns(4, "##ko_splits", true);
        ImGui::TextDisabled("Repère");
        ImGui::NextColumn();
        ImGui::TextDisabled("Toi");
        ImGui::NextColumn();
        ImGui::TextDisabled("%s", r.hasDuel && !r.passive ? "Fantôme" : "Référence");
        ImGui::NextColumn();
        ImGui::TextDisabled("Écart");
        ImGui::NextColumn();
        for (const auto& s : r.splits) {
            ImGui::Text("%d %%", static_cast<int>(s.fraction * 100.f + 0.5f));
            ImGui::NextColumn();
            ImGui::TextUnformatted(s.reached ? secs(s.playerTime).c_str() : "-");
            ImGui::NextColumn();
            ImGui::TextUnformatted(secs(s.refTime).c_str());
            ImGui::NextColumn();
            if (s.reached) ui::textColored(s.delta <= 0.f ? kOk : kBad, fmt("%+.2f s", static_cast<double>(s.delta)));
            else ImGui::TextUnformatted("-");
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    if (ImGui::CollapsingHeader("Détails de l'essai##ko_details")) {
        ImGui::Indent();
        if (m.touched) {
            ImGui::Text("Vitesse vers la balle au contact : %.0f uu/s", static_cast<double>(m.speedToBall));
            ImGui::Text("Décalage du contact : %.0f uu (idéal < 15)", static_cast<double>(std::abs(m.lateralOffset)));
        }
        if (m.reaction >= 0.f) ImGui::Text("Réaction au GO : %.0f ms", static_cast<double>(m.reaction * 1000.f));
        else ImGui::TextUnformatted("Réaction au GO : ni boost ni accélérateur détectés");
        if (m.firstJump >= 0.f)
            ImGui::Text("Premier saut : %.2f s (idéal 0,45-0,55 s)", static_cast<double>(m.firstJump));
        if (m.dodge >= 0.f)
            ImGui::Text("Flip : %.2f s, angle %.0f° (idéal ~30°)", static_cast<double>(m.dodge),
                        static_cast<double>(std::abs(m.dodgeAngleDeg)));
        if (m.cancel >= 0.f)
            ImGui::Text("Annulation : %.0f ms après le flip", static_cast<double>((m.cancel - m.dodge) * 1000.f));
        else if (m.dodge >= 0.f) ImGui::TextUnformatted("Annulation : aucune");
        if (m.landing >= 0.f && m.dodge >= 0.f)
            ImGui::Text("Retour au sol : %.2f s après le flip", static_cast<double>(m.landing - m.dodge));
        ImGui::Text("Boost utilisé : %.0f, restant : %.0f, maintenu %.0f %% du temps", static_cast<double>(m.boostUsed),
                    static_cast<double>(m.boostAtTouch), static_cast<double>(m.boostHeldRatio * 100.f));
        if (m.boostLooksUnlimited)
            ui::textColored(kWarn, "Boost illimité détecté : coupe-le dans le freeplay pour un kickoff réaliste.");
        ImGui::Text("Écart moyen à la ligne droite : %.0f uu (max %.0f)", static_cast<double>(m.pathDevRms),
                    static_cast<double>(m.pathDevMax));
        if (m.hasBallExit)
            ImGui::Text("Direction de la balle : %.0f° (0 = vers le but adverse) %s",
                        static_cast<double>(m.ballExitAngleDeg), m.projectedY > 0.f ? "camp adverse" : "ton camp");
        ImGui::Unindent();
    }
    for (const auto& t : r.grade.tips) {
        ImGui::Bullet();
        ImGui::SameLine();
        ui::textColored(kWarn, kickoffTipText(t.tip));
    }
}

void statsBlock(const UiContext& ctx, const KickoffUiData& d) {
    ImGui::Columns(7, "##ko_stats", true);
    const char* heads[] = {"Position", "Essais", "Record", "Moy. 10", "Note moy.", "Vs fantôme", "Type favori"};
    for (const char* h : heads) {
        ImGui::TextDisabled("%s", h);
        ImGui::NextColumn();
    }
    ImGui::Separator();
    for (int i = 0; i < kKoSpawnCount; ++i) {
        const KickoffSpawnStats& s = d.stats[static_cast<size_t>(i)];
        ImGui::TextUnformatted(kickoffSpawnInfo(kickoffSpawnFromIndex(i)).name);
        ImGui::NextColumn();
        ImGui::Text("%d", s.attempts);
        ImGui::NextColumn();
        ImGui::TextUnformatted(secs(s.bestTime).c_str());
        ImGui::NextColumn();
        ImGui::TextUnformatted(secs(s.avgTime()).c_str());
        ImGui::NextColumn();
        if (s.avgScore() >= 0.f) ImGui::Text("%.0f", static_cast<double>(s.avgScore()));
        else ImGui::TextUnformatted("-");
        ImGui::NextColumn();
        if (s.duels > 0) ImGui::Text("%s (%d)", ui::percent(s.winRate()).c_str(), s.duels);
        else ImGui::TextUnformatted("-");
        ImGui::NextColumn();
        ImGui::TextUnformatted(s.attempts > 0 ? kickoffTypeName(s.favouriteType()) : "-");
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    // Recent scores of the last spawn played.
    const KickoffSpawn plotted = d.last.valid ? d.last.metrics.spawn : d.status.spawn;
    const KickoffSpawnStats& ps = d.stats[static_cast<size_t>(kickoffIndex(plotted))];
    if (ps.recentScores.size() >= 2) {
        float values[kKoRecentCount];
        int n = 0;
        for (int v : ps.recentScores) values[n++] = static_cast<float>(v);
        const std::string label = std::string("Notes récentes : ") + kickoffSpawnInfo(plotted).name;
        ImGui::PlotLines("##ko_recent", values, n, 0, label.c_str(), 0.f, 100.f, ImVec2(0.f, 60.f));
    }

    if (ImGui::Button("Remettre les stats à zéro")) ImGui::OpenPopup("##ko_reset_confirm");
    if (ImGui::BeginPopup("##ko_reset_confirm")) {
        ImGui::TextUnformatted("Effacer toutes tes stats et records de kickoff ?");
        if (ImGui::Button("Oui, effacer")) {
            ctx.post("kickoff", "reset_stats");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Annuler")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void guideBlock() {
    if (ImGui::CollapsingHeader("Le speedflip, pas à pas##ko_guide_sf")) {
        ImGui::Indent();
        ImGui::BulletText("Pendant le décompte : maintiens déjà boost + accélérateur, tout part à l'instant du GO.");
        ImGui::BulletText("Corrige l'angle tout de suite : ta voiture ne vise pas exactement la balle au départ.");
        ImGui::BulletText("Premier saut court vers 0,45-0,55 s (environ 1000 uu/s), boost toujours enfoncé.");
        ImGui::BulletText("Flip diagonal avant (environ 30° du nez) moins de 0,15 s après le saut.");
        ImGui::BulletText("Annule tout de suite : stick à fond en arrière en moins de 0,1 s.");
        ImGui::BulletText("Finis la vrille avec l'air roll du même côté pour retomber vite sur les roues.");
        ImGui::BulletText("Garde le boost jusqu'à la panne : le petit boost sur ta route te redonne +12.");
        ImGui::TextWrapped(
            "Gain attendu : 0,1 à 0,2 s par rapport à un front flip classique, ce qui suffit à gagner "
            "la plupart des kickoffs.");
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Qu'est-ce qui gagne un 50/50 ?##ko_guide_duel")) {
        ImGui::Indent();
        ImGui::BulletText("Arriver le premier : avec 0,1 s d'avance, tu gagnes presque toujours.");
        ImGui::BulletText("Frapper le centre de la balle, côté de ton propre but : la balle part dans l'axe.");
        ImGui::BulletText("Flipper dans la balle au dernier moment ajoute de la puissance au contact.");
        ImGui::BulletText("Plus de vitesse au contact = plus de force : ne relâche pas le boost trop tôt.");
        ImGui::TextWrapped(
            "Le fantôme applique ces règles : si vous arrivez à moins de 0,1 s d'écart, la puissance "
            "(vitesse, centrage, flip) décide. S'il gagne, la balle part vers ton camp.");
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Commandes console (F6)##ko_guide_cmd")) {
        ImGui::Indent();
        ImGui::BulletText("rlt kickoff start [diagonal_left|diagonal_right|offcenter_left|offcenter_right|back]");
        ImGui::BulletText("rlt kickoff start [random|sequence|weakness]");
        ImGui::BulletText("rlt kickoff next  (essai suivant tout de suite)");
        ImGui::BulletText("rlt kickoff stop");
        ImGui::BulletText("rlt kickoff ghost <pro|elite|standard|beginner|pb|adaptive|off>");
        ImGui::BulletText("rlt kickoff reset_stats");
        ImGui::TextWrapped("Astuce : le bouton Reset du freeplay relance tout de suite l'essai suivant.");
        ImGui::Unindent();
    }
}

} // namespace

void renderKickoffTab(UiContext& ctx) {
    KickoffUiData d;
    {
        auto lk = ctx.engine.lock();
        auto* mod = ctx.engine.get<KickoffModule>();
        if (mod) {
            d.found = true;
            d.status = mod->status();
            d.last = mod->lastResult();
            d.stats = mod->spawnStats();
            d.cfg = mod->config();
        }
        d.worldControl = ctx.engine.worldControlAllowed();
        d.activityOwner = ctx.engine.activityOwner();
    }
    if (!d.found) {
        ImGui::TextDisabled("Module Kickoff indisponible.");
        return;
    }

    ImGui::TextWrapped(
        "Kickoff comme en match : la voiture est figée pendant le 3, 2, 1, puis GO. Un fantôme part de la "
        "position adverse, tu es chronométré jusqu'à la balle et noté sur 100 avec des conseils.");

    ui::separatorText("Exercice");
    statusBlock(d);
    startButtons(ctx, d);

    ui::separatorText("Fantôme");
    ghostBlock(ctx, d);

    if (d.last.valid) {
        ui::separatorText("Dernier essai");
        lastResultBlock(d);
    }

    ui::separatorText("Statistiques par position");
    statsBlock(ctx, d);

    ui::separatorText("Comprendre");
    guideBlock();

    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "kickoff");
}

} // namespace rlt
