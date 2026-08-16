// shud - stow hud: grid-based overlay dashboard driven by a config file.
// A pure consumer of the public API: links stow::stow, includes no X11.
#include "stow/hud.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: shud <config.ini> [timeout_sec]\n";
		return 1;
	}

	stow::Error err;
	std::optional<stow::Hud> hud = stow::Hud::from_file(argv[1], &err);
	if(!hud) {
		std::cerr << "shud: " << err.message << "\n";
		return 1;
	}

	if(argc > 2) hud->set_timeout(std::atof(argv[2]));
	hud->run();
	return 0;
}
