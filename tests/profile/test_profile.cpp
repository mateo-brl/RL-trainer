#include "tests/TestFramework.h"

#include "core/modules/profile/ProfileModule.h"

using namespace rlt;

TEST(profile_stub_has_id) {
    ProfileModule m;
    CHECK(std::string(m.id()) == "profile");
}
