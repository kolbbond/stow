// combined window and process - cross-platform test
#include "platform.hpp"

#if STOW_POSIX
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"
#include "xwindow.hpp"
#include <unistd.h>
#elif STOW_WINDOWS
#include "win32pipeprocess.hpp"
#include "win32ptyprocess.hpp"
#include "win32window.hpp"
#include <windows.h>
#endif

#include "window.hpp"

int main(int argc, char** argv) {

	// parse cmdline input
	std::string cmd;
	std::vector<std::string> args;
	if(argc > 1) {
		for(int i = 0; i < argc; i++) {
			std::printf("%s\t", argv[i]);
			if(i > 1) args.push_back(std::string(argv[i]));
		}
		std::printf("\n");
		cmd = std::string(argv[1]);
	}

	if(cmd.empty()) {
		std::cerr << "input cmd";
		return 1;
	}

	// create window using abstract factory
	ShWindowPtr win = StowWindow::create();
	// keep visible on WSLg/XWayland
	gconf.px = {10, 0, 0};
	gconf.py = {10, 0, 0};
	gconf.tx = {0, 0, 0};
	gconf.ty = {0, 0, 0};
	win->_overlay = true; // click-through
	win->_override_redirect = true; // borderless popup
	win->_transparent_background = true;

	// setup window -> what do we need to pass into it
	win->setup();

	while(true) {
		// fire process for cmdline input
#if STOW_POSIX
		ShProcessPr process = PTYProcess::create();
#elif STOW_WINDOWS
		ShProcessPr process = Win32PTYProcess::create();
#endif
		process->setup();
		process->start_cmd(cmd, args);
		process->read_text(win);
#if STOW_POSIX
		sleep(gconf.period);
#elif STOW_WINDOWS
		Sleep(gconf.period * 1000);
#endif
	}

	// check process
	// it either ends or runs indefinitely
	// redirect process output and capture in string
	// @hey: implement buffer for output...
}
