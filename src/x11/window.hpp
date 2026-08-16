// X11 overlay window with transparency and click-through support
#pragma once

#include "platform.hpp"

#if STOW_POSIX

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "stow/config.hpp"
#include "legacy_error.hpp"
#include "x11/stow_window.hpp"

typedef std::shared_ptr<class XWindow> ShXWindowPr;

class XWindow : public StowWindow, public std::enable_shared_from_this<XWindow> {
public:
	ShXWindowPr shared_self() {
		return std::static_pointer_cast<XWindow>(shared_from_this());
	}

	// Configuration
	stow::WindowConfig _config;

	// X11 resources
	Display* _dpy = nullptr;
	Window _win = 0;
	Window _root = 0;
	Drawable _drawable = 0;
	XftDraw* _xdraw = nullptr;
	XftFont* _xfont = nullptr;
	Visual* _visual = nullptr;
	Colormap _colormap = 0;
	GC _xgc = nullptr;
	int _xfd = 0;
	int _screen = 0;
	int _depth = 32;

	// Colors
	XftColor _xforeground = {};
	XftColor _xbackground = {};
	std::unordered_map<unsigned int, XftColor> _color_cache;

	// Monitor offset (for multi-monitor support)
	int _monitor_x = 0;
	int _monitor_y = 0;
	unsigned int _monitor_width = 0;
	unsigned int _monitor_height = 0;

	static ShXWindowPr create() { return std::make_shared<XWindow>(); }

	static ShXWindowPr create(const stow::WindowConfig& config) {
		auto win = std::make_shared<XWindow>();
		win->set_config(config);
		return win;
	}

	void set_config(const stow::WindowConfig& config) {
		_config = config;
		_overlay = config.overlay;
		_transparent_background = config.overlay;
		_override_redirect = config.overlay;
		_borderless = config.borderless;
		_fullscreen = config.fullscreen;
		_use_fixed_geometry = config.use_fixed_geometry;
		_fixed_x = config.fixed_x;
		_fixed_y = config.fixed_y;
		_fixed_w = config.fixed_w;
		_fixed_h = config.fixed_h;
	}

	// Configure as transparent click-through overlay (call before setup)
	void set_overlay_mode(bool enabled) {
		_overlay = enabled;
		_transparent_background = enabled;
		_override_redirect = enabled;
		_config.overlay = enabled;
	}

	// Configure as normal window that receives input (call before setup)
	void set_normal_mode() {
		_overlay = false;
		_transparent_background = false;
		_override_redirect = false;
		_config.overlay = false;
	}

