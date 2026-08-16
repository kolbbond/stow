// Minimal click-through test - reproduces shud's pattern
// Compare with test_fullscreen which works
#include "stow/config.hpp"
#include "skip_if_headless.hpp"
#include "platform.hpp"

#if STOW_POSIX

#include "xwindow.hpp"

#include <X11/keysym.h>
#include <chrono>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <poll.h>

int main(int argc, char** argv) {
	SKIP_IF_NO_DISPLAY();

	// Mode selection: run different patterns to find what breaks
	// 0 = fullscreen (like test_fullscreen - known working)
	// 1 = fixed geometry (like shud)
	// 2 = fixed geometry + XGrabKey
	// 3 = fixed geometry + event loop
	// 4 = fixed geometry + draw_region instead of draw
	// 5 = fixed geometry + poll delay (like shud main loop)
	// 6 = all of the above (full shud pattern)
	int mode = argc > 1 ? std::atoi(argv[1]) : 0;
	std::cout << "mode " << mode << "\n";

	// Query screen size
	Display* tmp = XOpenDisplay(nullptr);
	if(!tmp) { std::cerr << "no display\n"; return 1; }
	int scr = DefaultScreen(tmp);
	unsigned int sw = DisplayWidth(tmp, scr);
	unsigned int sh = DisplayHeight(tmp, scr);
	std::cout << "screen: " << sw << "x" << sh << "\n";
	XCloseDisplay(tmp);

	stow::WindowConfig cfg;
	cfg.overlay = true;
	cfg.borderless = true;
	cfg.alpha = 0.8;

	if(mode == 0) {
		// Fullscreen mode (like test_fullscreen)
		cfg.fullscreen = true;
	} else if(mode == 7) {
		// Slightly smaller than fullscreen (avoid Hyprland fullscreen detection)
		cfg.use_fixed_geometry = true;
		cfg.fixed_x = 1;
		cfg.fixed_y = 1;
		cfg.fixed_w = sw - 2;
		cfg.fixed_h = sh - 2;
	} else {
		// Fixed geometry mode (like shud)
		cfg.use_fixed_geometry = true;
		cfg.fixed_x = 0;
		cfg.fixed_y = 0;
		cfg.fixed_w = sw;
		cfg.fixed_h = sh;
	}

	ShXWindowPr xwin = XWindow::create(cfg);
	xwin->setup();
	std::cout << "overlay: " << (xwin->_overlay ? "yes" : "no") << "\n";
	std::cout << "depth: " << xwin->_depth << "\n";
	std::cout << "window: 0x" << std::hex << xwin->_win << std::dec << "\n";

	Display* dpy = xwin->_dpy;
	Window root = xwin->_root;

	// Mode 2,6: XGrabKey (will fail on Hyprland)
	if(mode == 2 || mode == 6) {
		XSetErrorHandler([](Display*, XErrorEvent* ev) -> int {
			std::cerr << "X error: code=" << static_cast<int>(ev->error_code)
			          << " opcode=" << static_cast<int>(ev->request_code) << "\n";
			return 0;
		});
		KeyCode kc = XKeysymToKeycode(dpy, XK_h);
		if(kc) {
			XGrabKey(dpy, kc, Mod4Mask, root, False, GrabModeAsync, GrabModeAsync);
		}
		XSync(dpy, False);
		std::cout << "grab attempted\n";
	}

	auto start = std::chrono::steady_clock::now();
	constexpr double timeout_sec = 5.0;
	int cnt = 0;

	while(true) {
		auto now = std::chrono::steady_clock::now();
		if(std::chrono::duration<double>(now - start).count() >= timeout_sec) break;

		std::ostringstream text;
		text << "click-through test mode=" << mode << "\n";
		text << "frame: " << cnt++ << "\n";

		// Mode 5,6: poll delay (like shud)
		if(mode == 5 || mode == 6) {
			poll(nullptr, 0, 100);
		}

		// Mode 4,6: draw_region instead of draw
		if(mode == 4 || mode == 6) {
			xwin->draw_region(text.str(), 0, 0, sw, sh);
		} else {
			xwin->draw(text.str());
		}

		xwin->run();

		// Mode 3,6: event loop (like shud)
		if(mode == 3 || mode == 6) {
			while(XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);
			}
		}
	}

	return 0;
}

#else
#include <iostream>
int main() { std::cout << "POSIX only\n"; return 0; }
#endif
