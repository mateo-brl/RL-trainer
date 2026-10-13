#pragma once
// Mécas balle: Détection et notation des mécas avec balle (flip reset, ceiling shot, flicks, double tap, air dribble...).

#include "core/common/Module.h"
#include "core/modules/ballmech/BallMechConfig.h"

namespace rlt {

class BallMechModule : public Module {
public:
    const char* id() const override { return "ballmech"; }
    const char* title() const override { return "Mécas balle"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const BallMechConfig& config() const { return cfg_; }

private:
    BallMechConfig cfg_;
};

} // namespace rlt
