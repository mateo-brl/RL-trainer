#include "core/modules/maps/MapsModule.h"

namespace rlt {

void MapsModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::maps, "maps", "Maps");
    w.toggle("rlt_map_enabled", &MapsConfig::enabled, true, "Activer", "Active le module Maps.");
}

void MapsModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.maps; }

} // namespace rlt
