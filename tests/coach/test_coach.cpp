#include "tests/TestFramework.h"

#include "core/modules/coach/CoachModule.h"

using namespace rlt;

TEST(coach_stub_has_id) {
    CoachModule m;
    CHECK(std::string(m.id()) == "coach");
}
