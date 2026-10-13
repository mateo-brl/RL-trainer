#pragma once
// Speed/distance unit conversions. 1 uu = 1 cm, so 1 uu/s = 0.036 km/h and 44.704 uu/s = 1 mph.

#include <string>

namespace rlt {

enum class SpeedUnit : int { Auto = 0, Kph = 1, Mph = 2, Uus = 3 };

constexpr float kUusToKph = 0.036f;
constexpr float kUusPerMph = 44.704f;

inline float convertSpeed(float uus, SpeedUnit unit) {
    switch (unit) {
    case SpeedUnit::Mph: return uus / kUusPerMph;
    case SpeedUnit::Uus: return uus;
    case SpeedUnit::Kph:
    case SpeedUnit::Auto:
    default: return uus * kUusToKph;
    }
}

// Resolves Auto with the game setting (GameWrapper::GetbMetric()).
inline SpeedUnit resolveUnit(SpeedUnit configured, bool gameIsMetric) {
    if (configured != SpeedUnit::Auto) return configured;
    return gameIsMetric ? SpeedUnit::Kph : SpeedUnit::Mph;
}

inline const char* unitSuffix(SpeedUnit unit) {
    switch (unit) {
    case SpeedUnit::Mph: return "MPH";
    case SpeedUnit::Uus: return "uu/s";
    case SpeedUnit::Kph:
    case SpeedUnit::Auto:
    default: return "KPH";
    }
}

} // namespace rlt
