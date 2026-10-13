#include "tests/TestFramework.h"

#include "core/modules/kickoff/KickoffModule.h"

using namespace rlt;

TEST(kickoff_stub_has_id) {
    KickoffModule m;
    CHECK(std::string(m.id()) == "kickoff");
}
