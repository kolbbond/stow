// test stow functionality - runs a command and exits after timeout
#include "stow/config.hpp"
#include "skip_if_headless.hpp"
#include "platform.hpp"

#if STOW_POSIX
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"
#include "xwindow.hpp"
#include <chrono>
#include <unistd.h>
#elif STOW_WINDOWS
#include "win32pipeprocess.hpp"
#include "win32ptyprocess.hpp"
#include "win32window.hpp"
#include <windows.h>
#endif

#include "window.hpp"

int main() {
	SKIP_IF_NO_DISPLAY();

	// Create window config
	stow::WindowConfig win_cfg;
	win_cfg.px = stow::Position(10);
	win_cfg.py = stow::Position(10);
	win_cfg.overlay = true;

#if STOW_POSIX
	// test overlay mode
	ShXWindowPr xwin = XWindow::create(win_cfg);
	xwin->setup();

	auto start = std::chrono::steady_clock::now();
	constexpr double timeout_sec = 3.0;
	int iterations = 0;

	while (true) {
		auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration<double>(now - start).count() >= timeout_sec) break;

		// run a simple command
		ShProcessPr process = PTYProcess::create();
		process->setup();
		process->start_cmd("echo", {"stow test iteration", std::to_string(iterations++)});
		process->read_text(xwin);

		usleep(100000); // 100ms between iterations
	}

	// test normal mode
	stow::WindowConfig normal_cfg;
	normal_cfg.overlay = false;

	ShXWindowPr xwin2 = XWindow::create(normal_cfg);
	xwin2->setup();

	xwin2->draw("normal mode test\n");
	xwin2->run();
	usleep(500000); // show for 500ms
#endif

	return 0;
}
