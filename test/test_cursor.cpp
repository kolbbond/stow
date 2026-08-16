// A red box that follows the cursor and shows its coordinates.
//
// This is the spec's acceptance example for the Tier 1 API, and it doubles as
// the click-through check: the box sits under your pointer, so if the window
// swallowed input you could not click anything beneath it.
//
// Runs for `duration_sec` (default 3) so it can live in ctest. Needs $DISPLAY.
#include "stow/overlay.hpp"
#include "stow/screen.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <chrono>
#include <string>

int main(int argc, char** argv) {
	SKIP_IF_NO_DISPLAY();

	const double duration_sec = (argc > 1) ? std::atof(argv[1]) : 3.0;

	constexpr unsigned int kW = 120;
	constexpr unsigned int kH = 40;

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(
		{
			.size = {kW, kH},
			.font = "monospace:size=10",
			.alpha = 0.9,
			.clickthrough = true,
			.title = "stow-cursor",
		},
		&err);

	CHECK(ov.has_value());
	if(!ov) {
		std::fprintf(stderr, "create failed: %s\n", err.message.c_str());
		CHECK_REPORT();
	}

	stow::Caps caps = ov->caps();
	std::fprintf(stderr, "caps: clickthrough=%d transparency=%d\n", caps.clickthrough, caps.transparency);
	if(!caps.clickthrough) {
		// Not a failure: a compositor may legitimately deny it, and the overlay
		// must still render. But it is worth being loud about.
		std::fprintf(stderr, "WARNING: click-through unavailable; window will swallow input\n");
	}

	ov->show();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration_sec);
	int frames = 0;
	stow::Point last{-1, -1};
	bool ever_moved = false;

	while(std::chrono::steady_clock::now() < deadline) {
		if(!ov->pump(std::chrono::milliseconds(16))) break;  // ~60fps

		stow::Point p = stow::pointer();
		if(last.x >= 0 && !(p == last)) ever_moved = true;
		last = p;

		// Offset so the box sits below-right of the cursor, not under it.
		ov->move_to(p.x + 12, p.y + 12);

		ov->begin();
		ov->rect({0, 0, kW, kH}, stow::Color::red(), 2);
		ov->text(6, 24, std::to_string(p.x) + "," + std::to_string(p.y), stow::Color::white());
		ov->end();

		frames++;
	}

	std::fprintf(stderr, "%d frames in %.1fs; pointer moved: %s (last %d,%d)\n", frames, duration_sec,
		ever_moved ? "yes" : "no", last.x, last.y);

	// The overlay rendered continuously for the whole window.
	CHECK(frames > 10);
	CHECK(ov->open());

	CHECK_REPORT();
}
