#include "core/modules/ballmech/BallMechModule.h"

namespace rlt {

void BallMechModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::ballmech, "ballmech", "Mécas balle");
    w.toggle("rlt_bm_enabled", &BallMechConfig::enabled, true, "Activer", "Active le module Mécas balle.");
}

void BallMechModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.ballmech; }

} // namespace rlt
