#include "core/modules/profile/ProfileModule.h"

namespace rlt {

void ProfileModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::profile, "profile", "Ma config");
    w.toggle("rlt_pf_enabled", &ProfileConfig::enabled, true, "Activer", "Active le module Ma config.");
}

void ProfileModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.profile; }

} // namespace rlt
