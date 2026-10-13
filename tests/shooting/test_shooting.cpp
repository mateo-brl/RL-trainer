#include "tests/TestFramework.h"

#include "core/modules/shooting/ShootingModule.h"

using namespace rlt;

TEST(shooting_stub_has_id) {
    ShootingModule m;
    CHECK(std::string(m.id()) == "shooting");
}
