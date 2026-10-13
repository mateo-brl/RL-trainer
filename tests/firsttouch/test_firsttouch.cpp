#include "tests/TestFramework.h"

#include "core/modules/firsttouch/FirstTouchModule.h"

using namespace rlt;

TEST(firsttouch_stub_has_id) {
    FirstTouchModule m;
    CHECK(std::string(m.id()) == "firsttouch");
}
