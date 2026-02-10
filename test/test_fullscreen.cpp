// test fullscreen xwindow
#include "stow/config.hpp"
#include "xwindow.hpp"

#include <chrono>

int main() {
	// Create fullscreen window config
	stow::WindowConfig cfg;
	cfg.alpha = 0.2;
	cfg.px = stow::Position(0);
	cfg.py = stow::Position(0);
	cfg.borderless = true;
	cfg.fullscreen = true;
	cfg.overlay = true;

	ShXWindowPr xwin = XWindow::create(cfg);
	xwin->setup();

	auto start = std::chrono::steady_clock::now();
	constexpr double timeout_sec = 3.0;

	int cnt = 0;
	while (true) {
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration<double>(now - start).count() >= timeout_sec) break;

		std::string text = "fullscreen test " + std::to_string(cnt++) + "\n";
		xwin->draw(text);
		xwin->run();
	}

	return 0;
}
