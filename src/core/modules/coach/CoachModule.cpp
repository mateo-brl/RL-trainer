#include "core/modules/coach/CoachModule.h"

namespace rlt {

void CoachModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::coach, "coach", "Coach & progression");
    w.toggle("rlt_coach_enabled", &CoachConfig::enabled, true, "Activer", "Active le module Coach & progression.");
}

void CoachModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.coach; }

} // namespace rlt
