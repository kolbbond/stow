// test xwindow - POSIX/X11 only
#include "platform.hpp"

#if STOW_POSIX

#include "xwindow.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

std::string teststr =
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vivamus luctus urna sed urna ultricies ac tempor dui \n"
	"sagittis. In condimentum facilisis porta. Sed nec diam eu diam mattis viverra. Nulla fringilla, orci ac euismod\n"
	"semper, massa velit luctus felis, a aliquet magna ante ac quam. Maecenas fermentum consequat mi. Donec "
	"fermentum.\n"
	"Pellentesque tincidunt, sem in lacinia aliquet, mi nisl adipiscing odio, eu gravida dolor metus non quam. \n"
	"Curabitur ultrices ligula ut augue laoreet, nec malesuada enim condimentum. Pellentesque et nulla vel nisi \n"
	"pretium tincidunt ac sit amet eros. Sed malesuada odio augue, id suscipit felis pharetra eget. Mauris ac dui sit\n"
	"amet augue lacinia luctus. Fusce congue nunc sit amet nisl mollis pharetra. Etiam venenatis, lorem sit amet \n"
	"auctor varius, ante nisi laoreet risus, vel fringilla mauris enim sed nulla. Suspendisse potenti.	Nam aliquam \n"
	"hendrerit enim,			sed feugiat sapien posuere non.Ut fermentum dui lectus,			at viverra urna \n"
	"iaculis a.Integer non faucibus justo,			a faucibus nisl.Nam tristique nibh id libero malesuada \n"
	"tempor.Nulla non libero at purus cursus volutpat				.Mauris eleifend mi a lorem dictum lacinia.Etiam \n"
	"ut felis risus.Nullam euismod est in ipsum cursus,			a sodales sapien sodales.Ut sed arcu a justo "
	"faucibus\n"
	"cursus et vel nulla.Phasellus				placerat laoreet mollis.Aenean viverra auctor orci,			in mollis\n"
	"lorem hendrerit a.Suspendisse cursus dolor ligula,			sed lacinia arcu pellentesque et.Duis vitae leo\n"
	"mollis, feugiat turpis a,			ullamcorper arcu.Aliquam erat volutpat.Nam congue, nisi ut sodales suscipit, \n"
	"eros odio tristique leo,AAtae condimentum risus arcu at enim.Nulla facilisi.\0";

static std::string format_time_now() {
	std::time_t now = std::time(nullptr);
	std::tm* local = std::localtime(&now);
	char buf[32];
	if(local && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local)) {
		return std::string(buf);
	}
	return "unknown";
}

int main() {
	// keep the test window on-screen regardless of desktop size
	gconf.px = {10, 0, 0};
	gconf.py = {10, 0, 0};
	gconf.tx = {0, 0, 0};
	gconf.ty = {0, 0, 0};

	// create
	ShXWindowPr xwin = XWindow::create();
	// WSLg/XWayland may not show override-redirect windows reliably
	xwin->_overlay = true; // click-through via empty input region
	xwin->_override_redirect = false; // WM-managed so it stays visible
	xwin->_transparent_background = true;

	// setup window -> what do we need to pass into it
	xwin->setup();

	// draw
	bool done = false;
	int cnt = 0;
	int frames = 0;
	double fps = 0.0;
	auto last_fps = std::chrono::steady_clock::now();
	while(!done) {
		frames++;
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration<double>(now - last_fps).count();
		if(elapsed >= 1.0) {
			fps = frames / elapsed;
			frames = 0;
			last_fps = now;
		}

		int mouse_x = 0;
		int mouse_y = 0;
		Window root = 0;
		Window child = 0;
		int win_x = 0;
		int win_y = 0;
		unsigned int mask = 0;
		if(XQueryPointer(xwin->_dpy, xwin->_root, &root, &child,
			   &mouse_x, &mouse_y, &win_x, &win_y, &mask) == False) {
			mouse_x = 0;
			mouse_y = 0;
		}

		std::ostringstream hud;
		hud << std::fixed << std::setprecision(1);
		hud << "fps: " << fps << "\n";
		hud << "mouse: " << mouse_x << "," << mouse_y << "\n";
		hud << "time: " << format_time_now() << "\n";
		hud << "frame: " << cnt++ << "\n";

		xwin->draw(hud.str());
		xwin->run();
	}
}

#else // STOW_WINDOWS

#include <iostream>

int main() {
	std::cout << "test_xwindow is only available on POSIX/X11 platforms\n";
	return 0;
}

#endif // STOW_POSIX
