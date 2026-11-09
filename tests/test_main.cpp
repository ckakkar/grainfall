#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>

#include "testing.hpp"

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    int failed = 0;
    const auto start = std::chrono::steady_clock::now();

    for (const auto& test : testing::registry()) {
        if (filter && !std::strstr(test.name, filter)) continue;
        ++ran;
        testing::current_failures() = 0;
        std::cout << "[ RUN  ] " << test.name << "\n";
        try {
            test.fn();
        } catch (const testing::RequireFailed&) {
            // already reported
        } catch (const std::exception& e) {
            testing::report_failure(test.file, test.line, std::string("uncaught exception: ") + e.what());
        } catch (...) {
            testing::report_failure(test.file, test.line, "uncaught non-std exception");
        }
        if (testing::current_failures() > 0) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << "\n";
        } else {
            std::cout << "[  OK  ] " << test.name << "\n";
        }
    }

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "\n" << ran - failed << "/" << ran << " tests passed in " << ms << " ms\n";
    return failed == 0 ? 0 : 1;
}
