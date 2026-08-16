// Stow dashboard: owns grid + window(s) + widgets, runs the render/poll loop.
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>
#include <cctype>
#include <iostream>

#include <X11/keysym.h>
#include <poll.h>
#include <unistd.h>

#include "stow/grid.hpp"
#include "stow/hud_config.hpp"
#include "stow/widget.hpp"
#include "stow/overlay.hpp"
#include "stow/screen.hpp"
#include "hud/widgets.hpp"

namespace stow {

// split a command string into argv honoring single/double quotes
inline std::vector<std::string> split_cmd(const std::string& s) {
	std::vector<std::string> out;
	std::string cur;
	char quote = 0;
	for(char c : s) {
		if(quote) { if(c == quote) quote = 0; else cur.push_back(c); continue; }
		if(c == '"' || c == '\'') { quote = c; continue; }
		if(c == ' ' || c == '\t') { if(!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
		cur.push_back(c);
	}
	if(!cur.empty()) out.push_back(cur);
	return out;
}

// parse a keybind like "super+h", "ctrl+shift+escape", "F12"
inline bool parse_keybind(const std::string& s, unsigned int& mod_mask, KeySym& keysym) {
	mod_mask = 0;
	keysym = NoSymbol;
	std::vector<std::string> parts;
	std::string cur;
	for(char c : s) {
		if(c == '+') { if(!cur.empty()) { parts.push_back(cur); cur.clear(); } }
		else cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
	}
	if(!cur.empty()) parts.push_back(cur);
	if(parts.empty()) return false;
	for(size_t i = 0; i + 1 < parts.size(); i++) {
		const std::string& m = parts[i];
		if(m == "ctrl" || m == "control") mod_mask |= ControlMask;
		else if(m == "shift") mod_mask |= ShiftMask;
		else if(m == "alt" || m == "mod1") mod_mask |= Mod1Mask;
		else if(m == "super" || m == "mod4" || m == "win") mod_mask |= Mod4Mask;
	}
	const std::string& key = parts.back();
	keysym = XStringToKeysym(key.c_str());
	if(keysym == NoSymbol) {
		std::string cap = key;
		cap[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap[0])));
		keysym = XStringToKeysym(cap.c_str());
	}
	if(keysym == NoSymbol) {
		std::string up = key;
		for(char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		keysym = XStringToKeysym(up.c_str());
	}
	return keysym != NoSymbol;
}

// build the widget for one CellSpec
inline std::unique_ptr<Widget> make_widget(const CellSpec& c, int default_period) {
	int period = (c.period > 0) ? c.period : default_period;
	if(c.is_hud || c.widget == "hud") return std::make_unique<HudWidget>(c.fields);
	if(c.widget == "number") return std::make_unique<NumberWidget>(c.cmd, c.label, period);
	if(c.widget == "bar") return std::make_unique<BarWidget>(c.cmd, c.label, c.max, period);
	std::vector<std::string> parts = split_cmd(c.cmd);
	if(parts.empty()) return nullptr;
	std::string cmd = parts[0];
	std::vector<std::string> args(parts.begin() + 1, parts.end());
	return std::make_unique<CommandWidget>(cmd, args, period);
}

class Dashboard {
public:
	explicit Dashboard(const GridConfig& grid) : _grid(grid) {}

	int monitor = -1;
	int period = 1;
	bool single_window = false;
	bool grid_lines = true;
	bool fit_to_cells = true;
	std::string toggle_key = "super+h";

	void add(int row, int col, std::unique_ptr<Widget> w) {
		_pending.push_back({row, col, std::move(w)});
	}

	void set_timeout(double s) { _timeout_sec = s; }

	static Dashboard from_config(const HudConfig& cfg) {
		Dashboard d(cfg.grid);
		d.monitor = cfg.monitor;
		d.period = cfg.period;
		d.single_window = cfg.single_window;
		d.grid_lines = cfg.grid_lines;
		d.fit_to_cells = cfg.fit_to_cells;
		d.toggle_key = cfg.toggle_key;
		for(const CellSpec& c : cfg.cells) {
			std::unique_ptr<Widget> w = make_widget(c, cfg.period);
			if(w) d.add(c.row, c.col, std::move(w));
		}
		return d;
	}

	void run();

private:
	struct Pending { int row, col; std::unique_ptr<Widget> widget; };
	// Overlay is move-only; shared_ptr gives Placed entries a stable address for
	// RenderCtx and lets several cells share one window in single_window mode.
	struct Placed { std::unique_ptr<Widget> widget; Rect region; std::shared_ptr<Overlay> win; };

	GridConfig _grid;
	std::vector<Pending> _pending;
	double _timeout_sec = 0.0;
	bool _interactive = false;
};

inline void Dashboard::run() {
	GridLayout grid(_grid);

	// monitor geometry, via the public screen API
	Monitor mon = primary_monitor();
	if(monitor >= 0) {
		std::vector<Monitor> all = monitors();
		if(monitor < static_cast<int>(all.size())) mon = all[monitor];
	}
	int monitor_x = mon.x, monitor_y = mon.y;
	unsigned int screen_w = mon.width, screen_h = mon.height;
	if(screen_w == 0 || screen_h == 0) { std::cerr << "dashboard: cannot open display\n"; return; }

	unsigned int grid_w = 0, grid_h = 0;
	grid.total_size(screen_w, screen_h, grid_w, grid_h);
	std::vector<Rect> geoms = grid.calculate(screen_w, screen_h);
	if(geoms.empty()) { std::cerr << "dashboard: failed to build grid\n"; return; }

	GridLayout::GridLines lines;
	if(single_window && grid_lines) lines = grid.get_grid_lines(screen_w, screen_h);

	const Color fg = Color::parse("#00a080");

	auto make_overlay = [&](int x, int y, unsigned int w, unsigned int h) -> std::shared_ptr<Overlay> {
		OverlayConfig oc;
		oc.title = "shud";
		oc.clickthrough = true;
		oc.size = Size{w, h};
		Error err;
		std::optional<Overlay> ov = Overlay::create(oc, &err);
		if(!ov) { std::cerr << "dashboard: " << err.message << "\n"; return nullptr; }
		ov->move_to(x, y);
		ov->show();
		return std::make_shared<Overlay>(std::move(*ov));
	};

	std::shared_ptr<Overlay> shared;
	if(single_window) {
		shared = make_overlay(monitor_x, monitor_y, fit_to_cells ? grid_w : screen_w, fit_to_cells ? grid_h : screen_h);
		if(!shared) return;
	}

	std::vector<Placed> placed;
	for(Pending& p : _pending) {
		int idx = grid.cell_index(p.row, p.col);
		if(idx < 0 || idx >= static_cast<int>(geoms.size())) continue;
		std::shared_ptr<Overlay> win;
		Rect region;
		if(single_window) {
			win = shared;
			region = geoms[idx];
		} else {
			win = make_overlay(monitor_x + geoms[idx].x, monitor_y + geoms[idx].y, geoms[idx].width, geoms[idx].height);
			if(!win) continue;
			region = Rect{0, 0, geoms[idx].width, geoms[idx].height};
		}
		placed.push_back({std::move(p.widget), region, win});
	}

	if(placed.empty() && !shared) { std::cerr << "dashboard: no widgets placed\n"; return; }

	// The toggle key needs a root-window grab, which the Overlay API does not
	// expose (and should not - it is a global input concern, not a window one).
	// Dashboard keeps its own connection for exactly that.
	Display* dpy = XOpenDisplay(nullptr);
	Window root = dpy ? DefaultRootWindow(dpy) : 0;
	unsigned int tmod = 0; KeySym tsym = NoSymbol; KeyCode tcode = 0;
	if(dpy && parse_keybind(toggle_key, tmod, tsym)) {
		tcode = XKeysymToKeycode(dpy, tsym);
		if(tcode) {
			XGrabKey(dpy, tcode, tmod, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | LockMask, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | Mod2Mask | LockMask, root, False, GrabModeAsync, GrabModeAsync);
		}
		XSync(dpy, False);
	}

	DashStats stats;
	int frames = 0;
	auto last_fps = std::chrono::steady_clock::now();
	auto run_start = std::chrono::steady_clock::now();

	while(true) {
		if(_timeout_sec > 0.0 &&
			std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count() >= _timeout_sec) break;

		for(Placed& pw : placed) pw.widget->update();

		std::vector<struct pollfd> pfds;
		for(Placed& pw : placed) {
			int fd = pw.widget->fd();
			if(fd >= 0) pfds.push_back({fd, static_cast<short>(POLLIN | POLLHUP | POLLERR), 0});
		}
		if(!pfds.empty()) poll(pfds.data(), pfds.size(), 100);
		else usleep(100 * 1000);

		stats.now = std::time(nullptr);
		frames++;
		auto now_c = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now_c - last_fps).count();
		if(elapsed >= 1.0) { stats.fps = frames / elapsed; frames = 0; last_fps = now_c; }
		Point mp = pointer();
		stats.mouse_x = mp.x;
		stats.mouse_y = mp.y;

		if(single_window && shared) shared->begin();

		for(Placed& pw : placed) {
			if(!single_window && pw.win) pw.win->begin();
			RenderCtx ctx{pw.win.get(), pw.region, &stats};
			pw.widget->render(ctx);
			if(!single_window && pw.win) { pw.win->end(); pw.win->pump(); }
		}

		if(single_window && shared) {
			if(grid_lines) {
				// A grid line is a 1px filled rect - no raw X11 drawing needed.
				for(int hy : lines.horizontal_y) shared->rect(Rect{0, hy, grid_w, 1}, fg, 0);
				for(int vx : lines.vertical_x) shared->rect(Rect{vx, 0, 1, grid_h}, fg, 0);
			}
			shared->end();
			shared->pump();
		}

		if(dpy) {
			while(XPending(dpy)) {
				XEvent ev; XNextEvent(dpy, &ev);
				if(ev.type == KeyPress && tcode && ev.xkey.keycode == tcode) {
					_interactive = !_interactive;
					// Interactive means the HUD accepts clicks, so passthrough
					// is the inverse. The pre-port code always passed `true`
					// here, which made the toggle a no-op.
					if(shared) shared->set_clickthrough(!_interactive);
					for(Placed& pw : placed)
						if(!single_window && pw.win) pw.win->set_clickthrough(!_interactive);
				}
			}
		}
	}

	if(dpy) XCloseDisplay(dpy);
}

}  // namespace stow
