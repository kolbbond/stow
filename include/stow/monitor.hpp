// stow::Monitor - one display's geometry. Pure value type; no X11.
// The X11 detection that fills these in (MonitorManager) is private, under
// src/x11/monitor.hpp. Consumers query monitors via <stow/screen.hpp>.
#pragma once

#include <string>

namespace stow {

struct Monitor {
	int index = 0;
	std::string name;
	int x = 0;
	int y = 0;
	unsigned int width = 0;
	unsigned int height = 0;
	bool primary = false;

	// Check if a point is within this monitor
	bool contains(int px, int py) const {
		return px >= x && px < x + static_cast<int>(width) && py >= y && py < y + static_cast<int>(height);
	}
};

}  // namespace stow
