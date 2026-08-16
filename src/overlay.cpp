#include "stow/overlay.hpp"

#include "stow/screen.hpp"

#include "x11/window.hpp"

#include <X11/extensions/shape.h>

#include <poll.h>

#include <cstdio>

namespace stow {

struct Overlay::Impl {
	std::shared_ptr<XWindow> win;
	OverlayConfig cfg;
	Caps caps;
	bool shown = false;
	bool closed = false;
};

namespace {

// X error handling is global process state. The library owns it so a consumer
// does not have to know that; previously every binary installed its own.
int x_error_handler(Display* dpy, XErrorEvent* ev) {
	char buf[256];
	XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
	std::fprintf(stderr, "stow: X error: %s (opcode=%d)\n", buf, int(ev->request_code));
	return 0;
}

void install_error_handler_once() {
	static const bool installed = [] {
		XSetErrorHandler(x_error_handler);
		return true;
	}();
	(void)installed;
}

// Map the public config onto the WindowConfig XWindow already understands.
WindowConfig to_window_config(const OverlayConfig& c) {
	WindowConfig w;
	w.anchor = c.anchor;
	w.px = c.x;
	w.py = c.y;
	w.monitor = c.monitor;
	w.font = c.font;
	w.alpha = c.alpha;
	w.align = c.align;
	w.border_px = c.border_px;
	w.borderless = c.borderless;
	w.overlay = c.clickthrough;
	w.on_top = c.on_top;
	w.title = c.title;

	// stow::Color -> the "#rrggbb" strings XftColorAllocName wants.
	char buf[16];
	std::snprintf(buf, sizeof(buf), "#%06x", c.fg.rgb());
	w.fg = buf;
	std::snprintf(buf, sizeof(buf), "#%06x", c.bg.rgb());
	w.bg = buf;

	// A non-empty size means fixed geometry; empty means size-to-content.
	if(!c.size.empty()) {
		w.use_fixed_geometry = true;
		w.fixed_w = c.size.w;
		w.fixed_h = c.size.h;
	}
	return w;
}

Caps probe_caps(XWindow& w, const OverlayConfig& cfg) {
	Caps caps;
	caps.transparency = (w._depth == 32);
	caps.override_redirect = w._override_redirect;

	int event_base = 0, error_base = 0;
	caps.clickthrough = cfg.clickthrough && XShapeQueryExtension(w._dpy, &event_base, &error_base);
	return caps;
}

}  // namespace

std::optional<Overlay> Overlay::create(const OverlayConfig& cfg, Error* err) {
	if(err) *err = Error{};
	install_error_handler_once();

	// XWindow::setup() calls Error::die() on failure, which exits the process.
	// Probe the display first so a headless caller gets an error back instead
	// of an exit. This is the one failure mode the library reports.
	Display* probe = XOpenDisplay(nullptr);
	if(!probe) {
		if(err) *err = Error{Error::Code::NoDisplay, "cannot open X display"};
		return std::nullopt;
	}
	XCloseDisplay(probe);

	auto impl = std::make_unique<Impl>();
	impl->cfg = cfg;
	WindowConfig wcfg = to_window_config(cfg);
	impl->win = XWindow::create(wcfg);
	impl->win->setup();
	impl->caps = probe_caps(*impl->win, cfg);

	// An explicit size means fixed geometry, which makes XWindow place the
	// window at _fixed_x/_fixed_y instead of resolving the anchor. Resolve it
	// once here, now that the size is known, so anchor and size compose
	// instead of the anchor being silently ignored. It also gives geometry()
	// a real answer before the first draw.
	if(!cfg.size.empty()) {
		Monitor mon = primary_monitor();
		if(cfg.monitor >= 0) {
			std::vector<Monitor> all = monitors();
			if(cfg.monitor < static_cast<int>(all.size())) mon = all[cfg.monitor];
		}
		int x = 0, y = 0;
		wcfg.resolve_anchor(static_cast<int>(mon.width), static_cast<int>(mon.height), static_cast<int>(cfg.size.w),
			static_cast<int>(cfg.size.h), x, y);
		impl->win->set_geometry(mon.x + x, mon.y + y, cfg.size.w, cfg.size.h);
	}

	return Overlay(std::move(impl));
}

Overlay::Overlay(const OverlayConfig& cfg) {
	Error err;
	std::optional<Overlay> ov = create(cfg, &err);
	if(!ov) throw err;
	_p = std::move(ov->_p);
}

Overlay::Overlay(std::unique_ptr<Impl> p) : _p(std::move(p)) {}
Overlay::Overlay(Overlay&&) noexcept = default;
Overlay& Overlay::operator=(Overlay&&) noexcept = default;
Overlay::~Overlay() = default;

void Overlay::show() {
	_p->shown = true;
	_p->win->_hidden = false;
	_p->win->_dirty = true;
}

void Overlay::hide() {
	_p->shown = false;
	_p->win->_hidden = true;
	_p->win->run();  // unmaps
}

void Overlay::close() {
	_p->shown = false;
	_p->closed = true;
}

bool Overlay::open() const { return _p->shown && !_p->closed; }

Caps Overlay::caps() const { return _p->caps; }

unsigned long Overlay::debug_solid_pixel(Color c) const { return _p->win->solid_pixel(c.rgb()); }

void Overlay::set_clickthrough(bool enabled) {
	if(!_p->caps.clickthrough && enabled) return;  // never granted; nothing to turn on
	_p->win->set_clickthrough(enabled);
}

void Overlay::set_text(std::string_view text) {
	if(_p->closed) return;
	_p->win->draw(std::string(text));
	_p->win->_dirty = true;
}

void Overlay::set_spans(const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_spans(lines);
	_p->win->_dirty = true;
}

void Overlay::move_to(int x, int y) {
	Rect g = geometry();
	_p->win->set_geometry(x, y, g.width, g.height);
}

void Overlay::resize(Size s) {
	Rect g = geometry();
	_p->win->set_geometry(g.x, g.y, s.w, s.h);
}

Rect Overlay::geometry() const {
	Rect r;
	r.x = _p->win->_fixed_x;
	r.y = _p->win->_fixed_y;
	r.width = _p->win->_window_width;
	r.height = _p->win->_window_height;
	return r;
}

void Overlay::begin() {
	if(_p->closed) return;
	_p->win->begin_frame();
}

void Overlay::rect(Rect r, Color c, int thickness) {
	if(_p->closed) return;
	if(thickness <= 0)
		_p->win->fill_rect(r.x, r.y, r.width, r.height, c.rgb());
	else
		_p->win->draw_rect_outline(r.x, r.y, r.width, r.height, c.rgb(), thickness);
}

void Overlay::text(int x, int y, std::string_view s, Color c) {
	if(_p->closed) return;
	_p->win->draw_text_at(x, y, std::string(s), c.rgb());
}

void Overlay::spans(const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_spans(lines);
}

void Overlay::end() {
	if(_p->closed) return;
	_p->win->_dirty = true;
}

void Overlay::text_in(Rect region, std::string_view s) {
	if(_p->closed) return;
	_p->win->draw_region(std::string(s), region.x, region.y, region.width, region.height);
}

void Overlay::spans_in(Rect region, const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_region_spans(lines, region.x, region.y, region.width, region.height);
}

bool Overlay::pump() {
	if(_p->closed) return false;

	// Drain pending X events without blocking.
	while(XPending(_p->win->_dpy)) {
		XEvent ev;
		XNextEvent(_p->win->_dpy, &ev);
		if(ev.type == Expose) _p->win->_dirty = true;
	}

	if(_p->shown) _p->win->run();
	return true;
}

bool Overlay::pump(std::chrono::milliseconds timeout) {
	if(_p->closed) return false;

	// Block on the X connection until an event arrives or the timeout expires.
	// This is what gives a caller a frame rate without a busy-spin.
	if(!XPending(_p->win->_dpy)) {
		struct pollfd pfd = {_p->win->_xfd, POLLIN, 0};
		::poll(&pfd, 1, static_cast<int>(timeout.count()));
	}
	return pump();
}

void Overlay::run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb) {
	while(!_p->closed) {
		if(cb) cb(*this);
		if(_p->closed) break;
		if(!pump(period)) break;
	}
}

}  // namespace stow
