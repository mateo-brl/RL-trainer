#pragma once
// Coach & progression: Feedback, records, historique, plan d'entraînement adaptatif.

#include "core/common/Module.h"
#include "core/modules/coach/CoachConfig.h"

namespace rlt {

class CoachModule : public Module {
public:
    const char* id() const override { return "coach"; }
    const char* title() const override { return "Coach & progression"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const CoachConfig& config() const { return cfg_; }

private:
    CoachConfig cfg_;
};

} // namespace rlt
