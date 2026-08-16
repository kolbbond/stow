// Assertions over stow::Position and WindowConfig::resolve_anchor.
// Headless: no X11, no $DISPLAY.
#include "stow/config.hpp"
#include "check.hpp"

int main() {
	// --- Position::parse ---
	stow::Position p = stow::Position::parse("10");
	CHECK_EQ(p.value, 10);
	CHECK_EQ(int(p.prefix), 0);
	CHECK_EQ(int(p.suffix), 0);

	p = stow::Position::parse("-10");
	CHECK_EQ(p.value, 10);
	CHECK_EQ(p.prefix, '-');

	p = stow::Position::parse("50%");
	CHECK_EQ(p.value, 50);
	CHECK_EQ(p.suffix, '%');

	p = stow::Position::parse("+25%");
	CHECK_EQ(p.value, 25);
	CHECK_EQ(p.prefix, '+');
	CHECK_EQ(p.suffix, '%');

	p = stow::Position::parse("");
	CHECK_EQ(p.value, 0);

	// --- Position::resolve ---
	// absolute
	CHECK_EQ(stow::Position::parse("10").resolve(1920, 100), 10);
	// percent of screen
	CHECK_EQ(stow::Position::parse("50%").resolve(1920, 100), 960);
	// negative prefix measures from the far edge, leaving room for the window
	CHECK_EQ(stow::Position::parse("-10").resolve(1920, 100), 1810);
	// negative percent
	CHECK_EQ(stow::Position::parse("-25%").resolve(1000, 100), 650);
	// explicit plus behaves as absolute
	CHECK_EQ(stow::Position::parse("+10").resolve(1920, 100), 10);

	// --- resolve_anchor ---
	stow::WindowConfig cfg;
	cfg.margin_x = 10;
	cfg.margin_y = 20;
	int x = -1, y = -1;

	cfg.anchor = stow::Anchor::TopLeft;
	cfg.resolve_anchor(1920, 1080, 400, 300, x, y);
	CHECK_EQ(x, 10);
	CHECK_EQ(y, 20);

	cfg.anchor = stow::Anchor::BottomRight;
	cfg.resolve_anchor(1920, 1080, 400, 300, x, y);
	CHECK_EQ(x, 1920 - 400 - 10);
	CHECK_EQ(y, 1080 - 300 - 20);

	cfg.anchor = stow::Anchor::Center;
	cfg.resolve_anchor(1920, 1080, 400, 300, x, y);
	CHECK_EQ(x, (1920 - 400) / 2);
	CHECK_EQ(y, (1080 - 300) / 2);

	cfg.anchor = stow::Anchor::Top;
	cfg.resolve_anchor(1920, 1080, 400, 300, x, y);
	CHECK_EQ(x, (1920 - 400) / 2);
	CHECK_EQ(y, 20);

	// Custom uses px/py plus the tx/ty offsets
	cfg.anchor = stow::Anchor::Custom;
	cfg.px = stow::Position::parse("100");
	cfg.py = stow::Position::parse("200");
	cfg.tx = stow::Position(0);
	cfg.ty = stow::Position(0);
	cfg.resolve_anchor(1920, 1080, 400, 300, x, y);
	CHECK_EQ(x, 100);
	CHECK_EQ(y, 200);

	// --- anchor string round-trip ---
	CHECK(stow::anchor_from_string("tr") == stow::Anchor::TopRight);
	CHECK(stow::anchor_from_string("bottom-left") == stow::Anchor::BottomLeft);
	CHECK(stow::anchor_from_string("nonsense") == stow::Anchor::Custom);
	CHECK_EQ(std::string(stow::anchor_to_string(stow::Anchor::BottomRight)), std::string("bottom-right"));

	CHECK_REPORT();
}
