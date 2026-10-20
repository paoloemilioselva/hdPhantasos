// A deliberately tiny assertion harness, as in hdClaude: the core has no
// third-party dependency, and a test framework would undo that.

#ifndef HDPHANTASOS_TESTS_TEST_SUPPORT_H
#define HDPHANTASOS_TESTS_TEST_SUPPORT_H

#include <cstdio>
#include <string>

namespace phantasos_test {

inline int g_failures = 0;
inline int g_checks = 0;

inline void Report(bool passed, const char* expression, const char* file, int line,
                   const std::string& detail = {})
{
    ++g_checks;
    if (passed) return;
    ++g_failures;
    std::fprintf(stderr, "FAIL %s:%d\n  %s\n", file, line, expression);
    if (!detail.empty()) std::fprintf(stderr, "  %s\n", detail.c_str());
}

inline int Finish(const char* suite)
{
    std::printf("%s: %d checks, %d failures\n", suite, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace phantasos_test

#define CHECK(expression) \
    ::phantasos_test::Report(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#define CHECK_MSG(expression, detail) \
    ::phantasos_test::Report(static_cast<bool>(expression), #expression, __FILE__, __LINE__, (detail))

#endif
