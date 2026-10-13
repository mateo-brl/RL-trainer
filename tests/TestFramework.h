#pragma once
// Minimal self-registering test framework (no dependency). Usage:
//   TEST(my_test) { CHECK(1 + 1 == 2); CHECK_NEAR(0.1f + 0.2f, 0.3f, 1e-6f); }
// Every test binary links tests/TestMain.cpp, which runs all registered tests and returns the failure count.

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace rlt_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

inline int& failures() {
    static int f = 0;
    return f;
}

inline int& checks() {
    static int c = 0;
    return c;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void reportFailure(const char* file, int line, const std::string& what) {
    ++failures();
    std::printf("  FAILED %s:%d: %s\n", file, line, what.c_str());
}

} // namespace rlt_test

#define RLT_TEST_CONCAT2(a, b) a##b
#define RLT_TEST_CONCAT(a, b) RLT_TEST_CONCAT2(a, b)

#define TEST(name)                                                                              \
    static void RLT_TEST_CONCAT(test_fn_, name)();                                              \
    static ::rlt_test::Registrar RLT_TEST_CONCAT(test_reg_, name)(#name, &RLT_TEST_CONCAT(test_fn_, name)); \
    static void RLT_TEST_CONCAT(test_fn_, name)()

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        ++::rlt_test::checks();                                                                 \
        if (!(cond)) ::rlt_test::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");         \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                    \
    do {                                                                                        \
        ++::rlt_test::checks();                                                                 \
        if (!(cond)) ::rlt_test::reportFailure(__FILE__, __LINE__, std::string("CHECK(" #cond ") ") + (msg)); \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                   \
    do {                                                                                        \
        ++::rlt_test::checks();                                                                 \
        const double rlt_a_ = static_cast<double>(a), rlt_b_ = static_cast<double>(b);          \
        if (!(std::fabs(rlt_a_ - rlt_b_) <= static_cast<double>(tol)))                          \
            ::rlt_test::reportFailure(__FILE__, __LINE__,                                       \
                                      std::string("CHECK_NEAR(" #a ", " #b ") got ") + std::to_string(rlt_a_) + \
                                          " vs " + std::to_string(rlt_b_));                    \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        ++::rlt_test::checks();                                                                 \
        if (!((a) == (b))) ::rlt_test::reportFailure(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
    } while (0)
