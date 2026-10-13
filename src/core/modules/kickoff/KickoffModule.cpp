#include "core/modules/kickoff/KickoffModule.h"

namespace rlt {

void KickoffModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::kickoff, "kickoff", "Kickoff");
    w.toggle("rlt_ko_enabled", &KickoffConfig::enabled, true, "Activer", "Active le module Kickoff.");
}

void KickoffModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.kickoff; }

} // namespace rlt
