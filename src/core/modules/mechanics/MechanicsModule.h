#pragma once
// Mécaniques: Détection et notation des mécaniques (speedflip, wavedash, flip reset...).

#include "core/common/Module.h"
#include "core/modules/mechanics/MechanicsConfig.h"

namespace rlt {

class MechanicsModule : public Module {
public:
    const char* id() const override { return "mechanics"; }
    const char* title() const override { return "Mécaniques"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const MechanicsConfig& config() const { return cfg_; }

private:
    MechanicsConfig cfg_;
};

} // namespace rlt
