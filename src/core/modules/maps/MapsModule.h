#pragma once
// Maps: Maps workshop : catalogue, profils automatiques, chronos et stats par map.

#include "core/common/Module.h"
#include "core/modules/maps/MapsConfig.h"

namespace rlt {

class MapsModule : public Module {
public:
    const char* id() const override { return "maps"; }
    const char* title() const override { return "Maps"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const MapsConfig& config() const { return cfg_; }

private:
    MapsConfig cfg_;
};

} // namespace rlt