	// Toggle click-through at runtime (call after setup)
	void set_clickthrough(bool enabled) {
		_overlay = enabled;
		if(!_dpy || !_win) return;
		if(enabled) {
			// Empty input shape = all clicks pass through
			XShapeCombineRectangles(_dpy, _win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
		} else {
			// Full window input region = window receives clicks
			XRectangle rect = { 0, 0, static_cast<unsigned short>(_window_width), static_cast<unsigned short>(_window_height) };
			XShapeCombineRectangles(_dpy, _win, ShapeInput, 0, 0, &rect, 1, ShapeSet, Unsorted);
			XSelectInput(_dpy, _win, ExposureMask | ButtonPressMask | KeyPressMask);
		}
		XSync(_dpy, False);
	}

	// Set anchor position
	void set_anchor(stow::Anchor anchor, int margin_x = 10, int margin_y = 10) {
		_config.anchor = anchor;
		_config.margin_x = margin_x;
		_config.margin_y = margin_y;
	}

	// Set monitor index (-1 for primary)
	void set_monitor(int index) { _config.monitor = index; }

	// Set monitor geometry (called by MonitorManager)
	void set_monitor_geometry(int x, int y, unsigned int w, unsigned int h) {
		_monitor_x = x;
		_monitor_y = y;
		_monitor_width = w;
		_monitor_height = h;
	}

	~XWindow() {
		for(auto& pair : _color_cache) { XftColorFree(_dpy, _visual, _colormap, &pair.second); }
		XftColorFree(_dpy, _visual, _colormap, &_xforeground);
		XftColorFree(_dpy, _visual, _colormap, &_xbackground);
		if(_xfont) XftFontClose(_dpy, _xfont);
		if(_xdraw) XftDrawDestroy(_xdraw);
		if(_xgc) XFreeGC(_dpy, _xgc);
		if(_drawable) XFreePixmap(_dpy, _drawable);
		if(_win) XDestroyWindow(_dpy, _win);
		if(_colormap) XFreeColormap(_dpy, _colormap);
		if(_dpy) XCloseDisplay(_dpy);
	}

	void setup() override {
		_dpy = XOpenDisplay(nullptr);
		if(!_dpy) { Error::die("cannot open display"); }

		_xfd = ConnectionNumber(_dpy);
		_screen = DefaultScreen(_dpy);
		_root = RootWindow(_dpy, _screen);

		_screen_width = DisplayWidth(_dpy, _screen);
		_screen_height = DisplayHeight(_dpy, _screen);

		// Initialize monitor geometry to full screen if not set
		if(_monitor_width == 0 || _monitor_height == 0) {
			_monitor_width = _screen_width;
			_monitor_height = _screen_height;
		}

		// Use 32-bit ARGB visual for transparency, or default visual for opaque windows
		XVisualInfo vi = {};
		if(_transparent_background && XMatchVisualInfo(_dpy, _screen, 32, TrueColor, &vi)) {
			_depth = 32;
			_visual = vi.visual;
		} else {
			_depth = DefaultDepth(_dpy, _screen);
			_visual = DefaultVisual(_dpy, _screen);
		}

		_colormap = XCreateColormap(_dpy, _root, _visual, AllocNone);
		_drawable = XCreatePixmap(_dpy, _root, 1, 1, _depth);
		_xdraw = XftDrawCreate(_dpy, _drawable, _visual, _colormap);

		_xfont = XftFontOpenName(_dpy, _screen, _config.font.c_str());
		if(!_xfont) { Error::die("cannot load font"); }

		if(!XftColorAllocName(_dpy, _visual, _colormap, _config.fg.c_str(), &_xforeground)) {
			Error::die("cannot allocate foreground color");
		}
		if(!XftColorAllocName(_dpy, _visual, _colormap, _config.bg.c_str(), &_xbackground)) {
			Error::die("cannot allocate background color");
		}

		// Premultiply alpha for transparent backgrounds on 32-bit visuals
		if(_transparent_background && _depth == 32) {
			_xbackground.pixel &= 0x00FFFFFF;
			unsigned char r = ((_xbackground.pixel >> 16) & 0xff) * _config.alpha;
			unsigned char g = ((_xbackground.pixel >> 8) & 0xff) * _config.alpha;
			unsigned char b = (_xbackground.pixel & 0xff) * _config.alpha;
			_xbackground.pixel = (r << 16) | (g << 8) | b;
			_xbackground.pixel |= static_cast<unsigned long>(0xff * _config.alpha) << 24;
		}

		XSetWindowAttributes swa = {};
		swa.override_redirect = _override_redirect ? True : False;
		swa.background_pixel = _xbackground.pixel;
		swa.border_pixel = _xbackground.pixel;
		swa.colormap = _colormap;
		swa.event_mask = ExposureMask | ButtonPressMask;

		_win = XCreateWindow(_dpy,
			_root,
			-1,
			-1,
			1,
			1,
			0,
			_depth,
			InputOutput,
			_visual,
			CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask | CWColormap,
			&swa);

		// Window opacity hint for compositing WMs
		if(_config.alpha < 1.0) {
			unsigned long opacity = static_cast<unsigned long>(0xFFFFFFFFu * _config.alpha);
			Atom opacity_atom = XInternAtom(_dpy, "_NET_WM_WINDOW_OPACITY", False);
			XChangeProperty(_dpy, _win, opacity_atom, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&opacity), 1);
		}

		// Click-through: set empty input shape so all clicks pass through
		if(_overlay) {
			XShapeCombineRectangles(_dpy, _win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
		}

		XGCValues gcvalues = {};
		gcvalues.graphics_exposures = False;
		_xgc = XCreateGC(_dpy, _drawable, GCGraphicsExposures, &gcvalues);

		XSelectInput(_dpy, _win, swa.event_mask);

		// Set window title and class after XFixes setup
		XStoreName(_dpy, _win, _config.title.c_str());
		XClassHint class_hint = {};
		class_hint.res_name = const_cast<char*>(_config.title.c_str());
		class_hint.res_class = const_cast<char*>(_config.title.c_str());
		XSetClassHint(_dpy, _win, &class_hint);
	}

	void draw(const std::string& text) override {
		int borderpx = (_borderless || _config.borderless) ? 0 : _config.border_px;
		unsigned int prev_w = _window_width;
		unsigned int prev_h = _window_height;

		// Measure text dimensions
		_window_width = 0;
		_window_height = 0;
		std::istringstream stream(text);
		std::string line;
		while(std::getline(stream, line)) {
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, reinterpret_cast<const unsigned char*>(line.c_str()), line.size(), &ex);
			if(static_cast<unsigned int>(ex.xOff) > _window_width) _window_width = ex.xOff;
			_window_height += _xfont->ascent + _xfont->descent;
		}

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _monitor_width > 0 ? _monitor_width : _screen_width;
			_window_height = _monitor_height > 0 ? _monitor_height : _screen_height;
		} else {
			_hidden = (_window_width == 0 || _window_height == 0);
			if(_hidden) return;
			_window_width += borderpx * 2;
			_window_height += borderpx * 2;
		}

