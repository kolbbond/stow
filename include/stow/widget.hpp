// Stow dashboard widget interface
#pragma once

#include <ctime>

#include "layout.hpp"   // stow::Rect

// Forward-declare to avoid pulling X11 into headless consumers/tests.
class XWindow;

namespace stow {

// Per-frame shared state a widget may read (e.g. HUD fields).
struct DashStats {
	double fps = 0.0;
	int mouse_x = 0;
	int mouse_y = 0;
	std::time_t now = 0;
};

// What a widget needs to draw itself this frame.
struct RenderCtx {
	XWindow* win = nullptr;   // target window (shared or per-cell); never null at render time
	Rect region;              // pixel rect within the window to draw into
	const DashStats* stats = nullptr;
};

class Widget {
public:
	virtual ~Widget() = default;
	virtual void update() {}                 // refresh internal state (poll process, sample value)
	virtual void render(RenderCtx& ctx) = 0; // draw into ctx.region
	virtual int fd() const { return -1; }    // pollable fd, or -1 if none
	virtual bool dirty() const { return true; }  // skip redraw when false (optional optimization)
};

}  // namespace stow
