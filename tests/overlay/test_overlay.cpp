#include "tests/TestFramework.h"

#include "core/modules/overlay/OverlayModule.h"

using namespace rlt;

TEST(overlay_stub_has_id) {
    OverlayModule m;
    CHECK(std::string(m.id()) == "overlay");
}
