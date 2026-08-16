#include "stow/overlay.hpp"

#include "x11/window.hpp"

#include <X11/extensions/shape.h>

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
	impl->win = XWindow::create(to_window_config(cfg));
	impl->win->setup();
	impl->caps = probe_caps(*impl->win, cfg);

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

}  // namespace stow
