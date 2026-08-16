// The stow binary's core loop: run a command, render its output, repeat.
// Needs $DISPLAY; skips (77) without one.
#include "stow/overlay.hpp"
#include "proc/ptyprocess.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <chrono>

int main() {
	SKIP_IF_NO_DISPLAY();

	stow::OverlayConfig cfg;
	cfg.x = stow::Position(10);
	cfg.y = stow::Position(10);
	cfg.clickthrough = true;
	cfg.font = "monospace:size=10";

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(cfg, &err);
	CHECK(ov.has_value());
	if(!ov) {
		std::fprintf(stderr, "create failed: %s\n", err.message.c_str());
		CHECK_REPORT();
	}
	ov->show();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
	int iterations = 0;

	while(std::chrono::steady_clock::now() < deadline) {
		auto pty = PTYProcess::create();
		pty->setup();
		pty->start_cmd("date", {});
		pty->read_text(*ov);
		iterations++;
	}

	std::fprintf(stderr, "%d command iterations in 3s\n", iterations);

	// The loop actually ran the command and kept the overlay alive.
	CHECK(iterations > 0);
	CHECK(ov->open());

	// Size-to-content: after rendering `date`, the window has a real extent.
	stow::Rect g = ov->geometry();
	CHECK(g.width > 0);
	CHECK(g.height > 0);

	CHECK_REPORT();
}
