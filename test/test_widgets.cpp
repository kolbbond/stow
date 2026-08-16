// Widgets render through the public Overlay API - no raw X11 handles.
// Needs $DISPLAY; skips (77) without one.
#include "stow/overlay.hpp"
#include "stow/widget.hpp"
#include "hud/widgets.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <ctime>

int main() {
	SKIP_IF_NO_DISPLAY();

	// --- pure value helpers, no window needed ---
	CHECK_EQ(stow::number_text("CPU", "42"), std::string("CPU\n42"));
	CHECK_EQ(stow::number_text("", "42"), std::string("42"));
	CHECK_EQ(stow::bar_fraction("50", 100.0), 0.5);
	CHECK_EQ(stow::bar_fraction("150", 100.0), 1.0);   // clamped
	CHECK_EQ(stow::bar_fraction("-5", 100.0), 0.0);    // clamped
	CHECK_EQ(stow::bar_fraction("abc", 100.0), 0.0);   // non-numeric
	CHECK_EQ(stow::bar_fraction("50", 0.0), 0.0);      // max <= 0

	stow::Overlay ov(stow::OverlayConfig{.size = {400, 200}, .font = "monospace:size=10"});
	ov.show();

	stow::DashStats stats;
	stats.now = std::time(nullptr);
	stats.fps = 60.0;

	// A bar widget renders through the public drawing API, with no access to
	// _dpy / _xgc / _drawable.
	stow::BarWidget bar("echo 63", "MEM", 100.0, 1);
	bar.update();

	stow::RenderCtx ctx;
	ctx.win = &ov;
	ctx.region = stow::Rect{0, 0, 200, 60};
	ctx.stats = &stats;

	ov.begin();
	bar.render(ctx);
	ov.end();
	CHECK(ov.pump());

	// A number widget in the neighbouring region
	stow::NumberWidget num("echo 42", "CPU", 1);
	num.update();
	ctx.region = stow::Rect{200, 0, 200, 60};
	ov.begin();
	num.render(ctx);
	ov.end();
	CHECK(ov.pump());

	// The built-in HUD fields widget
	stow::HudWidget hud({"time", "fps", "mouse"});
	hud.update();
	ctx.region = stow::Rect{0, 60, 400, 60};
	ov.begin();
	hud.render(ctx);
	ov.end();
	CHECK(ov.pump());

	CHECK_REPORT();
}
