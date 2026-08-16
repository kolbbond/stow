// stow::Overlay lifecycle, built against the public API only.
// Needs $DISPLAY; skips (77) without one.
#include "stow/overlay.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <chrono>
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

	// ---- immediate-mode drawing and runtime geometry ----
	stow::Overlay draw_ov(stow::OverlayConfig{
		.size = {200, 80},
		.font = "monospace:size=10",
		.clickthrough = true,
	});
	draw_ov.show();

	// geometry() reflects the configured size
	stow::Rect g = draw_ov.geometry();
	CHECK_EQ(g.width, 200u);
	CHECK_EQ(g.height, 80u);

	// move_to updates geometry after the next pump
	draw_ov.move_to(300, 400);
	CHECK(draw_ov.pump());
	g = draw_ov.geometry();
	CHECK_EQ(g.x, 300);
	CHECK_EQ(g.y, 400);

	// resize updates geometry
	draw_ov.resize({240, 100});
	CHECK(draw_ov.pump());
	g = draw_ov.geometry();
	CHECK_EQ(g.width, 240u);
	CHECK_EQ(g.height, 100u);

	// click-through must survive a resize (XWayland resets ShapeInput)
	CHECK_EQ(draw_ov.caps().clickthrough, caps.clickthrough);

	// a full immediate-mode frame does not crash and pumps clean
	draw_ov.begin();
	draw_ov.rect({0, 0, 240, 100}, stow::Color::red(), 2);   // outline
	draw_ov.rect({10, 10, 40, 20}, stow::Color::blue(), 0);  // filled
	draw_ov.text(6, 40, "1234,5678", stow::Color::white());
	draw_ov.end();
	CHECK(draw_ov.pump());

	// pump(timeout) returns within roughly the timeout rather than blocking
	{
		auto t0 = std::chrono::steady_clock::now();
		CHECK(draw_ov.pump(std::chrono::milliseconds(50)));
		auto elapsed = std::chrono::steady_clock::now() - t0;
		CHECK(elapsed < std::chrono::milliseconds(500));
	}

	// run() drives the callback and stops when the callback closes the overlay
	{
		int ticks = 0;
		draw_ov.run(std::chrono::milliseconds(10), [&ticks](stow::Overlay& o) {
			ticks++;
			o.begin();
			o.text(4, 20, "tick", stow::Color::white());
			o.end();
			if(ticks >= 5) o.close();
		});
		CHECK_EQ(ticks, 5);
		CHECK(!draw_ov.open());
	}

	CHECK_REPORT();
}
