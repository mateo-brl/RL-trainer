#include "tests/TestFramework.h"

#include "core/modules/maps/MapsModule.h"

using namespace rlt;

TEST(maps_stub_has_id) {
    MapsModule m;
    CHECK(std::string(m.id()) == "maps");
}
