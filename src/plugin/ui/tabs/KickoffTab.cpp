#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <initializer_list>
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
    std::string activityOwner;  // module id of the running drill ("shooting")
    std::string activityName;   // its French name ("Tirs"), shown to the player
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

// Coloured text that wraps at the window edge (tips, advice).
void wrappedColored(const Color& c, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

// Numbered list of plain steps.
void steps(std::initializer_list<const char*> list) {
    int n = 1;
    for (const char* step : list) ImGui::TextWrapped("%d. %s", n++, step);
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
        const std::string& other = d.activityName.empty() ? d.activityOwner : d.activityName;
        ImGui::TextDisabled("Un autre exercice est en cours (%s) : lancer le kickoff l'arrêtera.", other.c_str());
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
    // Pro or Élite ghost, or Pro or Élite grading (speedflip expected): offer the beginner setting in one click.
    const bool hardGhost =
        current == static_cast<int>(KickoffGhostMode::Pro) || current == static_cast<int>(KickoffGhostMode::Elite);
    if (hardGhost || d.cfg.gradeTarget >= static_cast<int>(KickoffLevel::Pro)) {
        wrappedColored(kWarn, "Débutant : le fantôme Adaptatif part lentement et accélère quand tu gagnes. La "
                              "référence Standard note un kickoff avec des flips, sans speedflip.");
        if (ImGui::Button("Réglage débutant : fantôme Adaptatif, référence Standard##ko_beginner")) {
            ctx.setCvar("rlt_ko_ghost", std::to_string(static_cast<int>(KickoffGhostMode::Adaptive)));
            ctx.setCvar("rlt_ko_grade_target", std::to_string(static_cast<int>(KickoffLevel::Standard)));
        }
    }
    if (d.status.ghostFallback)
        ui::textColored(kWarn, std::string("Pas encore de record sur cette position : fantôme ") +
                                   kickoffLevelName(kickoffLevelFromIndex(d.cfg.gradeTarget)) + " en attendant.");
    if (current == static_cast<int>(KickoffGhostMode::Adaptive) || d.status.ghost == KickoffGhostMode::Adaptive) {
        const std::string overlay = fmt("%.0f %%", static_cast<double>(d.status.adaptiveLevel * 100.f));
        ui::progressBar(d.status.adaptiveLevel, overlay, 220.f);
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
    ui::progressBar(static_cast<float>(r.grade.total) / 100.f, overlay, 260.f);
    ImGui::PopStyleColor();
    if (r.hasDuel) {
        const std::string duel = std::string(kickoffDuelName(r.duel.outcome)) + " contre " + r.opponent;
        ui::textColored(kickoffDuelWon(r.duel.outcome) ? kOk : kWarn, duel);
    }
    // The one thing to work on, in words.
    if (!r.grade.tips.empty()) {
        const KickoffTip top = r.grade.tips.front().tip;
        wrappedColored(top == KickoffTip::GreatKickoff ? kOk : kWarn,
                       std::string(top == KickoffTip::GreatKickoff ? "" : "À travailler : ") + kickoffTipText(top));
    }
    // Safety net: the drill limits the boost at every placement, so this mostly concerns passive kickoffs.
    if (m.boostLooksUnlimited)
        wrappedColored(kWarn, r.passive ? "Boost illimité détecté : record non compté. Lance l'exercice : il le "
                                          "coupe pendant tes essais et le remet à la fin."
                                        : "Boost illimité détecté : record non compté. Le plugin n'a pas pu le "
                                          "couper ici : coupe-le dans les réglages du freeplay.");

    if (!ImGui::CollapsingHeader("Détails techniques##ko_last")) return;
    ImGui::Indent();
    ImGui::Text("Type : %s", kickoffTypeName(m.type));
    ImGui::TextDisabled("Notes : temps %.0f, vitesse %.0f, contact %.0f, issue %.0f, exécution %.0f (sur 100)",
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
    if (m.touched) {
        ImGui::Text("Vitesse vers la balle au contact : %.0f uu/s", static_cast<double>(m.speedToBall));
        ImGui::Text("Décalage du contact : %.0f uu (idéal < 15)", static_cast<double>(std::abs(m.lateralOffset)));
    }
    if (m.reaction >= 0.f) ImGui::Text("Réaction au GO : %.0f ms", static_cast<double>(m.reaction * 1000.f));
    else ImGui::TextUnformatted("Réaction au GO : ni boost ni accélérateur détectés");
    if (m.firstJump >= 0.f) ImGui::Text("Premier saut : %.2f s (idéal 0,45-0,55 s)", static_cast<double>(m.firstJump));
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
    ImGui::Text("Écart moyen à la ligne droite : %.0f uu (max %.0f)", static_cast<double>(m.pathDevRms),
                static_cast<double>(m.pathDevMax));
    if (m.hasBallExit)
        ImGui::Text("Direction de la balle : %.0f° (0 = vers le but adverse), %s",
                    static_cast<double>(m.ballExitAngleDeg), m.projectedY > 0.f ? "camp adverse" : "ton camp");
    if (r.grade.tips.size() > 1) {
        ImGui::TextUnformatted("Autres points :");
        for (size_t i = 1; i < r.grade.tips.size(); ++i) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", kickoffTipText(r.grade.tips[i].tip));
        }
    }
    if (!r.grade.tips.empty()) {
        ImGui::TextUnformatted("Les mesures derrière ces conseils :");
        for (const auto& t : r.grade.tips) {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", kickoffTipDetail(t.tip));
        }
    }
    ImGui::Unindent();
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

// Kickoff techniques from the simplest to the hardest. Plain steps first, the numbers in "Détails techniques".
void guideBlock() {
    wrappedColored(kOk, "Débutant : commence par le kickoff simple contre le fantôme Adaptatif. Passe au kickoff à "
                        "deux flips quand tu gagnes souvent, puis au speedflip.");
    if (ImGui::CollapsingHeader("Kickoff simple (Débutant)##ko_guide_simple")) {
        ImGui::Indent();
        steps({"Pendant le décompte, appuie déjà sur boost et accélérateur.",
               "Fonce tout droit vers la balle, boost enfoncé.",
               "Juste avant la balle, flippe vers l'avant dans la balle."});
        ImGui::TextWrapped("Pourquoi : la voiture qui arrive la première pousse la balle, et le flip au contact "
                           "lui donne plus de force.");
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Kickoff à deux flips (Intermédiaire)##ko_guide_double")) {
        ImGui::Indent();
        steps({"Pendant le décompte, appuie déjà sur boost et accélérateur.",
               "Fonce vers la balle et passe sur le petit boost s'il est sur ta route.",
               "À peu près à mi-chemin, flippe vers l'avant pour prendre de la vitesse.",
               "Retombe sur tes roues, boost toujours enfoncé.",
               "Juste avant la balle, flippe une deuxième fois dans la balle."});
        ImGui::TextWrapped("Pourquoi : un flip en route va plus vite que la voiture au sol, tu arrives plus tôt "
                           "qu'avec le kickoff simple.");
        if (ImGui::CollapsingHeader("Détails techniques##ko_guide_double_tech")) {
            ImGui::Indent();
            ImGui::TextWrapped("C'est le niveau de référence Standard : 2,07 s en diagonale, 2,30 s en décalé, "
                               "2,63 s au centre arrière.");
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Speedflip (Avancé)##ko_guide_sf")) {
        ImGui::Indent();
        steps({"Boost et tourne un peu.", "Saute.", "Flip en diagonale vers l'avant.",
               "Tire tout de suite le stick vers toi pour annuler.",
               "Finis la vrille avec l'air roll et retombe sur tes roues.",
               "Garde le boost jusqu'à la balle, puis flippe dans la balle."});
        ImGui::TextWrapped("Pourquoi : le flip annulé donne la vitesse d'un flip sans faire tourner la voiture. "
                           "Tu gardes le boost tout le temps et tu arrives avant un kickoff à deux flips.");
        ImGui::TextWrapped("%s", "Pour l'apprendre geste par geste, utilise l'exercice Speedflip de la page "
                                 "Mécas mouvement.");
        if (ImGui::CollapsingHeader("Détails techniques##ko_guide_sf_tech")) {
            ImGui::Indent();
            ImGui::BulletText("%s", "Premier saut court vers 0,45-0,55 s après le GO (environ 1000 uu/s).");
            ImGui::BulletText("%s", "Flip à environ 30° du nez (entre 20° et 45°), moins de 0,15 s après le saut.");
            ImGui::BulletText("%s", "Annulation : stick à fond en arrière moins de 0,1 s après le flip.");
            ImGui::BulletText("%s", "Retour au sol idéal moins de 0,85 s après le flip.");
            ImGui::BulletText("%s", "Le petit boost sur ta route redonne +12 de boost.");
            ImGui::TextWrapped("Dans les références du plugin, un speedflip arrive 0,10 à 0,15 s avant un kickoff à "
                               "deux flips (diagonale : 1,95 s contre 2,07 s).");
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Qu'est-ce qui gagne un 50/50 ?##ko_guide_duel")) {
        ImGui::Indent();
        ImGui::BulletText("%s", "Arriver le premier : la première voiture sur la balle la pousse avant l'autre.");
        ImGui::BulletText("%s", "Frapper le centre de la balle avec l'avant de ta voiture.");
        ImGui::BulletText("%s", "Flipper dans la balle juste avant le contact pour ajouter de la force.");
        ImGui::BulletText("%s", "Garder de la vitesse jusqu'au contact : ne lâche pas le boost trop tôt.");
        if (ImGui::CollapsingHeader("Détails techniques##ko_guide_duel_tech")) {
            ImGui::Indent();
            ImGui::TextWrapped(
                "Règle du fantôme de cet exercice : avec plus de 0,10 s d'écart (réglage Avance décisive), le "
                "premier arrivé gagne. En dessous, la puissance décide : vitesse au contact, centrage, flip dans la "
                "balle. S'il gagne, la balle part vers ton camp. Cette règle est une estimation du plugin, pas une "
                "mesure du jeu.");
            ImGui::Unindent();
        }
        ImGui::Unindent();
    }
    if (ImGui::CollapsingHeader("Commandes console (F6)##ko_guide_cmd")) {
        ImGui::Indent();
        ImGui::BulletText("%s", "rlt kickoff start [diagonal_left|diagonal_right|offcenter_left|offcenter_right|back]");
        ImGui::BulletText("%s", "rlt kickoff start [random|sequence|weakness]");
        ImGui::BulletText("%s", "rlt kickoff next  (essai suivant tout de suite)");
        ImGui::BulletText("%s", "rlt kickoff stop");
        ImGui::BulletText("%s", "rlt kickoff ghost <pro|elite|standard|beginner|pb|adaptive|off>");
        ImGui::BulletText("%s", "rlt kickoff reset_stats");
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
        d.activityName = ctx.engine.activityName();
    }
    if (!d.found) {
        ImGui::TextDisabled("Module Kickoff indisponible.");
        return;
    }

    // Built from the settings: the countdown length, the game's countdown and the boost at GO can be changed.
    std::string numbers;
    for (int n = std::clamp(d.cfg.countdownSeconds, 1, 5); n >= 1; --n)
        numbers += std::to_string(n) + (n > 1 ? ", " : "");
    const std::string intro =
        "Kickoff comme en match : la voiture est figée pendant le " + numbers +
        (d.cfg.nativeCountdown ? " du jeu" : "") +
        ", puis GO. Un fantôme part de la position adverse. Après chaque essai tu as une note sur 100 et un conseil. "
        "À chaque essai, tu pars avec " + std::to_string(static_cast<int>(std::lround(d.cfg.spawnBoost))) +
        " de boost, les boosts du terrain sont remis pendant le décompte et le boost illimité est coupé (tes "
        "réglages reviennent à la fin).";
    ImGui::TextWrapped("%s", intro.c_str());

    ui::separatorText("Exercice");
    statusBlock(d);
    startButtons(ctx, d);

    ui::separatorText("Fantôme");
    ghostBlock(ctx, d);

    if (d.last.valid) {
        ui::separatorText("Dernier essai");
        lastResultBlock(d);
    }

    ui::separatorText("Apprendre le kickoff");
    guideBlock();

    ui::separatorText("Statistiques par position");
    statsBlock(ctx, d);

    ui::separatorText("Réglages");
    ui::moduleSettings(ctx, "kickoff");
}

} // namespace rlt
