#include "core/modules/courses/CoursesModule.h"

namespace rlt {

void CoursesModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::courses, "courses", "Parcours");
    w.toggle("rlt_co_enabled", &CoursesConfig::enabled, true, "Activer", "Active le module Parcours.");
}

void CoursesModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.courses; }

} // namespace rlt
