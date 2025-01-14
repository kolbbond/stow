// test start a separate process
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"

int main(int argc, char** argv) {

	// parse cmdline input
	std::string cmd = "ls";
	if(argc > 1) {
		for(int i = 0; i < argc; i++) {
			std::printf("%s\t", argv[i]);
		}
		std::printf("\n");
		cmd = std::string(argv[1]);
	}
	//cmd = "ls";

	// fire process for cmdline input
	ShProcessPr process = PTYProcess::create();
	process->setup();
	process->start_cmd(cmd);
	process->read_text();

	// check process
	// it either ends or runs indefinitely
	// redirect process output and capture in string
	// @hey: implement buffer for output...
}
