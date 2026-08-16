// A red box that follows the cursor and shows its coordinates.
//
// This is the spec's acceptance example for the Tier 1 API, and it doubles as
// the click-through check: the box sits under your pointer, so if the window
// swallowed input you could not click anything beneath it.
//
// Runs for `duration_sec` (default 3) so it can live in ctest. Needs $DISPLAY.
#include "stow/overlay.hpp"
#include "stow/screen.hpp"
#include "check.hpp"
#include "skip_if_headless.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Compositor pointer lookup - DEMO CODE, deliberately not in the library.
//
// stow::pointer() uses XQueryPointer, which does not work under XWayland:
// XWayland only learns of pointer motion over surfaces it owns, so while the
// cursor is over Wayland-native windows the X server keeps reporting the last
// position it saw. Measured on this machine: XQueryPointer froze at one value
// across four samples while the pointer genuinely moved, and selecting
// PointerMotionMask on the root window yielded 0 MotionNotify events in 4s.
// So this is not a polling-vs-events problem; X is simply never told.
//
// The workaround is to ask the compositor. That belongs here rather than in
// libstow: an overlay library's job is putting a window on screen, not
// tracking input, and a compositor-specific IPC client has no business in a
// public API. Consumers that need the pointer can do exactly this.
//
// Hyprland and X also disagree about layout, so coordinates must be
// translated. The monitor *names* match, which makes the mapping simple:
//   hyprland                       x11
//   DVI-D-1  at 1920,1080          DVI-D-1  at 0,0
//   HDMI-A-3 at 0,1080             HDMI-A-3 at 1920,0
//   DP-3     at 960,0              DP-3     at 3840,0
// ---------------------------------------------------------------------------

std::string run_cmd(const char* cmd) {
	std::FILE* pipe = ::popen(cmd, "r");
	if(!pipe) return "";
	std::string out;
	std::array<char, 512> buf;
	while(std::fgets(buf.data(), int(buf.size()), pipe)) out += buf.data();
	::pclose(pipe);
	return out;
}

struct NamedRect {
	std::string name;
	int x = 0, y = 0;
	unsigned int w = 0, h = 0;
	bool contains(int px, int py) const {
		return px >= x && px < x + int(w) && py >= y && py < y + int(h);
	}
};

// Parse `hyprctl monitors`: "Monitor <name> (ID n):" then "\t<W>x<H>@rate at <X>x<Y>"
std::vector<NamedRect> hypr_monitors() {
	std::vector<NamedRect> out;
	std::string text = run_cmd("hyprctl monitors 2>/dev/null");
	size_t pos = 0;
	while((pos = text.find("Monitor ", pos)) != std::string::npos) {
		pos += 8;
		size_t sp = text.find(' ', pos);
		if(sp == std::string::npos) break;
		NamedRect m;
		m.name = text.substr(pos, sp - pos);

		// the geometry line is the next one starting with a tab
		size_t line = text.find("\n\t", sp);
		if(line == std::string::npos) break;
		line += 2;
		size_t eol = text.find('\n', line);
		std::string geom = text.substr(line, eol - line);
		// "1920x1080@144.00101 at 1920x1080"
		if(std::sscanf(geom.c_str(), "%ux%u@%*f at %dx%d", &m.w, &m.h, &m.x, &m.y) == 4) out.push_back(m);
		pos = eol == std::string::npos ? text.size() : eol;
	}
	return out;
}

// Translate a point from one monitor layout to another, matching by name.
std::optional<stow::Point> map_between(stow::Point p, const std::vector<NamedRect>& from,
	const std::vector<stow::Monitor>& to) {
	for(const NamedRect& f : from) {
		if(!f.contains(p.x, p.y)) continue;
		for(const stow::Monitor& t : to) {
			if(t.name != f.name) continue;
			return stow::Point{t.x + (p.x - f.x), t.y + (p.y - f.y)};
		}
	}
	return std::nullopt;
}

// The pointer, in X coordinates, however we can get it.
stow::Point cursor() {
	if(std::getenv("HYPRLAND_INSTANCE_SIGNATURE")) {
		std::string s = run_cmd("hyprctl cursorpos 2>/dev/null");
		stow::Point hp;
		if(std::sscanf(s.c_str(), "%d, %d", &hp.x, &hp.y) == 2) {
			static const std::vector<NamedRect> hmons = hypr_monitors();
			static const std::vector<stow::Monitor> xmons = stow::monitors();
			if(std::optional<stow::Point> mapped = map_between(hp, hmons, xmons)) return *mapped;
		}
	}
	return stow::pointer();  // native X11, or nothing better available
}

}  // namespace

int main(int argc, char** argv) {
	SKIP_IF_NO_DISPLAY();

	const double duration_sec = (argc > 1) ? std::atof(argv[1]) : 3.0;

	constexpr unsigned int kW = 120;
	constexpr unsigned int kH = 40;

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(
		{
			.size = {kW, kH},
			.font = "monospace:size=10",
			.alpha = 0.9,
			.clickthrough = true,
			.title = "stow-cursor",
		},
		&err);

	CHECK(ov.has_value());
	if(!ov) {
		std::fprintf(stderr, "create failed: %s\n", err.message.c_str());
		CHECK_REPORT();
	}

	stow::Caps caps = ov->caps();
	std::fprintf(stderr, "caps: clickthrough=%d transparency=%d\n", caps.clickthrough, caps.transparency);
	if(!caps.clickthrough) {
		// Not a failure: a compositor may legitimately deny it, and the overlay
		// must still render. But it is worth being loud about.
		std::fprintf(stderr, "WARNING: click-through unavailable; window will swallow input\n");
	}

	ov->show();

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(duration_sec);
	int frames = 0;
	stow::Point last{-1, -1};
	bool ever_moved = false;

	while(std::chrono::steady_clock::now() < deadline) {
		if(!ov->pump(std::chrono::milliseconds(16))) break;  // ~60fps

		stow::Point p = stow::pointer();
		if(last.x >= 0 && !(p == last)) ever_moved = true;
		last = p;

		// Offset so the box sits below-right of the cursor, not under it.
		ov->move_to(p.x + 12, p.y + 12);

		ov->begin();
		ov->rect({0, 0, kW, kH}, stow::Color::red(), 2);
		ov->text(6, 24, std::to_string(p.x) + "," + std::to_string(p.y), stow::Color::white());
		ov->end();

		frames++;
	}

	std::fprintf(stderr, "%d frames in %.1fs; pointer moved: %s (last %d,%d)\n", frames, duration_sec,
		ever_moved ? "yes" : "no", last.x, last.y);

	// The overlay rendered continuously for the whole window.
	CHECK(frames > 10);
	CHECK(ov->open());

	CHECK_REPORT();
}
