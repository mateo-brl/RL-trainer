#pragma once
// Tirs & flicks: Mode flick (balle sur le toit) et tirs de précision avec cibles dans le but.

#include "core/common/Module.h"
#include "core/modules/shooting/ShootingConfig.h"

namespace rlt {

class ShootingModule : public Module {
public:
    const char* id() const override { return "shooting"; }
    const char* title() const override { return "Tirs & flicks"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const ShootingConfig& config() const { return cfg_; }

private:
    ShootingConfig cfg_;
};

} // namespace rlt
