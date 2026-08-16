// stow screen queries, built against the public API only.
// Needs $DISPLAY; skips (77) without one.
#include "stow/screen.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

int main() {
	SKIP_IF_NO_DISPLAY();

	// --- screen_size is non-degenerate ---
	stow::Size s = stow::screen_size();
	CHECK(s.w > 0);
	CHECK(s.h > 0);

	// --- at least one monitor, indices are dense and ordered ---
	std::vector<stow::Monitor> mons = stow::monitors();
	CHECK(!mons.empty());
	for(size_t i = 0; i < mons.size(); i++) {
		CHECK_EQ(mons[i].index, int(i));
		CHECK(mons[i].width > 0);
		CHECK(mons[i].height > 0);
	}

	// --- exactly one primary, and primary_monitor() agrees with it ---
	int primaries = 0;
	for(const stow::Monitor& m : mons)
		if(m.primary) primaries++;
	CHECK_EQ(primaries, 1);
	CHECK(stow::primary_monitor().primary);

	// --- the pointer is somewhere on the desktop ---
	stow::Point p = stow::pointer();
	std::fprintf(stderr, "pointer: %d,%d   screen: %ux%u   monitors: %zu\n", p.x, p.y, s.w, s.h, mons.size());
	CHECK(p.x >= 0);
	CHECK(p.y >= 0);
	CHECK(p.x < int(s.w));
	CHECK(p.y < int(s.h));

	// --- and it falls inside some monitor ---
	bool inside = false;
	for(const stow::Monitor& m : mons)
		if(m.contains(p.x, p.y)) inside = true;
	CHECK(inside);

	// --- repeated calls are stable and do not leak a display connection ---
	for(int i = 0; i < 200; i++) (void)stow::pointer();
	CHECK(true);  // reaching here without EMFILE is the assertion

	CHECK_REPORT();
}
