// handle xwindow instance
#pragma once

#include <limits.h>
#include <poll.h>
//#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <memory>
#include <iostream>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "config.h"

#include "error.hpp"

typedef std::shared_ptr<class XWindow> ShXWindowPr;
struct ColorSpan {
	std::string text;
	unsigned int rgb;
};
class XWindow {

public:
	// x instance
	// xlib and xft
	Display* _dpy;
	Window _win;
	Window _root;
	Drawable _drawable;
	XftDraw* _xdraw;
	XftColor _xforeground;
	XftColor _xbackground;
	XftFont* _xfont;
	bool _dirty = true;
	Visual* _visual = NULL;
	Colormap _colormap = 0;
	std::unordered_map<unsigned int, XftColor> _color_cache;

	// x connection number
	int _xfd;
	int _screen;
	int _depth = 32;
	GC _xgc;
	unsigned int _screen_width;
	unsigned int _screen_height;
	unsigned int _window_width;
	unsigned int _window_height;
	bool _hidden = true;
	bool _overlay = true;
	bool _override_redirect = true;
	bool _transparent_background = true;
	bool _fullscreen = false;
	bool _borderless = false;
	bool _use_fixed_geometry = false;
	int _fixed_x = 0;
	int _fixed_y = 0;
	unsigned int _fixed_w = 0;
	unsigned int _fixed_h = 0;

	// constructors
	static ShXWindowPr create() {
		return std::make_shared<class XWindow>();
	}


	void setup() {
		std::printf("--- setup XWindow ---\n");

		// xlib and xft
		// opens a new connection to the X server
		_dpy = XOpenDisplay(NULL);
		if(!_dpy) {
			Error::die("cannot open display");
		}

		// get the display connection number to the X server
		_xfd = ConnectionNumber(_dpy);

		// set screen and window
		// can we get the other windows from this?
		// get the default _screen number for our display connection
		// for a single _screen app...
		_screen = DefaultScreen(_dpy);

		// debug get the total number of _screens
		int _screencount = ScreenCount(_dpy);
		//printf("We find %i _screens\n", _screencount);

		// gets the root window for our display connection (_dpy)
		// and the _screen (monitor)
		// returns a Window object
		_root = RootWindow(_dpy, _screen);

		// display size
		_screen_width = DisplayWidth(_dpy, _screen);
		_screen_height = DisplayHeight(_dpy, _screen);

		// choose a visual; fall back to default if ARGB depth is unavailable (common on XWayland/WSLg)
		XVisualInfo vi = {};
		Visual* visual = NULL;
		if(XMatchVisualInfo(_dpy, _screen, _depth, TrueColor, &vi)) {
			visual = vi.visual;
		} else {
			_depth = DefaultDepth(_dpy, _screen);
			visual = DefaultVisual(_dpy, _screen);
		}
		_visual = visual;

		// creates a new colormap
		Colormap colormap = XCreateColormap(_dpy, _root, visual, None);
		_colormap = colormap;
		// dumb 1x1 drawable only to initialize xdraw
		// I respect this comment, love a good hack
		_drawable = XCreatePixmap(_dpy, _root, 1, 1, vi.depth);
		_xdraw = XftDrawCreate(_dpy, _drawable, visual, colormap);
		_xfont = XftFontOpenName(_dpy, _screen, gconf.font.c_str());
		if(!_xfont) {
			Error::die("cannot load font");
		}

		// allocate colors for foreground and background
		// TODO: use dedicated color variables instead of array
		if(!XftColorAllocName(_dpy, visual, colormap, gconf.colors[0].c_str(), &_xforeground)) {
			Error::die("cannot allocate foreground color");
		}
		if(!XftColorAllocName(_dpy, visual, colormap, gconf.colors[1].c_str(), &_xbackground)) {
			Error::die("cannot allocate background color");
		}

		// alpha blending (only valid on 32-bit visuals)
		if(_depth == 32) {
			_xbackground.pixel &= 0x00FFFFFF;
			unsigned char r = ((_xbackground.pixel >> 16) & 0xff) * gconf.alpha;
			unsigned char g = ((_xbackground.pixel >> 8) & 0xff) * gconf.alpha;
			unsigned char b = (_xbackground.pixel & 0xff) * gconf.alpha;
			_xbackground.pixel = (r << 16) + (g << 8) + b;
			_xbackground.pixel |= (unsigned char)(0xff * gconf.alpha) << 24;
		}

		// window attributes
		XSetWindowAttributes swa;
		swa.override_redirect = _override_redirect ? True : False;
		swa.background_pixel = _xbackground.pixel;
		swa.border_pixel = _xbackground.pixel;
		swa.colormap = colormap;
		swa.event_mask = ExposureMask | ButtonPressMask;

		// create our window
		_win = XCreateWindow(_dpy,
			_root,
			-1,
			-1,
			1,
			1,
			0,
			vi.depth,
			InputOutput,
			visual,
			CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask | CWColormap,
			&swa);

		// overall window opacity for compositing WMs (works on WSLg/XWayland)
		if(gconf.alpha < 1.0) {
			unsigned long opacity = (unsigned long)(0xFFFFFFFFu * gconf.alpha);
			Atom opacity_atom = XInternAtom(_dpy, "_NET_WM_WINDOW_OPACITY", False);
			XChangeProperty(_dpy, _win, opacity_atom, XA_CARDINAL, 32, PropModeReplace,
				reinterpret_cast<unsigned char*>(&opacity), 1);
		}

		// create a fixed region to allow passthrough
		if(_overlay) {
			int xfixes_event = 0;
			int xfixes_error = 0;
			if(XFixesQueryExtension(_dpy, &xfixes_event, &xfixes_error)) {
				XserverRegion region = XFixesCreateRegion(_dpy, NULL, 0);
				XFixesSetWindowShapeRegion(_dpy, _win, ShapeInput, 0, 0, region);
				XFixesDestroyRegion(_dpy, region);
			} else {
				_overlay = false;
			}
		}

		// graphics context
		XGCValues gcvalues = {0};
		gcvalues.graphics_exposures = False;
		_xgc = XCreateGC(_dpy, _drawable, GCGraphicsExposures, &gcvalues);

		XSelectInput(_dpy, _win, swa.event_mask);

		return;
	}

