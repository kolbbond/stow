// Screen queries: pointer position and monitor geometry.
// These are facts about the display, not properties of a window, so they are
// free functions rather than Overlay methods. No X11 in this header.
#pragma once

#include <vector>

#include "stow/monitor.hpp"
#include "stow/overlay.hpp"  // Size

namespace stow {

struct Point {
	int x = 0, y = 0;
	constexpr bool operator==(const Point&) const = default;
};

// Global cursor position, in root-window coordinates.
Point pointer();

// All monitors, index-ordered. Never empty on a working display: falls back to
// a single monitor covering the whole screen when neither XRandR nor Xinerama
// is available. Exactly one entry is flagged primary.
std::vector<Monitor> monitors();

// The monitor flagged primary, or index 0 if none is.
Monitor primary_monitor();

// Total desktop size across all monitors.
Size screen_size();

}  // namespace stow