		resize_drawable_if_needed(prev_w, prev_h);
		clear_drawable();

		// Render text
		stream.clear();
		stream.seekg(0, std::ios::beg);
		unsigned int y = borderpx;
		while(std::getline(stream, line)) {
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, reinterpret_cast<const unsigned char*>(line.c_str()), line.size(), &ex);

			unsigned int x = borderpx;
			if(_config.align == 'r') {
				x = _window_width - ex.xOff;
			} else if(_config.align == 'c') {
				x = (_window_width - ex.xOff) / 2;
			}

			XftDrawStringUtf8(
				_xdraw, &_xforeground, _xfont, x, y + _xfont->ascent, reinterpret_cast<const unsigned char*>(line.c_str()), line.size());
			y += _xfont->ascent + _xfont->descent;
		}
	}

	void draw_region(const std::string& text, int rx, int ry, unsigned int rw, unsigned int rh) override {
		int borderpx = (_borderless || _config.borderless) ? 0 : _config.border_px;
		unsigned int prev_w = _window_width;
		unsigned int prev_h = _window_height;

		update_window_size(rw, rh);
		resize_drawable_if_needed(prev_w, prev_h);
		set_clip_region(rx, ry, rw, rh);
		clear_region(rx, ry, rw, rh);

		std::istringstream stream(text);
		std::string line;
		unsigned int y = ry + borderpx;
		while(std::getline(stream, line)) {
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, reinterpret_cast<const unsigned char*>(line.c_str()), line.size(), &ex);

			unsigned int x = rx + borderpx;
			if(_config.align == 'r' && ex.xOff < static_cast<int>(rw)) {
				x = rx + (rw - ex.xOff);
			} else if(_config.align == 'c' && ex.xOff < static_cast<int>(rw)) {
				x = rx + (rw - ex.xOff) / 2;
			}

			if(y + _xfont->ascent + _xfont->descent > ry + rh) break;
			XftDrawStringUtf8(
				_xdraw, &_xforeground, _xfont, x, y + _xfont->ascent, reinterpret_cast<const unsigned char*>(line.c_str()), line.size());
			y += _xfont->ascent + _xfont->descent;
		}

		clear_clip();
	}

	XftColor* get_color(unsigned int rgb) {
		auto it = _color_cache.find(rgb);
		if(it != _color_cache.end()) { return &it->second; }
		XRenderColor rc;
		rc.red = ((rgb >> 16) & 0xff) * 257;
		rc.green = ((rgb >> 8) & 0xff) * 257;
		rc.blue = (rgb & 0xff) * 257;
		rc.alpha = 0xffff;
		XftColor color;
		if(!XftColorAllocValue(_dpy, _visual, _colormap, &rc, &color)) { return &_xforeground; }
		return &_color_cache.emplace(rgb, color).first->second;
	}

	void draw_spans(const std::vector<std::vector<ColorSpan>>& lines) override {
		int borderpx = (_borderless || _config.borderless) ? 0 : _config.border_px;
		unsigned int rw = 0;
		unsigned int rh = 0;
		for(const auto& line : lines) {
			int line_width = measure_line_width(line);
			if(static_cast<unsigned int>(line_width) > rw) rw = line_width;
			rh += _xfont->ascent + _xfont->descent;
		}
		rw += borderpx * 2;
		rh += borderpx * 2;
		draw_region_spans(lines, 0, 0, rw, rh);
	}

	void draw_region_spans(const std::vector<std::vector<ColorSpan>>& lines, int rx, int ry, unsigned int rw, unsigned int rh) override {
		int borderpx = (_borderless || _config.borderless) ? 0 : _config.border_px;
		unsigned int prev_w = _window_width;
		unsigned int prev_h = _window_height;

		update_window_size(rw, rh);
		resize_drawable_if_needed(prev_w, prev_h);
		set_clip_region(rx, ry, rw, rh);
		clear_region(rx, ry, rw, rh);

		unsigned int y = ry + borderpx;
		for(const auto& spans : lines) {
			int line_width = measure_line_width(spans);

			unsigned int x = rx + borderpx;
			if(_config.align == 'r' && line_width < static_cast<int>(rw)) {
				x = rx + (rw - line_width);
			} else if(_config.align == 'c' && line_width < static_cast<int>(rw)) {
				x = rx + (rw - line_width) / 2;
			}

			for(const ColorSpan& sp : spans) {
				if(sp.text.empty()) continue;
				XftColor* c = get_color(sp.rgb);
				XftDrawStringUtf8(
					_xdraw, c, _xfont, x, y + _xfont->ascent, reinterpret_cast<const unsigned char*>(sp.text.c_str()), sp.text.size());
				XGlyphInfo ex;
				XftTextExtentsUtf8(_dpy, _xfont, reinterpret_cast<const unsigned char*>(sp.text.c_str()), sp.text.size(), &ex);
				x += ex.xOff;
			}

			y += _xfont->ascent + _xfont->descent;
			if(y > ry + rh) break;
		}

		clear_clip();
	}

	void run() override {
		if(_hidden) {
			XUnmapWindow(_dpy, _win);
			XSync(_dpy, False);
			return;
		}

		if(!_dirty) return;

		if(_config.on_top) {
			XRaiseWindow(_dpy, _win);
		} else {
			XLowerWindow(_dpy, _win);
		}

		XMapWindow(_dpy, _win);

		int x, y;
		if(_use_fixed_geometry) {
			x = _fixed_x;
			y = _fixed_y;
		} else if(_fullscreen) {
			x = _monitor_x;
			y = _monitor_y;
		} else {
			// Use anchor-based or position-based placement
			unsigned int mon_w = _monitor_width > 0 ? _monitor_width : _screen_width;
			unsigned int mon_h = _monitor_height > 0 ? _monitor_height : _screen_height;
			_config.resolve_anchor(mon_w, mon_h, _window_width, _window_height, x, y);
			x += _monitor_x;
			y += _monitor_y;
		}

		XMoveResizeWindow(_dpy, _win, x, y, _window_width, _window_height);

		// Re-apply click-through after resize (XWayland resets ShapeInput on resize)
		if(_overlay) {
			XShapeCombineRectangles(_dpy, _win, ShapeInput, 0, 0, NULL, 0, ShapeSet, Unsorted);
		}

		XCopyArea(_dpy, _drawable, _win, _xgc, 0, 0, _window_width, _window_height, 0, 0);
		XSync(_dpy, False);
	}

	// --- immediate-mode primitives (used by stow::Overlay) ---

	// Clear the backing pixmap to the (possibly transparent) background.
	void begin_frame() { clear_drawable(); }

	void fill_rect(int x, int y, unsigned int w, unsigned int h, unsigned int rgb) {
		XSetForeground(_dpy, _xgc, get_color(rgb)->pixel);
		XFillRectangle(_dpy, _drawable, _xgc, x, y, w, h);
	}

	void draw_rect_outline(int x, int y, unsigned int w, unsigned int h, unsigned int rgb, int thickness) {
		if(thickness < 1) thickness = 1;
		XSetForeground(_dpy, _xgc, get_color(rgb)->pixel);
		for(int i = 0; i < thickness; i++) {
			if(w <= static_cast<unsigned int>(2 * i) || h <= static_cast<unsigned int>(2 * i)) break;
			XDrawRectangle(_dpy, _drawable, _xgc, x + i, y + i, w - 2 * i - 1, h - 2 * i - 1);
		}
	}

	// y is the text baseline, matching XftDrawStringUtf8's convention.
	void draw_text_at(int x, int y, const std::string& s, unsigned int rgb) {
		XftDrawStringUtf8(_xdraw, get_color(rgb), _xfont, x, y, reinterpret_cast<const unsigned char*>(s.c_str()), s.size());
	}

	// Explicit geometry, bypassing content measurement.
	void set_geometry(int x, int y, unsigned int w, unsigned int h) {
		unsigned int prev_w = _window_width;
		unsigned int prev_h = _window_height;
		_use_fixed_geometry = true;
		_fixed_x = x;
		_fixed_y = y;
		_fixed_w = w;
		_fixed_h = h;
		_window_width = w;
		_window_height = h;
		_hidden = false;
		resize_drawable_if_needed(prev_w, prev_h);
		_dirty = true;
	}

