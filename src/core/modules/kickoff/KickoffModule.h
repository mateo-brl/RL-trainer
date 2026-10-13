#pragma once
// Kickoff: Entraînement au kickoff avec chrono, fantôme et notes.

#include "core/common/Module.h"
#include "core/modules/kickoff/KickoffConfig.h"

namespace rlt {

class KickoffModule : public Module {
public:
    const char* id() const override { return "kickoff"; }
    const char* title() const override { return "Kickoff"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const KickoffConfig& config() const { return cfg_; }

private:
    KickoffConfig cfg_;
};

} // namespace rlt
