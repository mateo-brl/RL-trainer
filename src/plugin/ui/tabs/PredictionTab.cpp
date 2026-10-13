#include "plugin/ui/Ui.h"

#include "imgui.h"

namespace rlt {

void renderPredictionTab(UiContext& ctx) { ui::moduleSettings(ctx, "prediction"); }

} // namespace rlt
