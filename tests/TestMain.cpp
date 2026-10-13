#include <cstdio>
#include <cstring>

#include "tests/TestFramework.h"

// Runs every registered test. Optional argument: substring filter on test names.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto& t : rlt_test::registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = rlt_test::failures();
        t.fn();
        ++run;
        std::printf("[%s] %s\n", rlt_test::failures() == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d checks, %d failures\n", run, rlt_test::checks(), rlt_test::failures());
    return rlt_test::failures() == 0 ? 0 : 1;
}
