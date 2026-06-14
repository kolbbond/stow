// bounded integration demo: build a Dashboard from the fixture config and run briefly
#include "stow/hud_config.hpp"
#include "stow/dashboard.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
	if(argc < 2) { std::cerr << "usage: test_dashboard <config> [timeout_sec]\n"; return 1; }
	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) { std::cerr << "test_dashboard: " << cfg.error << "\n"; return 1; }
	stow::Dashboard dash = stow::Dashboard::from_config(cfg);
	dash.set_timeout(argc > 2 ? std::atof(argv[2]) : 2.0);
	dash.run();
	std::cout << "test_dashboard ok\n";
	return 0;
}
