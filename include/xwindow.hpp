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
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <memory>
#include <iostream>
#include <cstring>

#include "config.h"

#include "error.hpp"

typedef std::shared_ptr<class XWindow> ShXWindowPr;
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
			// handle error

			//die("cannot open display");
			std::cerr << "cannot open display\n";
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

		// looks like this sets our new display to the currently
		// set options
		XVisualInfo vi = {.screen = _screen, .depth = _depth, .c_class = TrueColor};
		XMatchVisualInfo(_dpy, _screen, vi.depth, TrueColor, &vi);
		Visual* visual = vi.visual;

		// creates a new colormap
		Colormap colormap = XCreateColormap(_dpy, _root, visual, None);
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

		// alpha blending
		_xbackground.pixel &= 0x00FFFFFF;
		unsigned char r = ((_xbackground.pixel >> 16) & 0xff) * gconf.alpha;
		unsigned char g = ((_xbackground.pixel >> 8) & 0xff) * gconf.alpha;
		unsigned char b = (_xbackground.pixel & 0xff) * gconf.alpha;
		_xbackground.pixel = (r << 16) + (g << 8) + b;
		_xbackground.pixel |= (unsigned char)(0xff * gconf.alpha) << 24;

		// window attributes
		XSetWindowAttributes swa;
		swa.override_redirect = True;
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

		// create a fixed region to allow passthrough
		if(_overlay) {
			XRectangle rect;
			XserverRegion region = XFixesCreateRegion(_dpy, &rect, 1);
			XFixesSetWindowShapeRegion(_dpy, _win, ShapeInput, 0, 0, region);
			XFixesDestroyRegion(_dpy, region);
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
		printf("--- draw --- \n");
		int len = text.size();

		// draw window
		unsigned int prev_mw = _window_width;
		unsigned int prev_mh = _window_height;

		// find maximum text line width and height (does this not draw?
		// @hey: seems like we do this loop twice, here and below
		_window_width = 0;
		_window_height = 0;

		// copy string to char
		// @hey: move to use string overall

		char* ctext = new char[text.size() + 1];
		std::strcpy(ctext, text.c_str());

		for(char* line = ctext; line < ctext + len; line += strlen(line) + 1) {

			// X library glyph for each character?
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)line, strlen(line), &ex);

			// check we're onscreen
			if(ex.xOff > _window_width) _window_width = ex.xOff;
			_window_height += _xfont->ascent + _xfont->descent;
		}

		// hidden is a zero size _window ...
		_hidden = _window_width == 0 || _window_height == 0;
		if(_hidden) {
			printf("0 size _window = hidden\n");

			// @hey: delete ctext here too?
			return;
		}

		// add border to _window sizes
		_window_width += gconf.borderpx * 2;
		_window_height += gconf.borderpx * 2;

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
		XSetForeground(_dpy, _xgc, _xbackground.pixel);
		XFillRectangle(_dpy, _drawable, _xgc, 0, 0, _window_width, _window_height);

		// render text lines
		unsigned int y = gconf.borderpx;
		for(char* line = ctext; line < ctext + len; line += strlen(line) + 1) {
			// more glyphs ... ?
			XGlyphInfo ex;
			XftTextExtentsUtf8(_dpy, _xfont, (unsigned char*)line, strlen(line), &ex);

			// text alignment
			unsigned int x = gconf.borderpx;
			if(gconf.align == 'r') {
				x = _window_width - ex.xOff;
			} else if(gconf.align == 'c') {
				x = (_window_width - ex.xOff) / 2;
			}

			// X library draw function
			XftDrawStringUtf8(_xdraw, &_xforeground, _xfont, x, y + _xfont->ascent, (unsigned char*)line, strlen(line));
			y += _xfont->ascent + _xfont->descent;
		}

		// dont forget to delete!
		delete[] ctext;
	}

	void run() {
		std::printf("run!\n");

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
			static int x = 1000;
			static int y = 100;
			bool use_config = false;
			if(use_config) {
				int x = pos(gconf.px, _screen_width);
				if(gconf.px.prefix == '-') {
					x = _screen_width + x - _window_width;
				}
				x += pos(gconf.tx, _window_width);

				int y = pos(gconf.py, _screen_height);
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
