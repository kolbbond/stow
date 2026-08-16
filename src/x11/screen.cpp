#include "stow/screen.hpp"

#include "x11/monitor.hpp"

#include <X11/Xlib.h>

namespace stow {
namespace {

// One process-wide display connection for queries. Opening a display per call
// exhausts file descriptors under a per-frame caller like the cursor demo.
// Nothing here owns a window; the OS closes it at exit.
Display* query_display() {
	static Display* dpy = XOpenDisplay(nullptr);
	return dpy;
}

}  // namespace

Point pointer() {
	Display* dpy = query_display();
	if(!dpy) return {};

	Window root = DefaultRootWindow(dpy);
	Window ret_root = 0, ret_child = 0;
	int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
	unsigned int mask = 0;

	if(!XQueryPointer(dpy, root, &ret_root, &ret_child, &root_x, &root_y, &win_x, &win_y, &mask)) return {};
	return Point{root_x, root_y};
}

std::vector<Monitor> monitors() {
	Display* dpy = query_display();
	if(!dpy) return {};

	auto mgr = MonitorManager::create(dpy);
	std::vector<Monitor> out = mgr->monitors();

	if(out.empty()) {
		Monitor m;
		m.index = 0;
		m.name = "screen";
		m.width = DisplayWidth(dpy, DefaultScreen(dpy));
		m.height = DisplayHeight(dpy, DefaultScreen(dpy));
		m.primary = true;
		out.push_back(m);
	}

	// Guarantee the ordering and primary invariants the API promises: dense
	// indices, and exactly one primary.
	for(size_t i = 0; i < out.size(); i++) out[i].index = int(i);

	bool seen_primary = false;
	for(Monitor& m : out) {
		if(m.primary && !seen_primary) {
			seen_primary = true;
		} else {
			m.primary = false;
		}
	}
	if(!seen_primary) out[0].primary = true;

	return out;
}

Monitor primary_monitor() {
	std::vector<Monitor> mons = monitors();
	if(mons.empty()) return {};
	for(const Monitor& m : mons)
		if(m.primary) return m;
	return mons[0];
}

Size screen_size() {
	Display* dpy = query_display();
	if(!dpy) return {};
	int s = DefaultScreen(dpy);
	return Size{static_cast<unsigned int>(DisplayWidth(dpy, s)), static_cast<unsigned int>(DisplayHeight(dpy, s))};
}

}  // namespace stow
