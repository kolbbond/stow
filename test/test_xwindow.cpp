// test xwindow
#include "stow/config.hpp"
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
	// Create window config
	stow::WindowConfig cfg;
	cfg.px = stow::Position(10);
	cfg.py = stow::Position(10);
	cfg.overlay = true;
	cfg.alpha = 0.8;

	// Create window with config
	ShXWindowPr xwin = XWindow::create(cfg);
	xwin->setup();

	// run for limited time (test timeout)
	auto start = std::chrono::steady_clock::now();
	constexpr double timeout_sec = 3.0;

	int cnt = 0;
	int frames = 0;
	double fps = 0.0;
	auto last_fps = std::chrono::steady_clock::now();

	while (true) {
		auto now = std::chrono::steady_clock::now();
		auto total_elapsed = std::chrono::duration<double>(now - start).count();
		if (total_elapsed >= timeout_sec) break;

		frames++;
		auto elapsed = std::chrono::duration<double>(now - last_fps).count();
		if (elapsed >= 1.0) {
			fps = frames / elapsed;
			frames = 0;
			last_fps = now;
		}

		int mouse_x = 0, mouse_y = 0;
		Window root = 0, child = 0;
		int win_x = 0, win_y = 0;
		unsigned int mask = 0;
		if (XQueryPointer(xwin->_dpy, xwin->_root, &root, &child,
				&mouse_x, &mouse_y, &win_x, &win_y, &mask) == False) {
			mouse_x = mouse_y = 0;
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

	return 0;
}
