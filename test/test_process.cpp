// test start a separate process
#include "stow/config.hpp"
#include "pipeprocess.hpp"
#include "ptyprocess.hpp"

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
		std::cerr << "input cmd\n";
		return 1;
	}

	// Create process with config
	stow::ProcessConfig cfg;
	cfg.command = cmd;
	cfg.args = args;
	cfg.use_pty = true;

	// fire process for cmdline input
	ShProcessPr process = PTYProcess::create(cfg);
	process->setup();
	process->start_cmd(cmd, args);
	process->read_text();

	return 0;
}
