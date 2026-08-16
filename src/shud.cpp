// shud - stow hud: grid-based overlay dashboard driven by a config file
#include "stow/hud_config.hpp"
#include "hud/dashboard.hpp"

#include <cstdlib>
#include <iostream>

static int x_error_handler(Display* dpy, XErrorEvent* ev) {
	char buf[256];
	XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
	std::cerr << "X error: " << buf << " (opcode=" << static_cast<int>(ev->request_code) << ")\n";
	return 0;
}

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: shud <config.ini> [timeout_sec]\n";
		return 1;
	}
	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) {
		std::cerr << "shud: " << (cfg.error.empty() ? "invalid config" : cfg.error) << "\n";
		return 1;
	}

	XSetErrorHandler(x_error_handler);

	stow::Dashboard dash = stow::Dashboard::from_config(cfg);
	if(argc > 2) dash.set_timeout(std::atof(argv[2]));
	dash.run();
	return 0;
}
