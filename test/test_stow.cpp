// combined x window and process
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"
#include "xwindow.hpp"

#include <unistd.h>

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

	// create xwindow
	ShXWindowPr xwin = XWindow::create();
	// keep visible on WSLg/XWayland
	gconf.px = {10, 0, 0};
	gconf.py = {10, 0, 0};
	gconf.tx = {0, 0, 0};
	gconf.ty = {0, 0, 0};
	xwin->_overlay = true; // click-through
	xwin->_override_redirect = false; // WM-managed for visibility
	xwin->_transparent_background = true;

	// setup window -> what do we need to pass into it
	xwin->setup();

	while(true) {
		// fire process for cmdline input
		ShProcessPr process = PTYProcess::create();
		process->setup();
		process->start_cmd(cmd,args);
		process->read_text(xwin);
		sleep(gconf.period);
	}

	// check process
	// it either ends or runs indefinitely
	// redirect process output and capture in string
	// @hey: implement buffer for output...
}
