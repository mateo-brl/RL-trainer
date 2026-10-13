#include "core/modules/firsttouch/FirstTouchModule.h"

namespace rlt {

void FirstTouchModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::firstTouch, "firsttouch", "Première touche");
    w.toggle("rlt_ft_enabled", &FirstTouchConfig::enabled, true, "Activer", "Active le module Première touche.");
}

void FirstTouchModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.firstTouch; }

} // namespace rlt
