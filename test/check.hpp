// Minimal assertion harness. No third-party test framework by design.
// Usage:
//   #include "check.hpp"
//   int main() { CHECK_EQ(2 + 2, 4); CHECK_REPORT(); }
// A test binary returns 0 when every check passed, 1 otherwise.
// Return 77 instead to tell CTest the test was skipped (see SKIP_RETURN_CODE).
#pragma once

#include <cstdio>
#include <sstream>

namespace check {

inline int checks = 0;
inline int failures = 0;

template <typename A, typename B>
void eq(const A& a, const B& b, const char* expr, const char* file, int line) {
	checks++;
	if(a == b) return;
	failures++;
	std::ostringstream os;
	os << file << ":" << line << ": CHECK_EQ failed: " << expr << "\n"
	   << "  actual:   " << a << "\n"
	   << "  expected: " << b << "\n";
	std::fputs(os.str().c_str(), stderr);
}

inline void is_true(bool v, const char* expr, const char* file, int line) {
	checks++;
	if(v) return;
	failures++;
	std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
}

inline int report(const char* name) {
	std::fprintf(stderr, "%s: %d checks, %d failure(s)\n", name, checks, failures);
	return failures == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK_EQ(a, b) ::check::eq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK(x) ::check::is_true(static_cast<bool>(x), #x, __FILE__, __LINE__)
#define CHECK_REPORT() return ::check::report(__FILE__)
