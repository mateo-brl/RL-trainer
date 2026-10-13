#pragma once
// Ma config: Détection automatique de ta config Rocket League (touches, caméra, voiture).

#include "core/common/Module.h"
#include "core/modules/profile/ProfileConfig.h"

namespace rlt {

class ProfileModule : public Module {
public:
    const char* id() const override { return "profile"; }
    const char* title() const override { return "Ma config"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const ProfileConfig& config() const { return cfg_; }

private:
    ProfileConfig cfg_;
};

} // namespace rlt
