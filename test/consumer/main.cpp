// Everything a new CLI needs, using nothing but the public API.
#include <stow/hud.hpp>
#include <stow/overlay.hpp>
#include <stow/screen.hpp>

#include <cstdio>
#include <string>

int main() {
	stow::OverlayConfig cfg;
	cfg.anchor = stow::Anchor::TopRight;
	cfg.size = {120, 40};
	cfg.fg = stow::Color::red();

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(cfg, &err);
	if(!ov) {
		// Not a build failure: this test is about compiling and linking.
		std::fprintf(stderr, "no display: %s\n", err.message.c_str());
		return 0;
	}

	ov->show();
	stow::Point p = stow::pointer();
	ov->begin();
	ov->rect({0, 0, 120, 40}, stow::Color::red(), 2);
	ov->text(6, 24, std::to_string(p.x) + "," + std::to_string(p.y));
	ov->end();
	ov->pump();
	return 0;
}
