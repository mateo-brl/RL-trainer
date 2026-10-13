#include "core/modules/overlay/OverlayModule.h"

namespace rlt {

void OverlayModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::overlay, "overlay", "Affichage");
    w.toggle("rlt_ov_enabled", &OverlayConfig::enabled, true, "Activer", "Active le module Affichage.");
}

void OverlayModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.overlay; }

} // namespace rlt
