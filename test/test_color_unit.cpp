// Assertions over stow::Color parsing and conversion. Headless.
#include "stow/color.hpp"
#include "check.hpp"

int main() {
	// --- #rrggbb ---
	stow::Color c = stow::Color::parse("#ff8000");
	CHECK_EQ(int(c.r), 0xff);
	CHECK_EQ(int(c.g), 0x80);
	CHECK_EQ(int(c.b), 0x00);
	CHECK_EQ(int(c.a), 0xff);  // opaque by default

	// --- #rrggbbaa ---
	c = stow::Color::parse("#10203040");
	CHECK_EQ(int(c.r), 0x10);
	CHECK_EQ(int(c.g), 0x20);
	CHECK_EQ(int(c.b), 0x30);
	CHECK_EQ(int(c.a), 0x40);

	// --- leading '#' is optional ---
	c = stow::Color::parse("00a080");
	CHECK_EQ(int(c.r), 0x00);
	CHECK_EQ(int(c.g), 0xa0);
	CHECK_EQ(int(c.b), 0x80);

	// --- case-insensitive ---
	CHECK(stow::Color::parse("#AABBCC") == stow::Color::parse("#aabbcc"));

	// --- malformed input yields opaque black rather than throwing ---
	CHECK(stow::Color::parse("") == stow::Color::black());
	CHECK(stow::Color::parse("#12") == stow::Color::black());
	CHECK(stow::Color::parse("#gggggg") == stow::Color::black());
	CHECK(stow::Color::parse("not a color") == stow::Color::black());

	// --- rgb() packs to 0xRRGGBB, dropping alpha ---
	CHECK_EQ(stow::Color::parse("#ff8000").rgb(), 0xff8000u);
	CHECK_EQ(stow::Color::parse("#ff800040").rgb(), 0xff8000u);

	// --- named helpers ---
	CHECK_EQ(stow::Color::red().rgb(), 0xff0000u);
	CHECK_EQ(stow::Color::green().rgb(), 0x00ff00u);
	CHECK_EQ(stow::Color::blue().rgb(), 0x0000ffu);
	CHECK_EQ(stow::Color::white().rgb(), 0xffffffu);
	CHECK_EQ(stow::Color::black().rgb(), 0x000000u);
	CHECK_EQ(int(stow::Color::none().a), 0);  // fully transparent

	// --- round-trip through rgb() ---
	CHECK(stow::Color::from_rgb(0x00a080) == stow::Color::parse("#00a080"));

	// --- usable in constant expressions ---
	static_assert(stow::Color::parse("#ff0000") == stow::Color::red());
	static_assert(stow::Color::parse("bad").rgb() == 0u);

	CHECK_REPORT();
}
