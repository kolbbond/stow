// test fullscreen xwindow - POSIX/X11 only
#include "platform.hpp"

#if STOW_POSIX

#include "xwindow.hpp"

int main() {
	// fullscreen window on WSLg: keep it WM-managed but click-through
	gconf.alpha = 0.2;
	gconf.px = {0, 0, 0};
	gconf.py = {0, 0, 0};
	gconf.tx = {0, 0, 0};
	gconf.ty = {0, 0, 0};
	gconf.borderless = 1;

	ShXWindowPr xwin = XWindow::create();
	xwin->_overlay = true;
	xwin->_override_redirect = false;
	xwin->_transparent_background = true;
	xwin->_fullscreen = true;

	xwin->setup();

	bool done = false;
	int cnt = 0;
	while(!done) {
		std::string text = "fullscreen test " + std::to_string(cnt++) + "\n";
		xwin->draw(text);
		xwin->run();
	}
}

#else // STOW_WINDOWS

#include <iostream>

int main() {
	std::cout << "test_fullscreen is only available on POSIX/X11 platforms\n";
	return 0;
}

#endif // STOW_POSIX
