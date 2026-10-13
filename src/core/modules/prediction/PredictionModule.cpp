#include "core/modules/prediction/PredictionModule.h"

namespace rlt {

void PredictionModule::describeSettings(SettingsSchema& schema) {
    auto w = schema.section(&Settings::prediction, "prediction", "Prédiction");
    w.toggle("rlt_pr_enabled", &PredictionConfig::enabled, true, "Activer", "Active le module Prédiction.");
}

void PredictionModule::onSettingsChanged(const Settings& settings) { cfg_ = settings.prediction; }

} // namespace rlt
