#pragma once
// Prédiction: Trajectoire de la balle avec rebonds et point d'interception.

#include "core/common/Module.h"
#include "core/modules/prediction/PredictionConfig.h"

namespace rlt {

class PredictionModule : public Module {
public:
    const char* id() const override { return "prediction"; }
    const char* title() const override { return "Prédiction"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const PredictionConfig& config() const { return cfg_; }

private:
    PredictionConfig cfg_;
};

} // namespace rlt
