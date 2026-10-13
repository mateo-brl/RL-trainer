#include "core/modules/shooting/ShootingModule.h"

namespace rlt {

void ShootingModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::shooting, "shooting", "Tirs & flicks");
    w.toggle("rlt_sh_enabled", &ShootingConfig::enabled, true, "Activer", "Active le module Tirs & flicks.");
}

void ShootingModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.shooting; }

} // namespace rlt
