#pragma once
// Detector contract shared by every ball-mechanic family.
//
// A detector is fed one Frame per physics tick (with the previous one) and returns a MechGrade when an attempt is
// resolved. It never touches the world. Several detectors may grade the same touch; the Arbiter keeps only the
// most specific one (see Arbiter.h). `engaged()` tells the arbiter that an attempt is in progress and may still
// claim recent touches, so less specific grades are held until it resolves.

#include <optional>
#include <string>
#include <vector>

#include "core/common/Grade.h"
#include "core/modules/ballmech/Primitives.h"

namespace rlt::bm {

// Detector families, in arbitration priority order (lower value = more specific, wins overlaps).
enum class Family : int { FlipReset = 0, Ceiling, DoubleTap, Pinch, Flick, AirDribble, Aerial, PowerShot, Count };
constexpr int kFamilyCount = static_cast<int>(Family::Count);

const char* familyId(Family f);  // "flip_reset", "ceiling_shot", ...

struct MechGrade {
    Family family = Family::Aerial;
    std::string variant;               // detector variant ("used", "45", "drop", "ground_to_air_carry", ...)
    bool success = false;              // mechanical success (the drill may add stricter criteria)
    float score = 0.f;                 // 0..1
    std::vector<Metric> metrics;
    std::vector<std::string> errors;   // error codes (see Tips.h), most important first
    float tStart = 0.f, tEnd = 0.f;    // game time window covered by the attempt
    bool weak = false;                 // incomplete signature: loses overlaps against any strong grade
    bool drillOnly = false;            // only meaningful inside the matching drill (not reported in free play)
    // Shot facts (for HUD, records and drawing).
    bool hasShot = false;
    float ballSpeed = 0.f;
    GoalProjection projection;

    float value(const std::string& key, float def = 0.f) const {
        for (const auto& m : metrics)
            if (m.key == key) return m.value;
        return def;
    }
    bool has(const std::string& key) const {
        for (const auto& m : metrics)
            if (m.key == key) return true;
        return false;
    }
    bool hasError(const std::string& code) const {
        for (const auto& e : errors)
            if (e == code) return true;
        return false;
    }
};

struct DetectParams {
    float pinchSpeed = 4000.f;
    float aerialMinSpeed = 1200.f;
    float powerMinSpeed = 1500.f;
};

class Detector {
public:
    virtual ~Detector() = default;
    virtual Family family() const = 0;
    virtual void reset() = 0;
    virtual std::optional<MechGrade> update(const Frame& prev, const Frame& cur, const DetectParams& p) = 0;
    // True while an attempt is in progress; `since` = start of the window it will claim.
    virtual bool engaged(float& since) const {
        (void)since;
        return false;
    }
};

} // namespace rlt::bm
