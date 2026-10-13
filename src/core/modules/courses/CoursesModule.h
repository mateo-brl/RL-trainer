#pragma once
// Parcours: Maps virtuelles : parcours d'anneaux (vol, sol, air dribble) et parcours de contrôle (dribble, slalom, rattrapages).

#include "core/common/Module.h"
#include "core/modules/courses/CoursesConfig.h"

namespace rlt {

class CoursesModule : public Module {
public:
    const char* id() const override { return "courses"; }
    const char* title() const override { return "Parcours"; }

    static void describeSettings(SettingsSchema& schema);

    void onSettingsChanged(const Settings& settings) override;

    const CoursesConfig& config() const { return cfg_; }

private:
    CoursesConfig cfg_;
};

} // namespace rlt
