#pragma once
// Affichage: HUD de base : écart de vitesse, chrono de session, cercle de contact, sphère.

#include "core/common/Module.h"
#include "core/modules/overlay/OverlayConfig.h"

namespace rlt {

class OverlayModule : public Module {
public:
    const char* id() const override { return "overlay"; }
    const char* title() const override { return "Affichage"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const OverlayConfig& config() const { return cfg_; }

private:
    OverlayConfig cfg_;
};

} // namespace rlt
