#include "core/modules/mechanics/Presentation.h"

#include <cstring>
#include <iterator>

#include "core/common/Text.h"
#include "core/modules/mechanics/Tips.h"

namespace rlt::mech {

namespace {

constexpr float kBig = 1e9f;
constexpr float kMsTick = 1000.f / 120.f;  // ms per tick
constexpr float kPerTickS = 1.f / 120.f;
using B = MetricSpec::Band;

// {key, label, unit, scale, band, lo, hi, tol, ideal, idealKey, decimals, absolute, validMin, validMax}
const std::vector<MetricSpec> kSpeedFlip = {
    {"absAngleDeg", "Angle du flip", "°", 1.f, B::AroundIdeal, -8.f, 8.f, 17.f, 30.f, "targetAngleDeg", 0, false, 0.f, 180.f},
    {"cancelTicks", "Annulation", "ms", kMsTick, B::Fixed, -kBig, 50.f, 67.f, 40.f, nullptr, 0, false, 0.f, 1e6f},
    {"jumpToDodgeTicks", "Saut -> flip", "ms", kMsTick, B::Fixed, -kBig, 108.f, 142.f, 100.f, nullptr, 0, false, 0.f, 1e6f},
    {"boostRatio", "Boost tenu", "%", 100.f, B::Fixed, 90.f, kBig, 40.f, 100.f, nullptr, 0, false, 0.f, 2.f},
    {"timeToBall", "Temps jusqu'à la balle", "s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 2, false, 0.f, 1e6f},
    {"headingErrorDeg", "Cap à la réception", "°", 1.f, B::Fixed, -kBig, 5.f, 20.f, 0.f, nullptr, 0, false, 0.f, 179.f},
};
const std::vector<MetricSpec> kHalfFlip = {
    {"cancelTicks", "Annulation après le flip", "ms", kMsTick, B::Fixed, 117.f, 250.f, 150.f, 200.f, nullptr, 0, false, 0.f, 1e6f},
    {"headingChangeDeg", "Demi-tour", "°", 1.f, B::Fixed, 165.f, 195.f, 35.f, 180.f, nullptr, 0, false, 0.f, 360.f},
    {"rollDelayTicks", "Air roll après l'annulation", "ms", kMsTick, B::Fixed, -kBig, 375.f, 250.f, 200.f, nullptr, 0, false, 0.f, 900.f},
    {"landingTicks", "Durée du demi-tour", "ms", kMsTick, B::Fixed, -kBig, 917.f, 500.f, 900.f, nullptr, 0, false, 0.f, 1e6f},
    {"exitSpeed", "Vitesse en sortie", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, -1e6f, 1e6f},
};
const std::vector<MetricSpec> kFlipCancel = {
    {"cancelTicks", "Annulation", "ms", kMsTick, B::Fixed, -kBig, 50.f, 117.f, 40.f, nullptr, 0, false, 0.f, 1e6f},
    {"cancelStrength", "Stick opposé", "%", 100.f, B::Fixed, 95.f, kBig, 45.f, 100.f, nullptr, 0, false, 0.f, 2.f},
};
const std::vector<MetricSpec> kWavedash = {
    {"gainRatio", "Efficacité du dash", "%", 100.f, B::Fixed, 80.f, kBig, 50.f, 100.f, nullptr, 0, false, -10.f, 10.f},
    {"gainAlongDash", "Gain de vitesse", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, "expectedGain", 0, false, -1e6f, 1e6f},
    {"firstContactTicks", "Flip -> roues au sol", "ms", kMsTick, B::Fixed, -kBig, 67.f, 67.f, 25.f, nullptr, 0, false, 0.f, 1e6f},
    {"pitchAtContactDeg", "Nez au contact", "°", 1.f, B::Fixed, -3.f, 25.f, 17.f, 12.f, nullptr, 0, false, -90.f, 90.f},
    {"stickAtPress", "Stick au saut", "", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, "deadzone", 2, false, 0.f, 10.f},
    {"ticksAfterLanding", "Retard (roues déjà au sol)", "ms", kMsTick, B::Fixed, -kBig, 0.f, 50.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kWallDash = {
    {"jumpToDodgeTicks", "Entre les 2 sauts", "ms", kMsTick, B::Fixed, -kBig, 83.f, 50.f, 70.f, nullptr, 0, false, 0.f, 1e6f},
    {"firstJumpHoldTicks", "1er saut tenu", "ms", kMsTick, B::Fixed, -kBig, 83.f, 50.f, 40.f, nullptr, 0, false, 0.f, 1e6f},
    {"landingTicks", "Retour sur le mur", "ms", kMsTick, B::Fixed, -kBig, 83.f, 167.f, 50.f, nullptr, 0, false, 0.f, 1e6f},
    {"gainAlongDash", "Gain de vitesse", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, "expectedGain", 0, false, -1e6f, 1e6f},
};
const std::vector<MetricSpec> kChain = {
    {"dashes", "Dashes enchaînés", "", 1.f, B::Fixed, 3.f, kBig, 2.f, 4.f, nullptr, 0, false, 0.f, 1e6f},
    {"avgGapTicks", "Temps au sol entre dashes", "ms", kMsTick, B::Fixed, -kBig, 50.f, 150.f, 33.f, nullptr, 0, false, 0.f, 1e6f},
    {"maxSpeed", "Vitesse max", "uu/s", 1.f, B::Fixed, 2200.f, kBig, 800.f, 2300.f, nullptr, 0, false, 0.f, 1e6f},
    {"boostUsed", "Boost utilisé", "", 1.f, B::Fixed, -kBig, 0.f, 10.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
    {"timeToSupersonic", "Temps jusqu'au supersonique", "s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 2, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kWallChain = {
    {"dashes", "Wall dashes enchaînés", "", 1.f, B::Fixed, 3.f, kBig, 2.f, 4.f, nullptr, 0, false, 0.f, 1e6f},
    {"avgGapTicks", "Temps sur le mur entre dashes", "ms", kMsTick, B::Fixed, -kBig, 83.f, 150.f, 50.f, nullptr, 0, false, 0.f, 1e6f},
    {"avgGain", "Gain moyen", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, -1e6f, 1e6f},
    {"maxSpeed", "Vitesse max", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kCombo = {
    {"firstScore", "Score 1re méca", "/100", 1.f, B::Fixed, 70.f, kBig, 40.f, 100.f, nullptr, 0, false, 0.f, 101.f},
    {"secondScore", "Score 2e méca", "/100", 1.f, B::Fixed, 70.f, kBig, 40.f, 100.f, nullptr, 0, false, 0.f, 101.f},
    {"gapTicks", "Enchaînement", "ms", kMsTick, B::Fixed, -kBig, 83.f, 290.f, 50.f, nullptr, 0, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kFastAerial = {
    {"holdTicks", "1er saut tenu", "ms", kMsTick, B::Fixed, 192.f, 225.f, 100.f, 200.f, nullptr, 0, false, 0.f, 1e6f},
    {"gapTicks", "Relâche -> double saut", "ms", kMsTick, B::Fixed, -kBig, 75.f, 92.f, 40.f, nullptr, 0, false, 0.f, 1e6f},
    {"boostStartTicks", "Début du boost", "ms", kMsTick, B::Fixed, -kBig, 50.f, 33.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
    {"tiltInput", "Stick tiré", "%", 100.f, B::Fixed, 70.f, kBig, 50.f, 100.f, nullptr, 0, false, 0.f, 2.f},
    {"timeToBall", "Temps jusqu'à la balle", "s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 2, false, 0.f, 1e6f},
    {"vzEnd", "Vitesse verticale", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, -1e6f, 1e6f},
};
const std::vector<MetricSpec> kDoubleJumpAerial = {
    {"holdTicks", "1er saut tenu", "ms", kMsTick, B::Fixed, 167.f, 225.f, 100.f, 200.f, nullptr, 0, false, 0.f, 1e6f},
    {"gapTicks", "Relâche -> double saut", "ms", kMsTick, B::Fixed, -kBig, 75.f, 125.f, 40.f, nullptr, 0, false, 0.f, 1e6f},
    {"boostAfterDJTicks", "Boost après le double saut", "ms", kMsTick, B::Fixed, -kBig, 50.f, 200.f, 25.f, nullptr, 0, false, 0.f, 1e6f},
    {"vzEnd", "Vitesse verticale", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, -1e6f, 1e6f},
};
const std::vector<MetricSpec> kStall = {
    {"yawPlusRoll", "Yaw + air roll", "", 1.f, B::Fixed, -kBig, 0.1f, 0.4f, 0.f, nullptr, 2, true, -10.f, 10.f},
    {"pitchAbs", "Stick haut / bas", "", 1.f, B::Fixed, -kBig, 0.1f, 0.4f, 0.f, nullptr, 2, false, 0.f, 10.f},
    {"hoverTicks", "Flottement", "ms", kMsTick, B::Fixed, 417.f, kBig, 250.f, 500.f, nullptr, 0, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kAirSpin = {
    {"durationTicks", "Durée", "s", kPerTickS, B::Fixed, 1.f, kBig, 0.5f, 1.5f, nullptr, 1, false, 0.f, 1e6f},
    {"axisStability", "Stabilité de l'axe", "%", 100.f, B::Fixed, 80.f, kBig, 40.f, 100.f, nullptr, 0, false, 0.f, 2.f},
    {"avgSpinRate", "Vitesse de rotation", "rad/s", 1.f, B::Fixed, 3.f, kBig, 2.f, 5.f, nullptr, 1, false, 0.f, 100.f},
    {"turns", "Tours", "", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 1, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kRecovery = {
    {"tiltAtTouchDeg", "Inclinaison au contact", "°", 1.f, B::Fixed, -kBig, 10.f, 40.f, 0.f, nullptr, 0, false, 0.f, 180.f},
    {"velocityAlignDeg", "Nez / direction", "°", 1.f, B::Fixed, -kBig, 15.f, 45.f, 0.f, nullptr, 0, false, 0.f, 180.f},
    {"speedRetained", "Vitesse conservée", "%", 100.f, B::Fixed, 95.f, kBig, 35.f, 100.f, nullptr, 0, false, 0.f, 10.f},
    {"settleTicks", "Stabilisation", "ms", kMsTick, B::Fixed, -kBig, 125.f, 375.f, 50.f, nullptr, 0, false, 0.f, 1e6f},
    {"dashGain", "Gain du wavedash", "uu/s", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, -1e6f, 1e6f},
};
const std::vector<MetricSpec> kPowerslide = {
    {"speedRetained", "Vitesse conservée", "%", 100.f, B::Fixed, 85.f, kBig, 35.f, 100.f, nullptr, 0, false, 0.f, 10.f},
    {"exitSlipDeg", "Glisse en sortie", "°", 1.f, B::Fixed, -kBig, 8.f, 27.f, 0.f, nullptr, 0, false, 0.f, 180.f},
    {"headingChangeDeg", "Virage", "°", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
    {"turnRateDegPerS", "Vitesse de rotation", "°/s", 1.f, B::Fixed, 150.f, kBig, 100.f, 200.f, nullptr, 0, false, 0.f, 1e6f},
};
const std::vector<MetricSpec> kBoost = {
    {"wasteRatio", "Boost gaspillé à vitesse max", "%", 100.f, B::Fixed, -kBig, 5.f, 45.f, 0.f, nullptr, 0, false, 0.f, 2.f},
    {"coastAtSpeedRatio", "Sans accélérateur en supersonique", "%", 100.f, B::Fixed, -kBig, 5.f, 45.f, 0.f, nullptr, 0, false, 0.f, 2.f},
    {"supersonicRatio", "Temps supersonique", "%", 100.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, 0.f, 2.f},
    {"boostUsed", "Boost utilisé", "", 1.f, B::None, 0.f, 0.f, 0.f, 0.f, nullptr, 0, false, 0.f, 1e6f},
};

// {key, scale, lowerIsBetter, needSuccess, unit, label, decimals}
const RecordDef kRecords[] = {
    {"timeToBall", 1.f, true, false, "s", "Temps jusqu'à la balle", 2},          // SpeedFlip
    {"landingTicks", kPerTickS, true, true, "s", "Demi-tour le plus rapide", 2},  // HalfFlip
    {"cancelTicks", kMsTick, true, true, "ms", "Annulation la plus rapide", 0},         // FlipCancel
    {"gainAlongDash", 1.f, false, true, "uu/s", "Meilleur gain", 0},              // Wavedash
    {"timeToSupersonic", 1.f, true, false, "s", "Supersonique le plus rapide", 2},  // ChainWavedash
    {"gainAlongDash", 1.f, false, true, "uu/s", "Meilleur gain", 0},              // ZapDash
    {"gainAlongDash", 1.f, false, true, "uu/s", "Meilleur gain", 0},              // WallDash
    {"gainAlongDash", 1.f, false, true, "uu/s", "Meilleur gain", 0},              // CeilingDash
    {nullptr, 1.f, false, false, "", "Meilleur score", 0},                         // SpeedZap
    {nullptr, 1.f, false, false, "", "Meilleur score", 0},                         // HalfFlipWavedash
    {"vzEnd", 1.f, false, true, "uu/s", "Vitesse verticale max", 0},              // FastAerial
    {"vzEnd", 1.f, false, true, "uu/s", "Vitesse verticale max", 0},              // DoubleJumpAerial
    {"hoverTicks", kMsTick, false, true, "ms", "Flottement le plus long", 0},           // Stall
    {"durationTicks", kPerTickS, false, true, "s", "Rotation la plus longue", 1},  // AirSpin
    {nullptr, 1.f, false, false, "", "Meilleur score", 0},                         // Recovery
    {"speedRetained", 100.f, false, true, "%", "Meilleure vitesse conservée", 0},  // Powerslide
    {nullptr, 1.f, false, false, "", "Meilleur score", 0},                         // BoostEfficiency
    {"dashes", 1.f, false, true, "", "Plus longue chaîne", 0},                    // WallChain
};

static_assert(std::size(kRecords) == static_cast<size_t>(kMechCount), "one RecordDef per Mech, in enum order");

struct Resolved {
    float lo, hi, tol, ideal;
    bool hasIdeal;
};

// Band and ideal of one spec for this grade (idealKey, config-driven speedflip targets).
Resolved resolve(Mech m, const MetricSpec& sp, const DetectConfig& cfg, const MechGrade* g) {
    Resolved r{sp.lo, sp.hi, sp.tol, sp.ideal, sp.band != B::None};
    if (sp.idealKey) {
        if (std::strcmp(sp.idealKey, "targetAngleDeg") == 0) {
            r.ideal = g ? g->metric("targetAngleDeg", cfg.speedflipTargetAngleDeg) : cfg.speedflipTargetAngleDeg;
            r.hasIdeal = true;
        } else if (g && g->hasMetric(sp.idealKey)) {
            r.ideal = g->metric(sp.idealKey) * sp.scale;
            r.hasIdeal = true;
        } else if (std::strcmp(sp.idealKey, "deadzone") == 0) {
            r.ideal = cfg.dodgeDeadzone;
            r.hasIdeal = true;
        } else {
            r.hasIdeal = false;
        }
    }
    if (sp.band == B::AroundIdeal) {
        r.lo = r.ideal + sp.lo;
        r.hi = r.ideal + sp.hi;
    }
    if (m == Mech::SpeedFlip && std::strcmp(sp.key, "cancelTicks") == 0) {
        r.hi = static_cast<float>(cfg.speedflipCancelGreenTicks) * kMsTick;
        r.ideal = std::min(r.ideal, r.hi);
    }
    return r;
}

std::string formatValue(float v, const char* unit, int decimals, SpeedUnit su) {
    if (std::strcmp(unit, "uu/s") == 0) return formatSpeed(v, su, 0);
    std::string s = formatNumber(v, decimals);
    if (unit[0] == '\0') return s;
    if (std::strcmp(unit, "°") == 0 || std::strcmp(unit, "/100") == 0) return s + unit;
    return s + " " + unit;
}

std::string idealText(const MetricSpec& sp, const Resolved& r, SpeedUnit su) {
    if (!r.hasIdeal) return {};
    if (sp.band == B::AroundIdeal)
        return formatValue(r.ideal, sp.unit, sp.decimals, su) + " +/-" + formatNumber(sp.hi, sp.decimals);
    if (sp.band == B::None) return formatValue(r.ideal, sp.unit, sp.decimals, su);
    const bool openLo = r.lo <= -kBig * 0.5f, openHi = r.hi >= kBig * 0.5f;
    if (openLo && !openHi) return "<= " + formatValue(r.hi, sp.unit, sp.decimals, su);
    if (openHi && !openLo) return ">= " + formatValue(r.lo, sp.unit, sp.decimals, su);
    if (!openLo && !openHi) {
        if (r.lo == r.hi) return formatValue(r.lo, sp.unit, sp.decimals, su);
        return formatNumber(r.lo, sp.decimals) + " à " + formatValue(r.hi, sp.unit, sp.decimals, su);
    }
    return formatValue(r.ideal, sp.unit, sp.decimals, su);
}

float quality(const MetricSpec& sp, const Resolved& r, float v) {
    if (sp.band == B::None) return -1.f;
    return bandPts(v, r.lo, r.hi, r.lo - r.tol, r.hi + r.tol) / 100.f;
}

bool validRaw(const MetricSpec& sp, const MechGrade& g, float& raw) {
    if (!g.hasMetric(sp.key)) return false;
    raw = g.metric(sp.key);
    return std::isfinite(raw) && raw >= sp.validMin && raw <= sp.validMax;
}

} // namespace

const std::vector<MetricSpec>& metricSpecs(Mech m) {
    switch (m) {
    case Mech::SpeedFlip: return kSpeedFlip;
    case Mech::HalfFlip: return kHalfFlip;
    case Mech::FlipCancel: return kFlipCancel;
    case Mech::Wavedash:
    case Mech::ZapDash:
    case Mech::CeilingDash: return kWavedash;
    case Mech::WallDash: return kWallDash;
    case Mech::ChainWavedash: return kChain;
    case Mech::WallChain: return kWallChain;
    case Mech::SpeedZap:
    case Mech::HalfFlipWavedash: return kCombo;
    case Mech::FastAerial: return kFastAerial;
    case Mech::DoubleJumpAerial: return kDoubleJumpAerial;
    case Mech::Stall: return kStall;
    case Mech::AirSpin: return kAirSpin;
    case Mech::Recovery: return kRecovery;
    case Mech::Powerslide: return kPowerslide;
    case Mech::BoostEfficiency: return kBoost;
    default: return kSpeedFlip;
    }
}

const RecordDef& recordDef(Mech m) {
    const int i = index(m);
    return kRecords[(i >= 0 && i < kMechCount) ? i : 0];
}

AttemptResult buildResult(const MechGrade& g, const PlayerProfile& profile, const DetectConfig& cfg, float time,
                          KeyStyle style) {
    AttemptResult r;
    const MechInfo& info = mechInfo(g.mech);
    r.module = "mechanics";
    r.skill = std::string("mech.") + info.key;
    r.title = info.title;
    r.score = g.score;
    r.success = g.success;
    Verdict v = verdictFromScore(g.score);
    if (!g.success && (v == Verdict::Good || v == Verdict::Perfect)) v = Verdict::Ok;
    if (g.success && v == Verdict::Fail) v = Verdict::Ok;
    r.verdict = v;
    r.time = time;

    for (const MetricSpec& sp : metricSpecs(g.mech)) {
        if (r.metrics.size() >= 5) break;
        float raw = 0.f;
        if (!validRaw(sp, g, raw)) continue;
        const Resolved rs = resolve(g.mech, sp, cfg, &g);
        Metric m;
        m.key = sp.key;
        m.label = sp.label;
        m.value = sp.absolute ? std::fabs(raw * sp.scale) : raw * sp.scale;
        m.unit = sp.unit;
        m.ideal = rs.ideal;
        m.hasIdeal = rs.hasIdeal;
        r.metrics.push_back(std::move(m));
    }

    for (const MechError& e : g.errors) {
        if (r.tips.size() >= 3) break;
        const std::string code = tipCode(e.code);
        bool dup = false;
        for (const Tip& t : r.tips) dup = dup || t.code == code;
        if (dup) continue;
        r.tips.push_back({code, tipText(e.code, g, profile, cfg.dodgeDeadzone, style), e.weight});
    }

    const RecordDef& rd = recordDef(g.mech);
    if (!rd.key) {
        r.valueForRecord = g.score;
        r.lowerIsBetter = false;
        r.hasRecordValue = true;
    } else if (g.hasMetric(rd.key) && g.metric(rd.key) >= 0.f && (!rd.needSuccess || g.success)) {
        r.valueForRecord = g.metric(rd.key) * rd.scale;
        r.lowerIsBetter = rd.lowerIsBetter;
        r.hasRecordValue = true;
    }
    return r;
}

std::vector<MetricLine> metricLines(const MechGrade& g, const DetectConfig& cfg, SpeedUnit unit, size_t max) {
    std::vector<MetricLine> out;
    for (const MetricSpec& sp : metricSpecs(g.mech)) {
        if (out.size() >= max) break;
        float raw = 0.f;
        if (!validRaw(sp, g, raw)) continue;
        const Resolved rs = resolve(g.mech, sp, cfg, &g);
        const float v = sp.absolute ? std::fabs(raw * sp.scale) : raw * sp.scale;
        MetricLine l;
        l.label = sp.label;
        l.value = formatValue(v, sp.unit, sp.decimals, unit);
        l.ideal = idealText(sp, rs, unit);
        l.quality = quality(sp, rs, v);
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<std::string> targetLines(Focus f, const DetectConfig& cfg, SpeedUnit unit, size_t max) {
    std::vector<std::string> out;
    if (f == Focus::None) return out;
    const Mech m = focusInfo(f).primary;
    for (const MetricSpec& sp : metricSpecs(m)) {
        if (out.size() >= max) break;
        if (sp.band == B::None) continue;
        const Resolved rs = resolve(m, sp, cfg, nullptr);
        const std::string t = idealText(sp, rs, unit);
        if (!t.empty()) out.push_back(std::string(sp.label) + " : " + t);
    }
    return out;
}

std::string formatRecord(Mech m, float value, SpeedUnit unit) {
    const RecordDef& rd = recordDef(m);
    if (!rd.key) return formatNumber(value, 0) + "/100";
    return formatValue(value, rd.unit, rd.decimals, unit);
}

Color verdictColor(Verdict v) {
    switch (v) {
    case Verdict::Perfect: return Color(0, 230, 255, 255);
    case Verdict::Good: return Color(80, 230, 80, 255);
    case Verdict::Ok: return Color(255, 210, 60, 255);
    case Verdict::Fail: return Color(255, 80, 60, 255);
    default: return colors::White;
    }
}

Color qualityColor(float q) {
    if (q < 0.f) return Color(220, 220, 220, 255);
    const Color red(255, 80, 60, 255), yellow(255, 210, 60, 255), green(80, 230, 80, 255);
    if (q < 0.5f) return mix(red, yellow, q * 2.f);
    return mix(yellow, green, (q - 0.5f) * 2.f);
}

} // namespace rlt::mech
