#pragma once
// Mécas mouvement : détection et notation des mécaniques de déplacement (speedflip, wavedash, half-flip...).

#include "core/common/Module.h"
#include "core/modules/mechanics/MechanicsConfig.h"

namespace rlt {

class MechanicsModule : public Module {
public:
    const char* id() const override { return "mechanics"; }
    const char* title() const override { return "Mécas mouvement"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const MechanicsConfig& config() const { return cfg_; }

private:
    MechanicsConfig cfg_;
};

} // namespace rlt