private:
	void update_window_size(unsigned int w, unsigned int h) {
		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _monitor_width > 0 ? _monitor_width : _screen_width;
			_window_height = _monitor_height > 0 ? _monitor_height : _screen_height;
		} else {
			_hidden = false;
			_window_width = w;
			_window_height = h;
		}
	}

	void resize_drawable_if_needed(unsigned int prev_w, unsigned int prev_h) {
		if(_window_width != prev_w || _window_height != prev_h) {
			XFreePixmap(_dpy, _drawable);
			_drawable = XCreatePixmap(_dpy, _root, _window_width, _window_height, _depth);
			if(!_drawable) Error::die("cannot allocate drawable");
			XftDrawChange(_xdraw, _drawable);
		}
	}

	void clear_drawable() {
		unsigned long clear_pixel = (_transparent_background && _depth == 32) ? 0 : _xbackground.pixel;
		XSetForeground(_dpy, _xgc, clear_pixel);
		XFillRectangle(_dpy, _drawable, _xgc, 0, 0, _window_width, _window_height);
	}

	void set_clip_region(int x, int y, unsigned int w, unsigned int h) {
		XRectangle rect = { static_cast<short>(x), static_cast<short>(y), static_cast<unsigned short>(w), static_cast<unsigned short>(h) };
		XSetClipRectangles(_dpy, _xgc, 0, 0, &rect, 1, Unsorted);
		XftDrawSetClipRectangles(_xdraw, 0, 0, &rect, 1);
	}

	void clear_region(int x, int y, unsigned int w, unsigned int h) {
		unsigned long clear_pixel = (_transparent_background && _depth == 32) ? 0 : _xbackground.pixel;
		XSetForeground(_dpy, _xgc, clear_pixel);
		XFillRectangle(_dpy, _drawable, _xgc, x, y, w, h);
	}

	void clear_clip() {
		XSetClipMask(_dpy, _xgc, None);
		XftDrawSetClip(_xdraw, nullptr);
	}

	int measure_line_width(const std::vector<ColorSpan>& spans) {
		int width = 0;
		for(const ColorSpan& sp : spans) {
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, reinterpret_cast<const unsigned char*>(sp.text.c_str()), sp.text.size(), &ex);
			width += ex.xOff;
		}
		return width;
	}
};

// Factory implementation for POSIX
inline ShWindowPtr StowWindow::create() {
	return std::static_pointer_cast<StowWindow>(XWindow::create());
}

#endif // STOW_POSIX