	// draw to the screen
	void draw(std::string text) {
		//printf("--- draw --- \n");
		// copy string to char
		int len = text.size(); //@hey: configure the new line?
		char* ctext = new char[text.size() + 1];
		std::strcpy(ctext, text.c_str());

		// draw window
		unsigned int prev_mw = _window_width;
		unsigned int prev_mh = _window_height;
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		// find maximum text line width and height (does this not draw?
		// @hey: seems like we do this loop twice, here and below
		_window_width = 0;
		_window_height = 0;

		// setup stream
		std::istringstream stream(text);
		std::string line;
		while(std::getline(stream, line)) {

			// X library glyph for each character?
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)line.c_str(), line.size(), &ex);

			// check we're onscreen
			if(ex.xOff > _window_width) _window_width = ex.xOff;
			_window_height += _xfont->ascent + _xfont->descent;
		}

		// hidden is a zero size _window ...
		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = _window_width == 0 || _window_height == 0;
			if(_hidden) {
				printf("0 size _window = hidden\n");

				// @hey: delete ctext here too?
				return;
			}

			// add border to _window sizes
			_window_width += borderpx * 2;
			_window_height += borderpx * 2;
		}

		// why would the _window sizes change here? @hey
		if(_window_width != prev_mw || _window_height != prev_mh) {
			// TODO: for some reason old GC value still works after XFreePixmap call
			// creates a new pixmap
			//printf("_window size changes, redraw pixmap\n");
			XFreePixmap(_dpy, _drawable);
			_drawable = XCreatePixmap(_dpy, _root, _window_width, _window_height, _depth);
			if(!_drawable) Error::die("cannot allocate drawable");
			XftDrawChange(_xdraw, _drawable);
		}

		//printf("setting stow foreground\n");
		unsigned long clear_pixel = _xbackground.pixel;
		if(_transparent_background && _depth == 32) {
			clear_pixel = 0x00000000;
		}
		XSetForeground(_dpy, _xgc, clear_pixel);
		XFillRectangle(_dpy, _drawable, _xgc, 0, 0, _window_width, _window_height);

		// render text lines
		stream.clear();
		stream.seekg(0, std::ios::beg);
		unsigned int y = borderpx;
		while(std::getline(stream, line)) {

			// more glyphs ... ?
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)line.c_str(), line.size(), &ex);

			// text alignment
			unsigned int x = borderpx;
			if(gconf.align == 'r') {
				x = _window_width - ex.xOff;
			} else if(gconf.align == 'c') {
				x = (_window_width - ex.xOff) / 2;
			}

			// X library draw function
			XftDrawStringUtf8(
				_xdraw, &_xforeground, _xfont, x, y + _xfont->ascent, (unsigned char*)line.c_str(), line.size());
			y += _xfont->ascent + _xfont->descent;
		}


		// dont forget to delete!
		delete[] ctext;
	}

	// draw text clipped to a region in the window
	void draw_region(const std::string& text, int rx, int ry, unsigned int rw, unsigned int rh) {
		unsigned int prev_mw = _window_width;
		unsigned int prev_mh = _window_height;
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = false;
			_window_width = rw;
			_window_height = rh;
		}

		if(_window_width != prev_mw || _window_height != prev_mh) {
			XFreePixmap(_dpy, _drawable);
			_drawable = XCreatePixmap(_dpy, _root, _window_width, _window_height, _depth);
			if(!_drawable) Error::die("cannot allocate drawable");
			XftDrawChange(_xdraw, _drawable);
		}

		unsigned long clear_pixel = _xbackground.pixel;
		if(_transparent_background && _depth == 32) {
			clear_pixel = 0x00000000;
		}

		XRectangle rect;
		rect.x = rx;
		rect.y = ry;
		rect.width = rw;
		rect.height = rh;
		XSetClipRectangles(_dpy, _xgc, 0, 0, &rect, 1, Unsorted);
		XftDrawSetClipRectangles(_xdraw, 0, 0, &rect, 1);

		XSetForeground(_dpy, _xgc, clear_pixel);
		XFillRectangle(_dpy, _drawable, _xgc, rx, ry, rw, rh);

		std::istringstream stream(text);
		std::string line;
		unsigned int y = ry + borderpx;
		while(std::getline(stream, line)) {
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)line.c_str(), line.size(), &ex);

			unsigned int x = rx + borderpx;
			if(gconf.align == 'r') {
				if(ex.xOff < rw) x = rx + (rw - ex.xOff);
			} else if(gconf.align == 'c') {
				if(ex.xOff < rw) x = rx + (rw - ex.xOff) / 2;
			}

			if(y + _xfont->ascent + _xfont->descent > ry + rh) break;
			XftDrawStringUtf8(_xdraw, &_xforeground, _xfont, x, y + _xfont->ascent,
				(unsigned char*)line.c_str(), line.size());
			y += _xfont->ascent + _xfont->descent;
		}

		XSetClipMask(_dpy, _xgc, None);
		XftDrawSetClip(_xdraw, NULL);
	}

	XftColor* get_color(unsigned int rgb) {
		auto it = _color_cache.find(rgb);
		if(it != _color_cache.end()) {
			return &it->second;
		}
		XRenderColor rc;
		rc.red = ((rgb >> 16) & 0xff) * 257;
		rc.green = ((rgb >> 8) & 0xff) * 257;
		rc.blue = (rgb & 0xff) * 257;
		rc.alpha = 0xffff;
		XftColor color;
		if(!XftColorAllocValue(_dpy, _visual, _colormap, &rc, &color)) {
			return &_xforeground;
		}
		auto res = _color_cache.emplace(rgb, color);
		return &res.first->second;
	}

	void draw_spans(const std::vector<std::vector<ColorSpan>>& lines) {
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;
		unsigned int rw = 0;
		unsigned int rh = 0;
		for(size_t i = 0; i < lines.size(); i++) {
			int line_width = 0;
			for(const ColorSpan& sp : lines[i]) {
				XGlyphInfo ex;
				XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)sp.text.c_str(), sp.text.size(), &ex);
				line_width += ex.xOff;
			}
			if(static_cast<unsigned int>(line_width) > rw) rw = line_width;
			rh += _xfont->ascent + _xfont->descent;
		}
		rw += borderpx * 2;
		rh += borderpx * 2;
		draw_region_spans(lines, 0, 0, rw, rh);
	}

	void draw_region_spans(const std::vector<std::vector<ColorSpan>>& lines, int rx, int ry,
		unsigned int rw, unsigned int rh) {
		unsigned int prev_mw = _window_width;
		unsigned int prev_mh = _window_height;
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = false;
			_window_width = rw;
			_window_height = rh;
		}

		if(_window_width != prev_mw || _window_height != prev_mh) {
			XFreePixmap(_dpy, _drawable);
			_drawable = XCreatePixmap(_dpy, _root, _window_width, _window_height, _depth);
			if(!_drawable) Error::die("cannot allocate drawable");
			XftDrawChange(_xdraw, _drawable);
		}

		unsigned long clear_pixel = _xbackground.pixel;
		if(_transparent_background && _depth == 32) {
			clear_pixel = 0x00000000;
		}

		XRectangle rect;
		rect.x = rx;
		rect.y = ry;
		rect.width = rw;
		rect.height = rh;
		XSetClipRectangles(_dpy, _xgc, 0, 0, &rect, 1, Unsorted);
		XftDrawSetClipRectangles(_xdraw, 0, 0, &rect, 1);

		XSetForeground(_dpy, _xgc, clear_pixel);
		XFillRectangle(_dpy, _drawable, _xgc, rx, ry, rw, rh);

		unsigned int y = ry + borderpx;
		for(size_t i = 0; i < lines.size(); i++) {
			const std::vector<ColorSpan>& spans = lines[i];
			int line_width = 0;
			for(const ColorSpan& sp : spans) {
				XGlyphInfo ex;
				XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)sp.text.c_str(), sp.text.size(), &ex);
				line_width += ex.xOff;
			}

			unsigned int x = rx + borderpx;
			if(gconf.align == 'r') {
				if(line_width < static_cast<int>(rw)) x = rx + (rw - line_width);
			} else if(gconf.align == 'c') {
				if(line_width < static_cast<int>(rw)) x = rx + (rw - line_width) / 2;
			}

			for(const ColorSpan& sp : spans) {
				if(sp.text.empty()) continue;
				XftColor* c = get_color(sp.rgb);
				XftDrawStringUtf8(_xdraw, c, _xfont, x, y + _xfont->ascent,
					(unsigned char*)sp.text.c_str(), sp.text.size());
				XGlyphInfo ex;
				XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)sp.text.c_str(), sp.text.size(), &ex);
				x += ex.xOff;
			}

			y += _xfont->ascent + _xfont->descent;
			if(y > ry + rh) break;
		}

		XSetClipMask(_dpy, _xgc, None);
		XftDrawSetClip(_xdraw, NULL);
	}

	void run() {
		//std::printf("run!\n");

		/*
		// Process X events
		if(fds[1].revents & POLLIN) {
			while(XPending(_dpy)) {
				XEvent ev;
				XNextEvent(_dpy, &ev);

				if(ev.type == Expose && ev.xexpose.count == 0) {
					// Last expose event processed, redraw once
					dirty = true;

				} else if(ev.type == ButtonPress) {
					// X Window was clicked, restart subcommand
					// here's where we see a button press
					// this is where an overlay _window should send
					// the signal to any underlying _windows ...
					// so we need some _window index and position and
					// order from foreground to background
					//printf("ButtonPress registered\n");
					int root_x, root_y;
					unsigned int mask;
					XQueryPointer(_dpy,
						DefaultRootWindow(_dpy),
						&root,
						&root,
						&root_x,
						&root_y,
						&root_x,
						&root_y,
						&mask); //<--four

					if(cmdpid && kill(-cmdpid, SIGTERM) == -1) {
						die("kill:");
					}
					alarm(0);
					restart_now = true;
				}
			}
		}
        */

		// hidden _window
		if(_hidden) {
			XUnmapWindow(_dpy, _win);
			XSync(_dpy, False);

		} else if(_dirty) {
			// set _window position within server
			if(gconf.window_on_top) {
				XRaiseWindow(_dpy, _win);
			} else {
				XLowerWindow(_dpy, _win);
			}

			XMapWindow(_dpy, _win);

			// set __window position
			int x, y;
			bool use_config = true;
			if(_use_fixed_geometry) {
				x = _fixed_x;
				y = _fixed_y;
			} else if(_fullscreen) {
				x = 0;
				y = 0;
			} else if(use_config) {
				x = pos(gconf.px, _screen_width);
				if(gconf.px.prefix == '-') {
					x = _screen_width + x - _window_width;
				}
				x += pos(gconf.tx, _window_width);

				y = pos(gconf.py, _screen_height);
				if(gconf.py.prefix == '-') {
					y = _screen_height + y - _window_height;
				}
				y += pos(gconf.ty, _window_height);
			} else {
			}

			// final move and sync
			XMoveResizeWindow(_dpy, _win, x, y, _window_width, _window_height);
			XCopyArea(_dpy, _drawable, _win, _xgc, 0, 0, _window_width, _window_height, 0, 0);
			XSync(_dpy, False);
		} // dirty option
	}


	//       struct g is what? {prefix,suffix,value}?
	static int pos(struct gs g, int size) {
		//
		int sign = g.prefix == '-' ? -1 : 1;
		switch(g.suffix) {
		case '%':
			return sign * (g.value / 100.0) * size;
		default:
			return sign * g.value;
		}
	}
};
