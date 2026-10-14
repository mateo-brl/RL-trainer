#include "core/modules/kickoff/KickoffGrading.h"

#include <algorithm>
#include <cmath>

namespace rlt {

namespace {

constexpr float kTipMinLoss = 2.5f;       // tips costing less than this are not shown
constexpr float kMissedPadMinLoss = 2.6f;  // above kTipMinLoss: the pad tip can surface when nothing bigger is wrong

// Linear map of v to 0..100: `best` -> 100, `worst` -> 0 (either order), clamped.
float lin(float v, float best, float worst) { return rampScore(v, best, worst) * 100.f; }

float outcomeScore(KickoffDuel d) {
    switch (d) {
    case KickoffDuel::PlayerClear: return 100.f;
    case KickoffDuel::PlayerNarrow: return 80.f;
    case KickoffDuel::Neutral: return 55.f;
    case KickoffDuel::GhostNarrow: return 25.f;
    default: return 0.f;
    }
}

} // namespace

const char* kickoffTipText(KickoffTip t) {
    switch (t) {
    case KickoffTip::NoTouch: return "Tu n'as pas touché la balle : fonce au centre dès le GO.";
    case KickoffTip::LateArrival: return "Tu arrives trop tard : garde le boost enfoncé jusqu'à la balle.";
    case KickoffTip::NoBoostAtGo: return "Appuie sur boost et accélérateur pendant le décompte.";
    case KickoffTip::SlowReaction: return "Tu pars en retard : appuie avant la fin du décompte.";
    case KickoffTip::BoostReleased: return "Tu lâches le boost en route : garde-le enfoncé.";
    case KickoffTip::EarlyFirstJump: return "Tu sautes trop tôt : prends un peu de vitesse avant.";
    case KickoffTip::LateFirstJump: return "Tu sautes trop tard : saute un peu plus tôt.";
    case KickoffTip::SlowDodge: return "Tu flippes trop tard : flippe juste après le saut.";
    case KickoffTip::NoCancel: return "Annule ton flip : tire le stick vers toi juste après.";
    case KickoffTip::LateCancel: return "Annule plus vite : tire le stick vers toi dès le flip.";
    case KickoffTip::DodgeTooStraight: return "Flippe plus en diagonale vers l'avant.";
    case KickoffTip::DodgeTooWide: return "Flippe moins sur le côté : stick plus vers l'avant.";
    case KickoffTip::LateLanding: return "Retombe vite sur tes roues : finis la vrille à l'air roll.";
    case KickoffTip::PathDeviation: return "Va droit sur la balle : corrige ta direction avant de sauter.";
    case KickoffTip::OffCenterContact: return "Frappe la balle bien au centre avec l'avant de ta voiture.";
    case KickoffTip::LowContactSpeed: return "Arrive plus fort sur la balle : ne lâche pas le boost.";
    case KickoffTip::BallToOwnHalf: return "La balle repart chez toi : arrive plus vite, frappe au centre.";
    case KickoffTip::FlipIntoBall: return "Flippe dans la balle juste avant le contact pour la pousser.";
    case KickoffTip::MissedPad: return "Passe sur le petit boost de ta route : il te recharge.";
    case KickoffTip::TrySpeedflip:
        return "Pour gagner du temps, apprends le speedflip (page Mécas mouvement, exercice Speedflip).";
    case KickoffTip::GreatKickoff: return "Excellent kickoff ! Garde ce rythme et change de position.";
    default: return "";
    }
}

const char* kickoffTipDetail(KickoffTip t) {
    switch (t) {
    case KickoffTip::NoTouch:
        return "Aucune touche avant la fin du temps : la note dépend seulement de la distance parcourue (20 au plus).";
    case KickoffTip::LateArrival:
        return "Temps jusqu'à la balle au-dessus de la référence : garde le boost du GO jusqu'à la panne (33 de "
               "boost, environ 1 s) et flippe tôt.";
    case KickoffTip::NoBoostAtGo:
        return "Ni boost ni accélérateur au GO : ce qui est tenu pendant le décompte part à l'instant du GO.";
    case KickoffTip::SlowReaction:
        return "Réaction au GO notée de 1 image (100) à 0,20 s (0) : pré-appuie boost + accélérateur avant la fin "
               "du 1.";
    case KickoffTip::BoostReleased:
        return "Boost tenu moins de 97 % du temps de l'approche (0 point sous 60 %).";
    case KickoffTip::EarlyFirstJump:
        return "Premier saut avant 0,38 s : attends environ 0,45 s (vers 1000-1100 uu/s) avant de sauter.";
    case KickoffTip::LateFirstJump:
        return "Premier saut après 0,60 s : saute vers 0,45-0,55 s après le GO pour profiter du boost en l'air.";
    case KickoffTip::SlowDodge:
        return "Flip plus de 0,15 s après le saut (0 point à 0,35 s) : enchaîne le 2e appui plus vite.";
    case KickoffTip::NoCancel:
        return "Aucune annulation dans les 0,25 s après le flip : stick à fond en arrière juste après le flip.";
    case KickoffTip::LateCancel:
        return "Annulation plus de 0,10 s après le flip (0 point à 0,25 s) : vise moins de 0,1 s.";
    case KickoffTip::DodgeTooStraight:
        return "Angle du flip sous 20° : vise environ 30° (entre 20° et 45°) en diagonale avant.";
    case KickoffTip::DodgeTooWide:
        return "Angle du flip au-delà de 45° (0 point à 75°) : rapproche le stick de l'avant.";
    case KickoffTip::LateLanding:
        return "Retour au sol plus de 0,85 s après le flip (0 point à 1,3 s) : air roll pendant l'annulation.";
    case KickoffTip::PathDeviation:
        return "Écart moyen à la ligne droite au-delà de 35 uu (100 uu en décalé, où le détour par le boost est "
               "permis) : corrige l'angle avant de sauter, pas pendant le flip.";
    case KickoffTip::OffCenterContact:
        return "Décalage du contact au-delà de 15 uu (0 point à 75 uu) : frappe le centre de la balle, côté de ton "
               "propre but.";
    case KickoffTip::LowContactSpeed:
        return "Vitesse vers la balle au contact sous la référence du niveau choisi (0 point à 1300 uu/s).";
    case KickoffTip::BallToOwnHalf:
        return "Direction de la balle notée de 30° (100) à 150° (0) ; ici elle repart vers ton camp.";
    case KickoffTip::FlipIntoBall:
        return "Un flip lancé moins de 0,35 s avant le contact compte comme flip dans la balle (idéal vers 0,25 s).";
    case KickoffTip::MissedPad:
        return "Petit boost de ta ligne raté : +12 de boost, environ 0,04 à 0,06 s de gagné.";
    case KickoffTip::TrySpeedflip:
        return "Front flip ou sans flip : dans les références du plugin, un speedflip arrive 0,10 à 0,15 s plus tôt "
               "(diagonale : 1,95 s contre 2,07 s).";
    case KickoffTip::GreatKickoff: return "Aucune erreur ne coûte plus de 2,5 points.";
    default: return "";
    }
}

const char* kickoffTipCode(KickoffTip t) {
    switch (t) {
    case KickoffTip::NoTouch: return "kickoff.no_touch";
    case KickoffTip::LateArrival: return "kickoff.late_arrival";
    case KickoffTip::NoBoostAtGo: return "kickoff.no_boost_at_go";
    case KickoffTip::SlowReaction: return "kickoff.slow_reaction";
    case KickoffTip::BoostReleased: return "kickoff.boost_released";
    case KickoffTip::EarlyFirstJump: return "kickoff.early_first_jump";
    case KickoffTip::LateFirstJump: return "kickoff.late_first_jump";
    case KickoffTip::SlowDodge: return "kickoff.slow_dodge";
    case KickoffTip::NoCancel: return "kickoff.no_cancel";
    case KickoffTip::LateCancel: return "kickoff.late_cancel";
    case KickoffTip::DodgeTooStraight: return "kickoff.dodge_too_straight";
    case KickoffTip::DodgeTooWide: return "kickoff.dodge_too_wide";
    case KickoffTip::LateLanding: return "kickoff.late_landing";
    case KickoffTip::PathDeviation: return "kickoff.path_deviation";
    case KickoffTip::OffCenterContact: return "kickoff.off_center_contact";
    case KickoffTip::LowContactSpeed: return "kickoff.low_contact_speed";
    case KickoffTip::BallToOwnHalf: return "kickoff.ball_to_own_half";
    case KickoffTip::FlipIntoBall: return "kickoff.flip_into_ball";
    case KickoffTip::MissedPad: return "kickoff.missed_pad";
    case KickoffTip::TrySpeedflip: return "kickoff.try_speedflip";
    case KickoffTip::GreatKickoff: return "kickoff.great";
    default: return "kickoff.unknown";
    }
}

KickoffGrade kickoffGrade(const KickoffMetrics& m, KickoffLevel target, const KickoffDuelResult* duel,
                          const KickoffGradeWeights& w) {
    KickoffGrade g;
    std::vector<KickoffTipLoss> cands;
    auto add = [&](KickoffTip t, float loss) {
        if (loss > 0.f) cands.push_back({t, loss});
    };

    if (!m.touched && !m.estimated) {
        const float len = kickoffPath(m.spawn, m.team).length;
        g.total = std::clamp(static_cast<int>(std::lround(lin(m.progress, len, 0.f) * 0.2f)), 0, 20);
        g.tips.push_back({KickoffTip::NoTouch, 100.f - static_cast<float>(g.total)});
        return g;
    }
    const KickoffBenchmark b = kickoffBenchmark(m.spawn, target);
    const KickoffBenchmark slow = kickoffBenchmark(m.spawn, KickoffLevel::Beginner);

    // Time: target -> 100, beginner + 0.25 s -> 0.
    g.sTime = lin(m.timeToTouch, b.arrive, slow.arrive + 0.25f);
    // The part of the delay explained by a late start is blamed on the reaction, not on the approach.
    const float timeLoss = (100.f - g.sTime) * w.time;
    const float span = std::max(0.05f, slow.arrive + 0.25f - b.arrive);
    const float reactionLoss = m.reaction > 0.05f ? std::min(timeLoss, m.reaction / span * 100.f * w.time) : 0.f;
    add(KickoffTip::LateArrival, timeLoss - reactionLoss);
    add(KickoffTip::SlowReaction, reactionLoss);
    // Contact speed toward the ball.
    g.sSpeed = lin(m.speedToBall, b.contactSpeed, 1300.f);
    add(KickoffTip::LowContactSpeed, (100.f - g.sSpeed) * w.speed);
    // Centred contact (unknown when the arrival was estimated: neutral 50, no tip).
    if (m.estimated) {
        g.sContact = 50.f;
    } else {
        g.sContact = lin(std::fabs(m.lateralOffset), 15.f, 75.f);
        add(KickoffTip::OffCenterContact, (100.f - g.sContact) * w.contact);
    }
    // Outcome: duel verdict if a ghost (or a real opponent) took part, else the ball exit direction.
    if (duel) g.sOutcome = outcomeScore(duel->outcome);
    else if (m.hasBallExit) g.sOutcome = lin(m.ballExitAngleDeg, 30.f, 150.f);
    else g.sOutcome = 50.f;
    const float outcomeLoss = (100.f - g.sOutcome) * w.outcome;
    if (duel && duel->outcome == KickoffDuel::GhostClear) {
        add(KickoffTip::LateArrival, outcomeLoss);  // lost on time: arriving earlier is the fix
    } else if (duel && (duel->outcome == KickoffDuel::Neutral || duel->outcome == KickoffDuel::GhostNarrow)) {
        if (!m.flipIntoBall) add(KickoffTip::FlipIntoBall, outcomeLoss);
        else add(KickoffTip::OffCenterContact, outcomeLoss * 0.5f);
    } else if (!duel && m.hasBallExit && m.projectedY < -256.f) {
        add(KickoffTip::BallToOwnHalf, outcomeLoss + 1.f);
    }

    // Execution: reaction, boost discipline, path, flip timings (speedflip-centric).
    g.sReact = m.reaction < 0.f ? 0.f : lin(m.reaction, kTickDt, 0.20f);
    g.sBoost = lin(m.boostHeldRatio, 0.97f, 0.6f);
    const float tolDev =
        kickoffKind(m.spawn) == KickoffSpawnKind::OffCenter ? 200.f : 70.f;  // off-centre: pad detour ok
    g.sPath = lin(m.pathDevRms, tolDev * 0.5f, tolDev * 2.5f);
    g.speedflipAttempt = m.type == KickoffType::Speedflip ||
                         (m.firstJump >= 0.f && m.dodge >= 0.f && m.dodge <= 1.1f && std::fabs(m.dodgeSide) >= 0.15f);
    const bool wantSpeedflip = static_cast<int>(target) >= static_cast<int>(KickoffLevel::Pro);
    // Below Pro the reference kickoff uses plain flips: an early diagonal flip is only judged as a speedflip when it
    // really was one (cancelled), so a beginner is not told to cancel a flip they never meant as a speedflip.
    const bool judgeSpeedflip = g.speedflipAttempt && (wantSpeedflip || m.type == KickoffType::Speedflip);
    const float e = w.exec;
    if (judgeSpeedflip) {
        const float sJump = m.firstJump < 0.38f ? lin(m.firstJump, 0.38f, 0.20f) : lin(m.firstJump, 0.60f, 0.85f);
        const float sGap = lin(m.dodge - m.firstJump, 0.15f, 0.35f);
        const float sCancel = m.cancel < 0.f ? 0.f : lin(m.cancel - m.dodge, 0.10f, 0.25f);
        const float ang = std::fabs(m.dodgeAngleDeg);
        const float sAng = ang < 20.f ? lin(ang, 20.f, 0.f) : lin(ang, 45.f, 75.f);
        const float sLand = m.landing < 0.f ? 50.f : lin(m.landing - m.dodge, 0.85f, 1.3f);
        g.sFlip = (sJump + sGap + sCancel + sAng + sLand) / 5.f;
        // Flip sub-tips are weighted like a whole execution part (0.2): a bad cancel or angle is the root cause
        // of the time lost, which the time criterion alone would attribute to "arrive earlier".
        if (m.firstJump < 0.38f) add(KickoffTip::EarlyFirstJump, (100.f - sJump) * e * 0.2f);
        if (m.firstJump > 0.60f) add(KickoffTip::LateFirstJump, (100.f - sJump) * e * 0.2f);
        add(KickoffTip::SlowDodge, (100.f - sGap) * e * 0.2f);
        if (m.cancel < 0.f) add(KickoffTip::NoCancel, 100.f * e * 0.2f + 0.5f);
        else add(KickoffTip::LateCancel, (100.f - sCancel) * e * 0.2f);
        if (ang < 20.f) add(KickoffTip::DodgeTooStraight, (100.f - sAng) * e * 0.2f);
        else add(KickoffTip::DodgeTooWide, (100.f - sAng) * e * 0.2f);
        add(KickoffTip::LateLanding, (100.f - sLand) * e * 0.2f);
    } else {
        g.sFlip = m.dodges > 0 ? 60.f : 40.f;  // front / diagonal / double flip or no flip: valid but slower
        if (wantSpeedflip) add(KickoffTip::TrySpeedflip, (100.f - g.sFlip) * e * 0.4f);
    }
    g.sExec = 0.2f * g.sReact + 0.2f * g.sBoost + 0.2f * g.sPath + 0.4f * g.sFlip;
    if (m.reaction < 0.f) add(KickoffTip::NoBoostAtGo, 100.f * e * 0.2f);
    else if (m.reaction > 0.05f) add(KickoffTip::SlowReaction, (100.f - g.sReact) * e * 0.2f + 0.2f);
    add(KickoffTip::BoostReleased, (100.f - g.sBoost) * e * 0.2f);
    add(KickoffTip::PathDeviation, (100.f - g.sPath) * e * 0.2f);
    // Missed small pad that the straight line would have collected (meaningless with unlimited boost, and not the
    // player's fault when a pad picked on a previous passive kickoff may still be respawning; the drill respawns
    // every pad when it places the car, so this never excuses a drill rep). Its loss is the time the
    // 12 boost is worth in time points: ~0.06 s when the tank ran dry before the ball (the pad would have kept
    // the boost on), ~0.04 s otherwise; never under the 2.5 point floor of the tip list, so it can be shown.
    if (m.padsPicked == 0 && !m.boostLooksUnlimited && !m.padsMaybeOnCooldown &&
        !kickoffPadsOnPath(kickoffPath(m.spawn, m.team)).empty()) {
        const float padTime = m.boostAtTouch < 1.f ? 0.06f : 0.04f;
        add(KickoffTip::MissedPad, std::max(kMissedPadMinLoss, padTime / span * 100.f * w.time));
    }

    const float total =
        w.time * g.sTime + w.speed * g.sSpeed + w.contact * g.sContact + w.outcome * g.sOutcome + w.exec * g.sExec;
    g.total = std::clamp(static_cast<int>(std::lround(total)), 0, 100);

    // Same tip from several criteria: sum the losses, then keep the 3 biggest (>= 2.5 points).
    std::vector<KickoffTipLoss> merged;
    for (const auto& c : cands) {
        auto it = std::find_if(merged.begin(), merged.end(), [&](const KickoffTipLoss& x) { return x.tip == c.tip; });
        if (it == merged.end()) merged.push_back(c);
        else it->loss += c.loss;
    }
    std::stable_sort(merged.begin(), merged.end(),
                     [](const KickoffTipLoss& a, const KickoffTipLoss& b) { return a.loss > b.loss; });
    for (const auto& c : merged) {
        if (g.tips.size() >= 3 || c.loss < kTipMinLoss) break;
        g.tips.push_back(c);
    }
    if (g.tips.empty() && g.total >= 85) g.tips.push_back({KickoffTip::GreatKickoff, 0.f});
    return g;
}

float kickoffTimeAtDistance(const std::vector<float>& along, float d) {
    if (along.empty()) return -1.f;
    if (along.front() >= d) return 0.f;
    for (size_t i = 1; i < along.size(); ++i)
        if (along[i] >= d) {
            const float a = (d - along[i - 1]) / std::max(1e-3f, along[i] - along[i - 1]);
            return (static_cast<float>(i - 1) + clamp01(a)) * kTickDt;
        }
    return -1.f;
}

std::vector<KickoffSplit> kickoffSplits(const KickoffMetrics& m, const KickoffGhost& ref) {
    std::vector<KickoffSplit> out;
    const float len = kickoffPath(m.spawn, m.team).length;
    for (float f : {0.25f, 0.5f, 0.75f, 1.0f}) {
        KickoffSplit s;
        s.fraction = f;
        const float d = f * len;
        s.playerTime =
            f >= 0.999f ? ((m.touched || m.estimated) ? m.timeToTouch : -1.f) : kickoffTimeAtDistance(m.along, d);
        s.refTime = f >= 0.999f ? ref.arrivalTime() : ref.timeAt(f * ref.path().length);
        s.reached = s.playerTime >= 0.f;
        s.delta = s.reached ? s.playerTime - s.refTime : 0.f;
        out.push_back(s);
    }
    return out;
}

std::string kickoffAttemptTitle(KickoffSpawn s) {
    return std::string("Kickoff (") + kickoffSpawnInfo(s).nameLower + ")";
}

std::string kickoffSkillId(KickoffSpawn s) { return std::string("kickoff.") + kickoffKindSkill(kickoffKind(s)); }

AttemptResult kickoffMakeResult(const KickoffMetrics& m, const KickoffGrade& g, const KickoffDuelResult* duel,
                                KickoffLevel target, float gameTime) {
    AttemptResult r;
    r.module = "kickoff";
    r.skill = kickoffSkillId(m.spawn);
    r.title = kickoffAttemptTitle(m.spawn);
    r.score = static_cast<float>(g.total);
    r.verdict = verdictFromScore(r.score);
    r.success = (m.touched || m.estimated) && g.total >= 70 && (!duel || duel->outcome != KickoffDuel::GhostClear);
    r.time = gameTime;
    const KickoffBenchmark b = kickoffBenchmark(m.spawn, target);
    auto metric = [&](const char* key, const char* label, float value, const char* unit, bool hasIdeal, float ideal) {
        Metric x;
        x.key = key;
        x.label = label;
        x.value = value;
        x.unit = unit;
        x.hasIdeal = hasIdeal;
        x.ideal = ideal;
        r.metrics.push_back(std::move(x));
    };
    if (m.touched || m.estimated) {
        metric("time", m.estimated ? "Temps jusqu'à la balle (estimé)" : "Temps jusqu'à la balle", m.timeToTouch, "s",
               true, b.arrive);
        metric("speed", "Vitesse vers la balle au contact", m.speedToBall, "uu/s", true, b.contactSpeed);
    } else {
        metric("progress", "Distance parcourue vers la balle", m.progress, "uu", false, 0.f);
    }
    if (m.touched) metric("offset", "Décalage du contact", std::fabs(m.lateralOffset), "uu", true, 0.f);
    if (m.reaction >= 0.f) metric("reaction", "Réaction au GO", m.reaction * 1000.f, "ms", true, 0.f);
    if (m.firstJump >= 0.f) metric("first_jump", "Premier saut", m.firstJump, "s", true, 0.45f);
    if (m.dodge >= 0.f) metric("dodge_angle", "Angle du flip", std::fabs(m.dodgeAngleDeg), "°", true, 30.f);
    if (m.cancel >= 0.f) metric("cancel", "Annulation du flip", (m.cancel - m.dodge) * 1000.f, "ms", true, 50.f);
    metric("boost_used", "Boost utilisé", m.boostUsed, "", false, 0.f);
    metric("boost_left", "Boost restant", m.boostAtTouch, "", false, 0.f);
    metric("boost_held", "Boost maintenu", m.boostHeldRatio * 100.f, "%", true, 100.f);
    metric("path_dev", "Écart à la ligne droite", m.pathDevRms, "uu", true, 0.f);
    if (m.hasBallExit)
        metric("exit_angle", "Direction de la balle (0 = vers le but adverse)", m.ballExitAngleDeg, "°", true, 0.f);
    if (duel) metric("duel_dt", "Avance sur l'adversaire", duel->dt > -50.f ? duel->dt : 0.f, "s", false, 0.f);
    for (const auto& t : g.tips) {
        Tip tip;
        tip.code = kickoffTipCode(t.tip);
        tip.text = kickoffTipText(t.tip);
        tip.weight = t.loss;
        r.tips.push_back(std::move(tip));
    }
    // Unlimited boost makes the time unrealistic: graded, but never stored as a record.
    if (m.touched && !m.boostLooksUnlimited) {
        r.valueForRecord = m.timeToTouch;
        r.lowerIsBetter = true;
        r.hasRecordValue = true;
    }
    return r;
}

} // namespace rlt
