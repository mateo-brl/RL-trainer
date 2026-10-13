#include "tests/TestFramework.h"

#include "core/modules/ballmech/BallMechModule.h"

using namespace rlt;

TEST(ballmech_stub_has_id) {
    BallMechModule m;
    CHECK(std::string(m.id()) == "ballmech");
}
