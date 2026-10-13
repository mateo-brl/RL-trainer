#include "core/modules/mechanics/MechanicsModule.h"

namespace rlt {

void MechanicsModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::mechanics, "mechanics", "Mécas mouvement");
    w.toggle("rlt_me_enabled", &MechanicsConfig::enabled, true, "Activer", "Active le module Mécas mouvement.");
}

void MechanicsModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.mechanics; }

} // namespace rlt
