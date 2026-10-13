#include "tests/TestFramework.h"

#include "core/modules/mechanics/MechanicsModule.h"

using namespace rlt;

TEST(mechanics_stub_has_id) {
    MechanicsModule m;
    CHECK(std::string(m.id()) == "mechanics");
}
