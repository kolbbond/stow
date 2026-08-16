// Tests that need a real X display use this as the first statement in main().
// Returning 77 tells CTest the test was skipped rather than failed, so a
// headless box (CI, ssh without forwarding) stays green instead of going red
// for a reason that has nothing to do with the code.
// Pair it with: set_tests_properties(<name> PROPERTIES SKIP_RETURN_CODE 77)
#pragma once

#include <cstdio>
#include <cstdlib>

#define SKIP_IF_NO_DISPLAY()                                    \
	do {                                                        \
		if(!std::getenv("DISPLAY")) {                           \
			std::fprintf(stderr, "no $DISPLAY; skipping\n");    \
			return 77;                                          \
		}                                                       \
	} while(0)
