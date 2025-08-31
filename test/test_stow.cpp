// combined x window and process
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"
#include "xwindow.hpp"

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

	if(cmd.empty()) std::cerr << "input cmd";

	// create xwindow
	ShXWindowPr xwin = XWindow::create();

	// setup window -> what do we need to pass into it
	xwin->setup();

	// fire process for cmdline input
	ShProcessPr process = PTYProcess::create();
	process->setup();
	process->start_cmd(cmd,args);

    // we need to wait to read text
	process->read_text(xwin);

	// check process
	// it either ends or runs indefinitely
	// redirect process output and capture in string
	// @hey: implement buffer for output...
}
