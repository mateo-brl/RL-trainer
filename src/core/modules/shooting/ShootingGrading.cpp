#include "core/modules/shooting/ShootingGrading.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/common/Controls.h"
#include "core/common/Text.h"
#include "core/common/Units.h"

namespace rlt::shoot {

namespace {

Verdict verdictFor(float score, bool success) {
    Verdict v = verdictFromScore(score);
    if (success && v < Verdict::Ok) v = Verdict::Ok;
    if (!success && v > Verdict::Ok) v = Verdict::Ok;
    return v;
}

void addTip(std::vector<Tip>& tips, const char* code, const std::string& text, float weight) {
    for (const auto& t : tips)
        if (t.code == code) return;
    tips.push_back(Tip{code, text, weight});
}

void sortTips(std::vector<Tip>& tips) {
    std::stable_sort(tips.begin(), tips.end(), [](const Tip& a, const Tip& b) { return a.weight > b.weight; });
}

Metric metric(const char* key, const char* label, float value, const char* unit) {
    Metric m;
    m.key = key;
    m.label = label;
    m.value = value;
    m.unit = unit;
    return m;
}

Metric metricIdeal(const char* key, const char* label, float value, const char* unit, float ideal) {
    Metric m = metric(key, label, value, unit);
    m.ideal = ideal;
    m.hasIdeal = true;
    return m;
}

// " (A)" when the action is bound, "" otherwise: appended after a verb ("Saute (A), puis ...").
std::string keySuffix(const PlayerProfile& profile, GameAction a, KeyStyle style) {
    const std::string k = actionKeyLabel(profile, a, style);
    return k.empty() ? std::string() : " (" + k + ")";
}

bool needsRotation(FlickType t) {
    return t == FlickType::Flick45 || t == FlickType::Flick90 || t == FlickType::Musty || t == FlickType::Breezi ||
           t == FlickType::Reverse || t == FlickType::Delayed;
}

// Correction when the requested flick was not the one performed.
void wrongTypeTip(std::vector<Tip>& tips, const FlickReport& r, FlickType want, float delayedSec) {
    const float ay = std::fabs(r.f.yawTurnDeg);
    switch (want) {
    case FlickType::Musty:
        addTip(tips, "shooting.flick_musty_nose",
               "Bascule d'abord le nez vers le bas (stick avant), puis fais le backflip.", 8.f);
        break;
    case FlickType::Breezi:
        addTip(tips, "shooting.flick_breezi_spin",
               "Fais une vraie tornade (air roll + stick opposé) avant le backflip.", 8.f);
        break;
    case FlickType::Flick45:
        if (std::fabs(r.f.dodgeSide) < 0.35f)
            addTip(tips, "shooting.flick_diagonal_dodge",
                   "Fais un dodge en diagonale (stick avant + côté) vers la cible.", 8.f);
        else if (ay < 25.f)
            addTip(tips, "shooting.flick_under_rotated", "Tourne la voiture d'environ 45° avant le dodge diagonal.",
                   8.f);
        else
            addTip(tips, "shooting.flick_over_rotated", "Tourne moins : au-delà de 65° ton 45 devient un 90.", 8.f);
        break;
    case FlickType::Flick90:
        if (ay > 115.f)
            addTip(tips, "shooting.flick_over_rotated_90", "Tourne moins : vise un quart de tour (90°) avant le dodge.",
                   8.f);
        else
            addTip(tips, "shooting.flick_90_rotation",
                   "Tourne d'un quart de tour (environ 90°) avant le dodge latéral.", 8.f);
        break;
    case FlickType::Delayed:
        addTip(tips, "shooting.flick_not_delayed",
               "Garde la balle plus longtemps sur le toit en l'air avant le dodge (" + frenchDecimal(delayedSec) +
                   " s ou plus).",
               8.f);
        break;
    case FlickType::Backflip:
        addTip(tips, "shooting.flick_back_dodge", "Fais un dodge vers l'arrière (stick tiré à fond).", 8.f);
        break;
    case FlickType::Reverse:
        addTip(tips, "shooting.flick_reverse_roll",
               "Roule la voiture sur le côté avec l'air roll avant le dodge arrière-diagonal.", 8.f);
        break;
    case FlickType::Front:
    default:
        addTip(tips, "shooting.flick_front_dodge", "Fais un dodge droit vers l'avant (stick tout droit).", 8.f);
        break;
    }
}

} // namespace

float flickMinPower(float level) { return lerpf(1300.f, 2400.f, clamp01(level)); }

std::string frenchDecimal(float value) {
    std::string s = formatNumber(value, 2);
    const size_t dot = s.find('.');
    if (dot == std::string::npos) return s;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") return "0";
    const size_t d = s.find('.');
    if (d != std::string::npos) s[d] = ',';
    return s;
}

std::string airRollPrompt(const PlayerProfile& profile, KeyStyle style) {
    for (GameAction a : {GameAction::AirRoll, GameAction::AirRollLeft, GameAction::AirRollRight})
        if (!actionKeyLabel(profile, a, style).empty()) return actionPrompt(profile, a, style);
    return actionPrompt(profile, GameAction::AirRoll, style);
}

std::string flickCue(FlickType t, const PlayerProfile& profile, KeyStyle style, float delayedSec) {
    const std::string jump = actionPrompt(profile, GameAction::Jump, style);
    switch (t) {
    case FlickType::Front: return jump + ", puis dodge avant";
    case FlickType::Flick45: return jump + ", tourne d'environ 45°, dodge diagonal";
    case FlickType::Flick90: return jump + ", tourne d'environ 90°, dodge latéral";
    case FlickType::Backflip: return jump + ", puis dodge arrière";
    case FlickType::Delayed: return jump + ", garde la balle " + frenchDecimal(delayedSec) + " s, puis dodge";
    case FlickType::Musty: return jump + ", nez en bas, puis backflip";
    case FlickType::Breezi: return jump + " tenu, tornade (" + airRollPrompt(profile, style) + "), puis backflip";
    case FlickType::Reverse: return jump + ", roule sur le côté, dodge arrière-diagonal";
    case FlickType::Diagonal: return jump + ", puis dodge en diagonale";
    case FlickType::Side: return jump + ", puis dodge sur le côté";
    default: return jump + ", puis dodge";
    }
}

std::string flickHowTo(FlickType t, const PlayerProfile& profile, KeyStyle style, float delayedSec) {
    const std::string j = keySuffix(profile, GameAction::Jump, style);
    const std::string ar = airRollPrompt(profile, style);
    switch (t) {
    case FlickType::Front:
        return "Balle posée au centre du toit : saute" + j +
               ", puis rappuie sur saut avec le stick vers l'avant. Le dodge avant projette la balle devant toi.";
    case FlickType::Flick45:
        return "Saute" + j + ", tourne la voiture d'environ 45° (stick sur le côté, " + ar +
               " aide), puis dodge en diagonale vers la cible. Au-delà de 65° ça devient un 90.";
    case FlickType::Flick90:
        return "Saute" + j +
               ", tourne d'un quart de tour (environ 90°), puis dodge sur le côté vers la cible. Très puissant.";
    case FlickType::Backflip:
        return "Saute" + j +
               " puis dodge vers l'arrière (stick tiré à fond + saut). La balle part haute : idéal pour lober.";
    case FlickType::Delayed:
        return "Saute" + j + " avec la balle sur le toit, garde-la en équilibre en l'air au moins " +
               frenchDecimal(delayedSec) + " s, puis flick au dernier moment pour surprendre le défenseur.";
    case FlickType::Musty:
        return "Saute" + j +
               ", pousse le stick vers l'avant pour baisser le nez, puis backflip (stick tiré + saut). "
               "Le nez remonte et frappe la balle vers l'avant.";
    case FlickType::Breezi:
        return "Saute en gardant saut appuyé" + j + ", fais une tornade avec " + ar +
               " et le stick opposé, arrête la rotation nez vers le haut puis finis par un backflip. Pas en "
               "supersonique.";
    case FlickType::Reverse:
        return "Saute" + j + ", roule la voiture sur le côté avec " + ar +
               ", puis dodge en arrière-diagonale : la balle part tendue vers l'avant.";
    case FlickType::Diagonal:
        return "Saute" + j + " puis dodge en diagonale (stick avant + côté) sans tourner la voiture.";
    case FlickType::Side: return "Saute" + j + " puis dodge sur le côté (stick à gauche ou à droite).";
    default: return "Saute" + j + " puis dodge dans une direction franche (stick à fond).";
    }
}

// ---- Flicks -----------------------------------------------------------------------------------------------
AttemptResult gradeFlick(const FlickReport& r, const AimResult& aim, const FlickGradeParams& p,
                         const PlayerProfile& profile) {
    (void)profile;
    AttemptResult a;
    a.module = "shooting";
    a.time = p.time;
    const FlickType cat = r.category;
    const bool requested = p.requested >= 0 && p.requested < kRequestableFlickCount;
    const FlickType want = requested ? static_cast<FlickType>(p.requested) : cat;
    const bool matches = !requested || flickMatches(r.cls, p.requested);
    a.skill = std::string("shoot.flick.") + flickTypeId(cat);
    a.title = flickTypeLabel(cat);
    if (cat == FlickType::Delayed && r.cls.base != FlickType::Other)
        a.title += std::string(" (") + flickTypeLabel(r.cls.base) + ")";
    if (r.cls.uncertain) a.title += " (?)";
    if (requested && !matches) a.title += std::string(", demandé : ") + flickTypeLabel(want);

    const float kmh = r.power * kUusToKph;
    const float powerQ = r.contact ? rampScore(r.power, 2600.f, 800.f) : 0.f;
    float accQ = aim.reached ? aim.score01 : 0.4f * clamp01(1.f - p.aimErrorDeg / 30.f);
    if (!r.contact) accQ = 0.f;
    const DelayBand band = flickDelayBand(want);
    const float timingQ = bandScore(r.f.delay, band.lo, band.hi, 0.3f);
    const float roofQ =
        (bandScore(std::fabs(r.roofOffset.y), 0.f, 25.f, 40.f) + bandScore(r.roofOffset.x, -10.f, 45.f, 50.f) +
         rampScore(r.relSpeedAtJump, 50.f, 300.f) + rampScore(r.carryTime, 0.3f, 0.05f)) /
        4.f;
    float rotQ = 1.f;
    switch (r.cls.base) {
    case FlickType::Flick45: rotQ = bandScore(std::fabs(r.f.yawTurnDeg), 35.f, 55.f, 20.f); break;
    case FlickType::Flick90: rotQ = bandScore(std::fabs(r.f.yawTurnDeg), 80.f, 100.f, 20.f); break;
    case FlickType::Musty: rotQ = bandScore(r.f.pitchDeg, -120.f, -50.f, 25.f); break;
    case FlickType::Breezi: rotQ = bandScore(r.f.rollDeg, 270.f, 450.f, 120.f); break;
    default: break;
    }
    if (requested && !matches) rotQ *= 0.3f;
    float total = 0.35f * powerQ + 0.25f * accQ + 0.15f * timingQ + 0.10f * roofQ + 0.15f * rotQ;
    if (!r.contact) total = std::min(total, 0.15f);
    if (requested && !matches) total = std::min(total, 0.55f);
    a.score = std::round(100.f * clamp01(total));
    const bool powerOk = r.power >= p.minPower;
    const bool targetOk = !p.requireOnTarget || aim.onGoal;
    a.success = r.contact && matches && powerOk && targetOk;
    a.verdict = verdictFor(a.score, a.success);

    a.metrics.push_back(metricIdeal("power", "Puissance", kmh, "km/h", p.minPower * kUusToKph));
    a.metrics.push_back(metric("accuracy", "Précision", std::round(accQ * 100.f), "%"));
    a.metrics.push_back(metricIdeal("delay", "Délai saut / flick", r.f.delay, "s", 0.5f * (band.lo + band.hi)));
    a.metrics.push_back(metric("roof_time", "Temps sur le toit", r.roofTime, "s"));
    a.metrics.push_back(metric("elevation", "Angle de montée", r.elevationDeg, "°"));
    a.metrics.push_back(metric("yaw", "Rotation avant le flick", std::fabs(r.f.yawTurnDeg), "°"));
    a.metrics.push_back(metric("pitch", "Inclinaison au dodge", r.f.pitchDeg, "°"));
    a.metrics.push_back(metric("roof_offset", "Décalage sur le toit", length2D(r.roofOffset), "uu"));
    if (aim.reached) {
        a.metrics.push_back(metric("target_dist", "Écart à la cible", aim.centreDist, "uu"));
        a.metrics.push_back(metric("height", "Hauteur au but", aim.hit.y, "uu"));
    }
    a.metrics.push_back(metric("gain", "Vitesse ajoutée", r.gain * kUusToKph, "km/h"));

    std::vector<Tip>& tips = a.tips;
    if (!r.contact) {
        if (r.lostBall)
            addTip(tips, "shooting.flick_lost_ball", "Garde la balle au centre du toit avant de flick.", 10.f);
        else
            addTip(tips, "shooting.flick_no_contact", "Dodge pendant que la balle touche encore le toit.", 9.f);
    }
    if (requested && !matches) wrongTypeTip(tips, r, want, p.delayedSec);
    const bool late = r.f.delay > band.hi + 0.1f;
    const bool early = r.f.delay < band.lo - 0.05f;
    if (r.contact && !powerOk) {
        const float w = 4.f + 8.f * clamp01(1.f - r.power / std::max(1.f, p.minPower));
        if (late)
            addTip(tips, "shooting.flick_late", "Lance le dodge plus tôt pour plus de puissance.", w);
        else if (std::fabs(r.roofOffset.y) > 30.f)
            addTip(tips, "shooting.flick_roof_side", "Garde la balle au centre du toit avant de flick.", w);
        else if (r.roofOffset.x < -15.f)
            addTip(tips, "shooting.flick_roof_back",
                   "Avance la balle vers l'avant du toit avant de sauter : le flick sera plus puissant.", w);
        else
            addTip(tips, "shooting.flick_weak",
                   "Pousse le stick à fond dans la direction du flip pour plus de puissance.", w);
    }
    if (r.contact) {
        if (early && needsRotation(want))
            addTip(tips, "shooting.flick_early",
                   "Attends un peu après le saut : laisse la voiture tourner avant le dodge.", 3.f);
        else if (late && want != FlickType::Delayed)
            addTip(tips, "shooting.flick_late", "Lance le dodge plus tôt pour plus de puissance.", 3.f);
        const bool tooHigh =
            (aim.reached && !aim.onGoal && aim.hit.y > kOnTargetTopZ) || (!aim.reached && r.elevationDeg > 55.f);
        if (tooHigh)
            addTip(tips, "shooting.flick_too_high",
                   "Balle trop haute : flick plus tôt ou plus vers l'avant pour un tir plus tendu.", 5.f);
        else if (!aim.onGoal)
            addTip(tips, "shooting.flick_off_target",
                   "Oriente la voiture vers la cible avant de sauter : l'angle de départ décide de la direction.", 6.f);
        else if (!aim.inZone)
            addTip(tips, "shooting.flick_zone",
                   "Cadré ! Pour viser la zone, oriente la voiture vers elle avant de sauter.", 2.f);
    }
    if (r.carryTime < 0.2f || r.relSpeedAtJump > 150.f)
        addTip(tips, "shooting.flick_unsettled",
               "Stabilise la balle sur le toit (même vitesse que la voiture) avant de sauter.", 3.f);
    if (std::fabs(r.roofOffset.y) > 30.f)
        addTip(tips, "shooting.flick_roof_side", "Garde la balle au centre du toit avant de flick.", 2.f);
    if (r.supersonicStart && (want == FlickType::Musty || want == FlickType::Breezi))
        addTip(tips, "shooting.flick_supersonic",
               "Pars moins vite : en supersonique la balle ne tient pas sur la voiture.", 4.f);
    sortTips(tips);

    a.valueForRecord = kmh;
    a.hasRecordValue = r.contact;
    a.lowerIsBetter = false;
    return a;
}

AttemptResult gradeLostBall(int requested, float time) {
    AttemptResult a;
    a.module = "shooting";
    a.time = time;
    const bool req = requested >= 0 && requested < kRequestableFlickCount;
    a.skill = std::string("shoot.flick.") + (req ? flickTypeId(static_cast<FlickType>(requested)) : "any");
    a.title = "Balle perdue avant le flick";
    a.score = 0.f;
    a.success = false;
    a.verdict = Verdict::Fail;
    addTip(a.tips, "shooting.flick_lost_ball", "Garde la balle au centre du toit avant de flick.", 10.f);
    addTip(a.tips, "shooting.flick_unsettled", "Roule à la même vitesse que la balle, sans coup de volant brusque.",
           4.f);
    return a;
}

// ---- Targets ----------------------------------------------------------------------------------------------
AttemptResult gradeTarget(const AimResult& aim, float shotSpeed, const TargetGradeParams& p) {
    AttemptResult a;
    a.module = "shooting";
    a.time = p.time;
    a.skill = std::string("shoot.target.") + zoneId(p.zone);
    std::string title = std::string("Cible ") + zoneLabel(p.zone);
    if (p.flick) title += std::string(" (") + flickTypeLabel(p.flick->category) + ")";
    a.title = title;
    a.score = std::round(100.f * clamp01(aim.score01));
    const bool slow = p.minSpeed > 0.f && shotSpeed < p.minSpeed;
    a.success = aim.inZone && !slow;
    a.verdict = verdictFor(a.score, a.success);

    a.metrics.push_back(metric("accuracy", "Précision", a.score, "%"));
    if (aim.reached) {
        a.metrics.push_back(metric("target_dist", "Écart au centre de la cible", aim.centreDist, "uu"));
        a.metrics.push_back(metric("offset_h", "Écart horizontal (+ = à droite)", aim.du, "uu"));
        a.metrics.push_back(metric("offset_v", "Écart vertical (+ = plus haut)", aim.dz, "uu"));
    }
    a.metrics.push_back(
        p.minSpeed > 0.f ? metricIdeal("speed", "Vitesse du tir", shotSpeed * kUusToKph, "km/h", p.minSpeed * kUusToKph)
                         : metric("speed", "Vitesse du tir", shotSpeed * kUusToKph, "km/h"));
    a.metrics.push_back(metric("in_zone", "Dans la cible", aim.inZone ? 1.f : 0.f, ""));
    a.metrics.push_back(metric("on_goal", "Cadré", aim.onGoal ? 1.f : 0.f, ""));

    auto& tips = a.tips;
    if (!aim.reached) {
        addTip(tips, "shooting.target_short",
               "Ta balle n'atteint pas le but : frappe plus fort et plus droit vers la cible.", 8.f);
    } else if (!aim.inZone) {
        const float outU = std::fabs(aim.du) - p.rect.hw;
        const float outZ = std::fabs(aim.dz) - p.rect.hh;
        if (outU > 0.f) {
            const float w = std::min(10.f, 2.f + outU / 60.f);
            if (aim.du > 0.f)
                addTip(tips, "shooting.target_right",
                       "Tir trop à droite : vise plus à gauche (touche la balle sur son côté droit).", w);
            else
                addTip(tips, "shooting.target_left",
                       "Tir trop à gauche : vise plus à droite (touche la balle sur son côté gauche).", w);
        }
        if (outZ > 0.f) {
            const float w = std::min(10.f, 2.f + outZ / 60.f);
            if (aim.dz > 0.f)
                addTip(tips, "shooting.target_high",
                       "Tir trop haut : frappe la balle plus au centre, moins par dessous.", w);
            else
                addTip(tips, "shooting.target_low",
                       "Tir trop bas : frappe la balle un peu plus par dessous pour la lever.", w);
        }
    } else if (p.rect.normDistance(aim.hit) > 0.7f) {
        addTip(tips, "shooting.target_edge", "Dans la cible, mais au bord : vise le centre de la zone.", 1.f);
    }
    if (slow) {
        const std::string text =
            "Frappe plus fort : vise au moins " + formatNumber(p.minSpeed * kUusToKph, 0) + " km/h.";
        addTip(tips, "shooting.target_slow", text, 5.f);
    }
    sortTips(tips);
    a.valueForRecord = a.score;
    a.hasRecordValue = true;
    return a;
}

// ---- Power ------------------------------------------------------------------------------------------------
float powerScore01(float kmh) { return rampScore(kmh, 130.f, 40.f); }

AttemptResult gradePower(const PowerShot& shot, float goalUus, float time) {
    AttemptResult a;
    a.module = "shooting";
    a.time = time;
    a.skill = "shoot.power";
    a.title = "Frappe puissante";
    const float kmh = shot.speed * kUusToKph;
    a.score = std::round(100.f * powerScore01(kmh));
    a.success = shot.speed >= goalUus;
    a.verdict = verdictFor(a.score, a.success);
    a.metrics.push_back(metricIdeal("speed", "Vitesse du tir", kmh, "km/h", goalUus * kUusToKph));
    a.metrics.push_back(metric("car_speed", "Vitesse de la voiture", shot.carSpeed * kUusToKph, "km/h"));
    a.metrics.push_back(metric("flip", "Flip dans la balle", shot.flip ? 1.f : 0.f, ""));
    if (shot.flip && shot.dodgeToTouch >= 0.f)
        a.metrics.push_back(metric("dodge_to_touch", "Flip avant l'impact", shot.dodgeToTouch, "s"));
    a.metrics.push_back(metric("on_goal", "Cadré", shot.onGoal ? 1.f : 0.f, ""));

    auto& tips = a.tips;
    if (!a.success) {
        if (!shot.flip)
            addTip(tips, "shooting.power_no_flip", "Ajoute un flip juste avant l'impact pour gagner de la vitesse.",
                   6.f);
        else if (shot.dodgeToTouch > 0.25f)
            addTip(tips, "shooting.power_flip_early",
                   "Flippe plus tard : le nez doit toucher la balle pendant la rotation.", 5.f);
        if (shot.carSpeed < 1400.f)
            addTip(tips, "shooting.power_slow_car", "Arrive plus vite : boost jusqu'à la balle.", 5.f);
        if (shot.sideHit)
            addTip(tips, "shooting.power_side_hit", "Frappe avec l'avant de la voiture, pas avec le côté.", 4.f);
        if (tips.empty())
            addTip(tips, "shooting.power_weak", "Frappe le centre de la balle avec le nez, à pleine vitesse.", 3.f);
    }
    if (!shot.onGoal)
        addTip(tips, "shooting.power_off_target", "Cadre aussi ton tir : aligne voiture, balle et but.", 2.f);
    sortTips(tips);
    a.valueForRecord = kmh;
    a.hasRecordValue = true;
    return a;
}

} // namespace rlt::shoot
