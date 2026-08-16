// stow - text overlay: runs a command and renders its output in an overlay.
#include "stow/cli.hpp"
#include "stow/config.hpp"
#include "stow/overlay.hpp"
#include "proc/pipeprocess.hpp"
#include "proc/ptyprocess.hpp"

#include <unistd.h>

namespace {

// The CLI still parses into the older WindowConfig; map it onto the public
// OverlayConfig the library takes.
stow::OverlayConfig to_overlay_config(const stow::WindowConfig& w) {
	stow::OverlayConfig o;
	o.anchor = w.anchor;
	o.x = w.px;
	o.y = w.py;
	o.monitor = w.monitor;
	o.font = w.font;
	o.fg = stow::Color::parse(w.fg);
	o.bg = stow::Color::parse(w.bg);
	o.alpha = w.alpha;
	o.align = w.align;
	o.border_px = w.border_px;
	o.borderless = w.borderless;
	o.clickthrough = w.overlay;
	o.on_top = w.on_top;
	o.title = w.title;
	if(w.use_fixed_geometry) o.size = stow::Size{w.fixed_w, w.fixed_h};
	return o;
}

}  // namespace

int main(int argc, char** argv) {
	stow::CLI cli;
	if(!cli.parse(argc, argv)) {
		std::fprintf(stderr, "Error: %s\n", cli.error.c_str());
		stow::CLI::print_usage(argv[0]);
		return 1;
	}

	if(cli.show_help) {
		stow::CLI::print_usage(argv[0]);
		return 0;
	}
	if(cli.show_version) {
		stow::CLI::print_version();
		return 0;
	}

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(to_overlay_config(cli.config.window), &err);
	if(!ov) {
		std::fprintf(stderr, "stow: %s\n", err.message.c_str());
		return 1;
	}
	ov->show();

	// Re-run the command every `period` seconds, streaming each run's output.
	while(ov->pump()) {
		if(cli.config.process.use_pty) {
			auto pty = PTYProcess::create(cli.config.process);
			pty->set_default_fg(cli.config.window.fg);
			pty->setup();
			pty->start_cmd(cli.command(), cli.args());
			pty->read_text(*ov);
		} else {
			auto pipe = PipeProcess::create(cli.config.process);
			pipe->setup();
			pipe->start_cmd(cli.command(), cli.args());
			pipe->read_text();
		}

		sleep(cli.config.process.period);
	}

	return 0;
}
