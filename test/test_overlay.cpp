// stow::Overlay lifecycle, built against the public API only.
// Needs $DISPLAY; skips (77) without one.
#include "stow/overlay.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <utility>

int main() {
	SKIP_IF_NO_DISPLAY();

	// --- construction succeeds and reports capabilities ---
	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(
		{
			.anchor = stow::Anchor::TopRight,
			.size = {200, 60},
			.font = "monospace:size=10",
			.alpha = 0.8,
			.clickthrough = true,
		},
		&err);

	CHECK(ov.has_value());
	if(!ov) {
		std::fprintf(stderr, "create failed: %s\n", err.message.c_str());
		CHECK_REPORT();
	}
	CHECK_EQ(int(err.code), int(stow::Error::Code::None));

	// Capabilities are reported, never asserted true: on WSLg or a compositor
	// without XShape/ARGB these are legitimately false and the overlay must
	// still work. We assert only that querying is safe.
	stow::Caps caps = ov->caps();
	std::fprintf(stderr, "caps: clickthrough=%d transparency=%d override_redirect=%d\n", caps.clickthrough,
		caps.transparency, caps.override_redirect);

	// --- show / draw / pump / hide ---
	CHECK(!ov->open());  // not shown yet
	ov->show();
	CHECK(ov->open());

	ov->set_text("hello overlay");
	CHECK(ov->pump());

	ov->set_spans({
		{{"red ", 0xff0000}, {"green", 0x00ff00}},
		{{"second line", 0xffffff}},
	});
	CHECK(ov->pump());

	ov->hide();
	CHECK(ov->pump());  // hidden is not closed
	ov->show();

	// --- move semantics: an Overlay is move-only and survives the move ---
	stow::Overlay moved = std::move(*ov);
	moved.set_text("after move");
	CHECK(moved.pump());

	// --- close() ends the pump loop ---
	moved.close();
	CHECK(!moved.open());
	CHECK(!moved.pump());

	CHECK_REPORT();
}
