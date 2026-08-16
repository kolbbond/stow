// test multi-monitor detection
#include "stow/config.hpp"
#include "skip_if_headless.hpp"
#include "x11/monitor.hpp"
#include "x11/window.hpp"

#include <iostream>

int main() {
	SKIP_IF_NO_DISPLAY();

	// Create a basic window to get Display*
	stow::WindowConfig cfg;
	cfg.overlay = true;

	ShXWindowPr xwin = XWindow::create(cfg);
	xwin->setup();

	// Create monitor manager
	auto monitors = stow::MonitorManager::create(xwin->_dpy);

	std::cout << "Detection method: " << monitors->detection_method() << "\n";
	std::cout << "Number of monitors: " << monitors->monitors().size() << "\n";
	std::cout << "Has multiple monitors: " << (monitors->has_multiple_monitors() ? "yes" : "no") << "\n\n";

	// Print each monitor's info
	for (const auto& mon : monitors->monitors()) {
		std::cout << "Monitor " << mon.index << ":\n";
		std::cout << "  Name: " << mon.name << "\n";
		std::cout << "  Position: " << mon.x << ", " << mon.y << "\n";
		std::cout << "  Size: " << mon.width << "x" << mon.height << "\n";
		std::cout << "  Primary: " << (mon.primary ? "yes" : "no") << "\n\n";
	}

	// Test primary monitor lookup
	const stow::Monitor* primary = monitors->primary();
	if (primary) {
		std::cout << "Primary monitor is: " << primary->name << " (#" << primary->index << ")\n";
	}

	// Test monitor at index
	const stow::Monitor* mon0 = monitors->at(0);
	if (mon0) {
		std::cout << "Monitor at index 0: " << mon0->name << "\n";
	}

	// Test monitor at point (center of screen)
	unsigned int total_w, total_h;
	monitors->total_size(total_w, total_h);
	std::cout << "Total screen size: " << total_w << "x" << total_h << "\n";

	const stow::Monitor* center_mon = monitors->at_point(total_w / 2, total_h / 2);
	if (center_mon) {
		std::cout << "Monitor at center: " << center_mon->name << "\n";
	}

	std::cout << "\nMonitor test completed successfully.\n";
	return 0;
}
