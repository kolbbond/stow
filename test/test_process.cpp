// test start a separate process
#include "process.hpp"

int main(int argc, char** argv) {

	// parse cmdline input

	// command
	std::string cmd = "top";

	// fire process for cmdline input
	ShProcessPr process = Process::create();
	process->setup();
	process->start_cmd(cmd);
	process->read_text();

	// check process
	// it either ends or runs indefinitely
	// redirect process output and capture in string
	// @hey: implement buffer for output...
}
