#pragma once
// Turns internal grades into what the player sees: AttemptResult (2-5 metrics with ideal values, ranked French
// tips, record value) and HUD lines "mesuré / idéal" coloured by how close each metric is to its ideal band.

#include <string>
#include <vector>

#include "core/common/Color.h"
#include "core/common/Context.h"
#include "core/common/Controls.h"
#include "core/common/Grade.h"
#include "core/common/Units.h"
#include "core/modules/mechanics/MechCommon.h"

namespace rlt::mech {

// One displayed metric of a mechanic.
struct MetricSpec {
    enum class Band : int { None, Fixed, AroundIdeal };
    const char* key;          // internal metric key
    const char* label;        // French
    const char* unit;         // "ms", "°", "%", "uu/s", "s", "" ...
    float scale;              // display value = raw * scale
    Band band;
    float lo, hi;             // good band (display units); AroundIdeal: offsets from the ideal
    float tol;                // quality reaches 0 at lo - tol / hi + tol
    float ideal;              // ideal shown to the player (display units)
    const char* idealKey;     // ideal read from another raw metric (scaled), or nullptr
    int decimals;
    bool absolute;            // display |value|
    float validMin, validMax; // raw values outside are "not measured" and skipped
};

struct MetricLine {
    std::string label;
    std::string value;   // "42 ms"
    std::string ideal;   // "<= 50 ms" (empty when no ideal)
    float quality = -1.f;  // 0..1, < 0 = not rated
};

// Personal-best definition of a mechanic.
struct RecordDef {
    const char* key;      // raw metric key, nullptr = score
    float scale;          // display = raw * scale
    bool lowerIsBetter;
    bool needSuccess;     // only successful attempts can set the record
    const char* unit;
    const char* label;    // French label of the record ("Temps jusqu'à la balle")
    int decimals;
};

const std::vector<MetricSpec>& metricSpecs(Mech m);
const RecordDef& recordDef(Mech m);

// Builds the AttemptResult of a grade (module "mechanics", skill "mech.<key>").
AttemptResult buildResult(const MechGrade& g, const PlayerProfile& profile, const DetectConfig& cfg, float time,
                          KeyStyle style = KeyStyle::Auto);
// HUD lines for a grade (at most `max`), speeds shown in `unit`.
std::vector<MetricLine> metricLines(const MechGrade& g, const DetectConfig& cfg, SpeedUnit unit, size_t max);
// "Angle du flip : 30° +/-8" style targets of a family (focus mode HUD).
std::vector<std::string> targetLines(Focus f, const DetectConfig& cfg, SpeedUnit unit, size_t max);
// Value of a record formatted with its unit ("2,41 s", "512 uu/s" in the player's unit).
std::string formatRecord(Mech m, float value, SpeedUnit unit);

Color verdictColor(Verdict v);
Color qualityColor(float quality01);

} // namespace rlt::mech
