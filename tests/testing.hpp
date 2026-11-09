#pragma once
// Minimal self-registering test framework (no dependencies).
//
//   TEST(water_boils) {
//       CHECK(x == 1);           // records a failure, keeps going
//       CHECK_EQ(a, b);          // prints both values on failure
//       REQUIRE(ptr != nullptr); // records a failure and stops this test
//   }
//
// Run: ./grainfall_tests            (all tests)
//      ./grainfall_tests water      (only tests whose name contains "water")

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
    const char* name;
    const char* file;
    int line;
    std::function<void()> fn;
};

struct RequireFailed {};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& current_failures() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, const char* file, int line, std::function<void()> fn) {
        registry().push_back({name, file, line, std::move(fn)});
    }
};

inline void report_failure(const char* file, int line, const std::string& message) {
    ++current_failures();
    std::cerr << "    " << file << ":" << line << ": " << message << "\n";
}

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    if constexpr (requires { out << value; }) {
        out << value;
    } else {
        out << "<unprintable>";
    }
    return out.str();
}

}  // namespace testing

#define TESTING_CONCAT_INNER(a, b) a##b
#define TESTING_CONCAT(a, b) TESTING_CONCAT_INNER(a, b)

#define TEST(name)                                                                            \
    static void TESTING_CONCAT(test_fn_, name)();                                             \
    static ::testing::Registrar TESTING_CONCAT(test_reg_, name)(#name, __FILE__, __LINE__,    \
                                                                &TESTING_CONCAT(test_fn_, name)); \
    static void TESTING_CONCAT(test_fn_, name)()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) ::testing::report_failure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                          \
    do {                                                                                        \
        const auto& testing_a_ = (a);                                                           \
        const auto& testing_b_ = (b);                                                           \
        if (!(testing_a_ == testing_b_))                                                        \
            ::testing::report_failure(__FILE__, __LINE__,                                       \
                                      "CHECK_EQ(" #a ", " #b "): " + ::testing::show(testing_a_) + \
                                          " != " + ::testing::show(testing_b_));                \
    } while (0)

#define REQUIRE_EQ(a, b)                                                                        \
    do {                                                                                        \
        const auto& testing_a_ = (a);                                                           \
        const auto& testing_b_ = (b);                                                           \
        if (!(testing_a_ == testing_b_)) {                                                      \
            ::testing::report_failure(__FILE__, __LINE__,                                       \
                                      "REQUIRE_EQ(" #a ", " #b "): " + ::testing::show(testing_a_) + \
                                          " != " + ::testing::show(testing_b_));                \
            throw ::testing::RequireFailed{};                                                   \
        }                                                                                       \
    } while (0)

#define REQUIRE(cond)                                                                  \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            ::testing::report_failure(__FILE__, __LINE__, "REQUIRE(" #cond ")");       \
            throw ::testing::RequireFailed{};                                          \
        }                                                                              \
    } while (0)
