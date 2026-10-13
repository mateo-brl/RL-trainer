#pragma once
// Première touche: Analyse de chaque touche, note, coach et exercices de première touche.

#include "core/common/Module.h"
#include "core/modules/firsttouch/FirstTouchConfig.h"

namespace rlt {

class FirstTouchModule : public Module {
public:
    const char* id() const override { return "firsttouch"; }
    const char* title() const override { return "Première touche"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const FirstTouchConfig& config() const { return cfg_; }

private:
    FirstTouchConfig cfg_;
};

} // namespace rlt
