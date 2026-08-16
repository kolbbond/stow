// Tier 3: an ini-driven HUD, built through the public API only.
// Config parsing is asserted headless; the render half needs $DISPLAY.
#include "stow/hud.hpp"
#include "check.hpp"

#include <cstdlib>
#include <cstdio>

int main(int argc, char** argv) {
	const char* ini = (argc > 1) ? argv[1] : HUD_TEST_INI;

	// --- parsing is pure and works headless ---
	stow::HudConfig cfg = stow::HudConfig::load(ini);
	CHECK(cfg.valid());
	CHECK(cfg.error.empty());
	CHECK(!cfg.cells.empty());
	CHECK(cfg.grid.rows > 0);
	CHECK(cfg.grid.cols > 0);

	// --- a bad path fails cleanly rather than crashing ---
	stow::Error err;
	std::optional<stow::Hud> missing = stow::Hud::from_file("/nonexistent/nope.ini", &err);
	CHECK(!missing.has_value());
	CHECK_EQ(int(err.code), int(stow::Error::Code::BadConfig));
	CHECK(!err.message.empty());

	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping the render half\n");
		return 77;
	}

	std::optional<stow::Hud> hud = stow::Hud::from_file(ini, &err);
	CHECK(hud.has_value());
	if(!hud) {
		std::fprintf(stderr, "from_file failed: %s\n", err.message.c_str());
		CHECK_REPORT();
	}

	hud->set_timeout(2.0);
	hud->run();  // returns when the timeout expires

	CHECK_REPORT();
}
