#include "tests/TestFramework.h"

#include "core/modules/courses/CoursesModule.h"

using namespace rlt;

TEST(courses_stub_has_id) {
    CoursesModule m;
    CHECK(std::string(m.id()) == "courses");
}
