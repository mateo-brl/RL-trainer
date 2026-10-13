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
    case KickoffTip::NoTouch: return "Tu n'as pas touché la balle : fonce au centre dès le GO, boost enfoncé.";
    case KickoffTip::LateArrival:
        return "Tu arrives trop tard sur la balle : garde le boost du GO jusqu'à la panne et enchaîne un speedflip "
               "tôt.";
    case KickoffTip::NoBoostAtGo:
        return "Appuie sur boost ET accélérateur pendant le décompte : tout est pris en compte à l'instant du GO.";
    case KickoffTip::SlowReaction: return "Réaction lente au GO : pré-appuie boost + accélérateur avant la fin du 1.";
    case KickoffTip::BoostReleased:
        return "Tu relâches le boost pendant l'approche : avec un speedflip, garde-le enfoncé tout du long.";
    case KickoffTip::EarlyFirstJump:
        return "Premier saut trop tôt : attends ~0,45 s (vers 1000-1100 uu/s) avant de sauter.";
    case KickoffTip::LateFirstJump:
        return "Premier saut trop tard : saute vers 0,45-0,55 s après le GO pour profiter du boost en l'air.";
    case KickoffTip::SlowDodge:
        return "Le flip arrive trop longtemps après le saut : enchaîne le 2e appui en moins de 0,15 s.";
    case KickoffTip::NoCancel:
        return "Flip non annulé : tire le stick vers l'arrière juste après le flip pour garder le nez droit.";
    case KickoffTip::LateCancel:
        return "Annulation trop tardive : tire le stick en arrière en moins de 0,1 s après le flip.";
    case KickoffTip::DodgeTooStraight:
        return "Flip trop droit : vise un angle d'environ 30° (entre 20° et 45°) en diagonale avant.";
    case KickoffTip::DodgeTooWide:
        return "Flip trop latéral : rapproche le stick de l'avant (angle 20-45°) pour ne pas perdre de vitesse.";
    case KickoffTip::LateLanding:
        return "Atterrissage lent : utilise l'air roll pendant l'annulation pour reposer les roues plus vite.";
    case KickoffTip::PathDeviation:
        return "Ta trajectoire dévie de la ligne vers la balle : corrige l'angle avant de sauter, pas pendant le flip.";
    case KickoffTip::OffCenterContact:
        return "Contact décalé : frappe le centre de la balle (côté de ton propre but) pour gagner le 50/50.";
    case KickoffTip::LowContactSpeed:
        return "Vitesse au contact trop faible : boost plus longtemps ou flippe dans la balle au dernier moment.";
    case KickoffTip::BallToOwnHalf:
        return "La balle repart vers ton camp : arrive plus vite ou dévie légèrement pour frapper le centre côté but.";
    case KickoffTip::FlipIntoBall:
        return "Pense à flipper dans la balle ~0,25 s avant le contact pour ajouter de la puissance au 50/50.";
    case KickoffTip::MissedPad:
        return "Tu as raté le petit boost sur ta route : passe dessus, c'est +12 de boost gratuit.";
    case KickoffTip::TrySpeedflip:
        return "Ton approche (front flip / sans flip) coûte ~0,1-0,2 s : passe au speedflip (flip diagonal annulé, "
               "boost maintenu).";
    case KickoffTip::GreatKickoff: return "Excellent kickoff ! Garde ce timing et varie les positions.";
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
    const float e = w.exec;
    if (g.speedflipAttempt) {
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
    // player's fault when a pad picked on the previous rep may still be respawning). Its loss is the time the
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
