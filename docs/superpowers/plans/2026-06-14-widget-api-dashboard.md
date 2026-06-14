# Widget API + Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn `shud` into a small dashboard library: a `stow::Widget` interface plus a `stow::Dashboard` orchestrator that both the INI config (`stow::HudConfig`, built in Plan 1) and hand-written C++ construct identically, with v1 widgets Command/Hud/Number/Bar.

**Architecture:** Three layers — `Widget` (abstract, draws into a cell `Rect` via the existing `XWindow` primitives), `Dashboard` (owns grid + window(s) + widgets and runs the poll/render/toggle loop currently inlined in `shud.cpp`), and the concrete widgets. The render loop in `src/shud.cpp` is extracted into `Dashboard::run()`; `shud.cpp` shrinks to parse → `Dashboard::from_config` → `run`. Widget *value* logic (number formatting, bar fraction, command capture) is pure and headless-tested; rendering and the loop are X-integration, verified by build + bounded run.

**Tech Stack:** C++20, header-only (`stowlib` INTERFACE target), X11/Xft, CTest. Builds on Plan 1's `include/stow/hud_config.hpp` (`HudConfig`, `CellSpec`).

**Scope note:** This is Plan 2 of 2. Plan 1 (the config module) is complete and merged on `refactor`. Per-cell appearance fields (`fg/bg/font/alpha/align`) and `widget`/`max`/`label`/`fields` are already PARSED by `HudConfig`; this plan CONSUMES them. v2 widgets (Plot, Tab) remain out of scope; the `Widget` interface is designed so they slot in later without changes here.

**Reference for the loop port:** `src/shud.cpp` currently contains the exact poll/fps/mouse/restart/toggle/grid-line loop. Task 6 ports it into `Dashboard`. Read it before that task.

---

## File Structure

New (all header-only, in `include/stow/`):
- `widget.hpp` — `Widget` abstract base, `DashStats`, `RenderCtx`.
- `capture.hpp` — `capture_command(cmd)` value-source helper (popen, last non-empty line).
- `widgets.hpp` — concrete widgets: `HudWidget`, `NumberWidget`, `BarWidget`, `CommandWidget`.
- `dashboard.hpp` — `Dashboard` orchestrator.

New tests:
- `test/test_widgets.cpp` — headless tests for value logic (capture, number text, bar fraction).
- `test/test_dashboard.cpp` — bounded-timeout integration demo over a config (replaces `test_grid`).

Changed:
- `src/shud.cpp` — collapses to parse → `Dashboard::from_config` → `run`.
- `test/CMakeLists.txt` — add `test_widgets`, `test_dashboard`; remove `test_grid`.
- Delete `test/test_grid.cpp` (superseded by `test_dashboard`).

---

### Task 1: Widget interface + DashStats + RenderCtx

**Files:**
- Create: `include/stow/widget.hpp`
- Create: `test/test_widgets.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `test/test_widgets.cpp`:

```cpp
// headless unit tests for widget value logic
#include "stow/widget.hpp"

#include <iostream>
#include <string>

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
	std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++g_failures; } } while(0)

// a minimal concrete widget proves the interface is usable without X
namespace {
struct NullWidget : stow::Widget {
	int updates = 0;
	void update() override { updates++; }
	void render(stow::RenderCtx&) override {}
};
}

static void test_interface() {
	NullWidget w;
	CHECK(w.fd() == -1);          // default: not pollable
	CHECK(w.dirty() == true);     // default: always redraw
	w.update();
	CHECK(w.updates == 1);
}

int main() {
	test_interface();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all widget tests passed\n";
	return 0;
}
```

Add `test_widgets.cpp` to the `test_list` in `test/CMakeLists.txt` (after `test_hud_config.cpp`), and register it with the other `add_test` lines:

```cmake
add_test(NAME test_widgets     COMMAND test_widgets)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build >/dev/null && cmake --build build --target test_widgets 2>&1 | tail -5`
Expected: FAIL — `stow/widget.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `include/stow/widget.hpp`:

```cpp
// Stow dashboard widget interface
#pragma once

#include <ctime>

#include "layout.hpp"   // stow::Rect

// Forward-declare to avoid pulling X11 into headless consumers/tests.
class XWindow;

namespace stow {

// Per-frame shared state a widget may read (e.g. HUD fields).
struct DashStats {
	double fps = 0.0;
	int mouse_x = 0;
	int mouse_y = 0;
	std::time_t now = 0;
};

// What a widget needs to draw itself this frame.
struct RenderCtx {
	XWindow* win = nullptr;   // target window (shared or per-cell); never null at render time
	Rect region;              // pixel rect within the window to draw into
	const DashStats* stats = nullptr;
};

class Widget {
public:
	virtual ~Widget() = default;
	virtual void update() {}                 // refresh internal state (poll process, sample value)
	virtual void render(RenderCtx& ctx) = 0; // draw into ctx.region
	virtual int fd() const { return -1; }    // pollable fd, or -1 if none
	virtual bool dirty() const { return true; }  // skip redraw when false (optional optimization)
};

}  // namespace stow
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_widgets >/dev/null 2>&1 && ./build/bin/test_widgets`
Expected: PASS — `all widget tests passed`.

- [ ] **Step 5: Commit**

```bash
git add include/stow/widget.hpp test/test_widgets.cpp test/CMakeLists.txt
git commit -m "widget: abstract interface + DashStats/RenderCtx"
```

---

### Task 2: capture_command value-source helper

**Files:**
- Create: `include/stow/capture.hpp`
- Modify: `test/test_widgets.cpp`

- [ ] **Step 1: Write the failing test**

Append to `test/test_widgets.cpp` and call from `main` before the failure check:

```cpp
#include "stow/capture.hpp"

static void test_capture() {
	// last non-empty line of stdout, trimmed
	CHECK(stow::capture_command("printf 'a\\nb\\n'") == "b");
	CHECK(stow::capture_command("printf '  42  \\n'") == "42");
	// trailing blank lines ignored
	CHECK(stow::capture_command("printf 'x\\n\\n\\n'") == "x");
	// empty output => empty string
	CHECK(stow::capture_command("true") == "");
	// command that fails to run => empty string (no throw, no crash)
	CHECK(stow::capture_command("this_command_does_not_exist_xyz 2>/dev/null") == "");
}
```

Add `#include "stow/capture.hpp"` is already in the snippet above; add `test_capture();` to `main`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_widgets 2>&1 | tail -5`
Expected: FAIL — `stow/capture.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `include/stow/capture.hpp`:

```cpp
// Run a shell command, return the last non-empty stdout line (trimmed).
// Used by value widgets (Number, Bar). Never throws.
#pragma once

#include <array>
#include <cstdio>
#include <string>

namespace stow {

inline std::string trim_ws(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// Runs `cmd` via /bin/sh -c, captures stdout, returns the last non-empty line trimmed.
inline std::string capture_command(const std::string& cmd) {
	std::FILE* pipe = ::popen(cmd.c_str(), "r");
	if(!pipe) return "";
	std::string out;
	std::array<char, 4096> buf;
	while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
		out += buf.data();
	}
	::pclose(pipe);

	// last non-empty line
	std::string last;
	size_t pos = 0;
	while(pos <= out.size()) {
		size_t nl = out.find('\n', pos);
		std::string line = (nl == std::string::npos) ? out.substr(pos) : out.substr(pos, nl - pos);
		std::string t = trim_ws(line);
		if(!t.empty()) last = t;
		if(nl == std::string::npos) break;
		pos = nl + 1;
	}
	return last;
}

}  // namespace stow
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_widgets >/dev/null 2>&1 && ./build/bin/test_widgets`
Expected: PASS — `all widget tests passed`.

- [ ] **Step 5: Commit**

```bash
git add include/stow/capture.hpp test/test_widgets.cpp
git commit -m "capture: popen-based last-line value source helper"
```

---

### Task 3: Number/Bar value logic (pure, headless)

**Files:**
- Create: `include/stow/widgets.hpp`
- Modify: `test/test_widgets.cpp`

This task adds ONLY the pure compute helpers (no X rendering yet — render() comes in Task 5). They are free functions so they can be unit-tested headlessly.

- [ ] **Step 1: Write the failing test**

Append to `test/test_widgets.cpp`, add `#include "stow/widgets.hpp"` at the top with the other includes, and call `test_value_logic();` from `main`:

```cpp
static void test_value_logic() {
	// number_text: label + value on two lines; missing label => value only
	CHECK(stow::number_text("cores", "8") == "cores\n8");
	CHECK(stow::number_text("", "8") == "8");

	// bar_fraction: clamp to [0,1]; non-numeric => 0
	CHECK(stow::bar_fraction("50", 100.0) == 0.5);
	CHECK(stow::bar_fraction("150", 100.0) == 1.0);   // clamp high
	CHECK(stow::bar_fraction("-5", 100.0) == 0.0);    // clamp low
	CHECK(stow::bar_fraction("abc", 100.0) == 0.0);   // non-numeric
	CHECK(stow::bar_fraction("1", 0.0) == 0.0);       // max<=0 guarded
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_widgets 2>&1 | tail -5`
Expected: FAIL — `stow/widgets.hpp: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `include/stow/widgets.hpp` with ONLY the value helpers for now:

```cpp
// Stow concrete dashboard widgets + their pure value helpers.
#pragma once

#include <string>

namespace stow {

// Two-line "label\nvalue", or just "value" when label is empty.
inline std::string number_text(const std::string& label, const std::string& value) {
	if(label.empty()) return value;
	return label + "\n" + value;
}

// Parse `value` as a number, normalize against `max`, clamp to [0,1].
// Non-numeric or max<=0 => 0.0.
inline double bar_fraction(const std::string& value, double max) {
	if(max <= 0.0) return 0.0;
	double v = 0.0;
	try { v = std::stod(value); } catch(...) { return 0.0; }
	double f = v / max;
	if(f < 0.0) return 0.0;
	if(f > 1.0) return 1.0;
	return f;
}

}  // namespace stow
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_widgets >/dev/null 2>&1 && ./build/bin/test_widgets`
Expected: PASS — `all widget tests passed`.

- [ ] **Step 5: Commit**

```bash
git add include/stow/widgets.hpp test/test_widgets.cpp
git commit -m "widgets: pure number_text/bar_fraction value helpers"
```

---

### Task 4: HudWidget (built-in fields, X render)

**Files:**
- Modify: `include/stow/widgets.hpp`

From here on, widgets do real X rendering. There is no headless test for render() (needs a display); correctness is verified by the integration run in Task 7 and the reviews. Build-only verification per task.

`HudWidget` renders configured fields (`time`, `fps`, `mouse`; unknown name shown literally so typos are visible) as text via `XWindow::draw_region`.

- [ ] **Step 1: Add the include and HudWidget**

At the top of `include/stow/widgets.hpp` add (after `#include <string>`):

```cpp
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>

#include "widget.hpp"
#include "xwindow.hpp"
```

Then add, inside `namespace stow`, after the value helpers:

```cpp
// Renders a set of built-in status fields. `fields` is the ordered list from config.
class HudWidget : public Widget {
public:
	explicit HudWidget(std::vector<std::string> fields) : _fields(std::move(fields)) {
		if(_fields.empty()) _fields = {"time", "fps", "mouse"};
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win || !ctx.stats) return;
		const DashStats& s = *ctx.stats;
		std::ostringstream out;
		out << std::fixed << std::setprecision(1);
		for(const std::string& f : _fields) {
			if(f == "time") {
				char buf[32];
				std::tm* lt = std::localtime(&s.now);
				if(lt && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt)) out << "time: " << buf << "\n";
				else out << "time: ?\n";
			} else if(f == "fps") {
				out << "fps: " << s.fps << "\n";
			} else if(f == "mouse") {
				out << "mouse: " << s.mouse_x << "," << s.mouse_y << "\n";
			} else {
				out << f << ": ?\n";  // unknown field shown literally
			}
		}
		const Rect& r = ctx.region;
		ctx.win->draw_region(out.str(), r.x, r.y, r.width, r.height);
	}

private:
	std::vector<std::string> _fields;
};
```

- [ ] **Step 2: Build (no behavior test yet — needs X)**

Run: `cmake --build build --target test_widgets 2>&1 | tail -5`
Expected: PASS — `Built target test_widgets` (test_widgets includes widgets.hpp; if it compiles, HudWidget compiles). The headless value tests still pass: `./build/bin/test_widgets` → `all widget tests passed`.

- [ ] **Step 3: Commit**

```bash
git add include/stow/widgets.hpp
git commit -m "widgets: HudWidget renders configured built-in fields"
```

---

### Task 5: NumberWidget, BarWidget, CommandWidget (X render + value sourcing)

**Files:**
- Modify: `include/stow/widgets.hpp`

- [ ] **Step 1: Add the three widgets**

At the top of `include/stow/widgets.hpp` add (with the other includes):

```cpp
#include <memory>

#include "capture.hpp"
#include "ptyprocess.hpp"
```

Then add, inside `namespace stow`, after `HudWidget`:

```cpp
// Runs a command every `period` seconds, shows its last stdout line + a label.
class NumberWidget : public Widget {
public:
	NumberWidget(std::string cmd, std::string label, int period)
		: _cmd(std::move(cmd)), _label(std::move(label)), _period(period < 1 ? 1 : period) {}

	void update() override {
		std::time_t t = std::time(nullptr);
		if(t - _last < _period && !_value.empty()) return;
		_last = t;
		_value = capture_command(_cmd);
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win) return;
		const Rect& r = ctx.region;
		ctx.win->draw_region(number_text(_label, _value), r.x, r.y, r.width, r.height);
	}

private:
	std::string _cmd, _label, _value;
	int _period;
	std::time_t _last = 0;
};

// Runs a command every `period` seconds, draws a horizontal gauge of value/max.
class BarWidget : public Widget {
public:
	BarWidget(std::string cmd, std::string label, double max, int period)
		: _cmd(std::move(cmd)), _label(std::move(label)), _max(max), _period(period < 1 ? 1 : period) {}

	void update() override {
		std::time_t t = std::time(nullptr);
		if(t - _last < _period && !_value.empty()) return;
		_last = t;
		_value = capture_command(_cmd);
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win) return;
		XWindow& w = *ctx.win;
		const Rect& r = ctx.region;
		double frac = bar_fraction(_value, _max);

		// label + value text on top
		w.draw_region(number_text(_label, _value), r.x, r.y, r.width, r.height);

		// gauge: outline + filled portion along the bottom of the cell
		int pad = 4;
		int bh = 12;
		int bx = r.x + pad;
		int by = r.y + static_cast<int>(r.height) - bh - pad;
		int bw = static_cast<int>(r.width) - 2 * pad;
		if(bw <= 0 || by <= r.y) return;
		XSetForeground(w._dpy, w._xgc, w._xforeground.pixel);
		XDrawRectangle(w._dpy, w._drawable, w._xgc, bx, by, bw, bh);
		int fillw = static_cast<int>(frac * bw);
		if(fillw > 0) XFillRectangle(w._dpy, w._drawable, w._xgc, bx, by, fillw, bh);
	}

private:
	std::string _cmd, _label, _value;
	double _max;
	int _period;
	std::time_t _last = 0;
};

// Streams a command's full output (PTY, ANSI colors) into its cell, restarting
// every `period` seconds. Wraps the existing PTYProcess behavior.
class CommandWidget : public Widget {
public:
	CommandWidget(std::string cmd, std::vector<std::string> args, int period)
		: _cmd(std::move(cmd)), _args(std::move(args)), _period(period < 1 ? 1 : period) {}

	int fd() const override { return _proc ? _proc->fd() : -1; }

	void update() override {
		std::time_t now = std::time(nullptr);
		if(!_proc) {
			_proc = PTYProcess::create();
			_proc->setup();
			_proc->start_cmd(_cmd, _args);
			_done = false;
		} else if(_done && now >= _restart_at) {
			_proc = PTYProcess::create();
			_proc->setup();
			_proc->start_cmd(_cmd, _args);
			_done = false;
		}
		if(_proc && _proc->is_done() && !_done) {
			_done = true;
			_restart_at = now + _period;
		}
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win || !_proc || _done) return;
		const Rect& r = ctx.region;
		ShXWindowPr win = std::dynamic_pointer_cast<XWindow>(ctx.win->shared_self());
		_proc->pump_region(win, r.x, r.y, r.width, r.height);
	}

private:
	std::string _cmd;
	std::vector<std::string> _args;
	int _period;
	ShPTYProcessPr _proc;
	bool _done = false;
	std::time_t _restart_at = 0;
};
```

NOTE on `CommandWidget::render`: `pump_region` takes a `ShXWindowPr` (shared_ptr). `RenderCtx::win` is a raw `XWindow*`. To bridge, add a helper to `XWindow` (Step 2) returning a `shared_ptr` to itself. The Dashboard holds the windows as `ShXWindowPr`, so this is safe.

- [ ] **Step 2: Add `shared_self()` to XWindow**

CONFIRMED FACTS (do not re-verify): `StowWindow` (include/window.hpp) does NOT inherit `enable_shared_from_this`, and `XWindow::create()` uses `std::make_shared<XWindow>()`. So adding the base is safe (no diamond) and `shared_from_this()` is valid for created instances.

In `include/xwindow.hpp`:
1. Change the class declaration from `class XWindow : public StowWindow {` to:
```cpp
class XWindow : public StowWindow, public std::enable_shared_from_this<XWindow> {
```
2. Ensure `#include <memory>` is present near the top (add it if not).
3. Add to the `public:` section:
```cpp
	ShXWindowPr shared_self() {
		return std::static_pointer_cast<XWindow>(shared_from_this());
	}
```

- [ ] **Step 3: Build**

Run: `cmake --build build --target test_widgets 2>&1 | tail -6`
Expected: PASS — `Built target test_widgets`. Then `./build/bin/test_widgets` → `all widget tests passed` (headless value tests unaffected).

- [ ] **Step 4: Commit**

```bash
git add include/stow/widgets.hpp include/xwindow.hpp
git commit -m "widgets: Number/Bar/Command widgets with value sourcing + render"
```

---

### Task 6: Dashboard orchestrator (port shud's loop)

**Files:**
- Create: `include/stow/dashboard.hpp`
- Reference: `src/shud.cpp` (the loop being ported)

This task ports the render loop from `src/shud.cpp` into `Dashboard`. Read `src/shud.cpp` fully first — the loop there (window creation, poll over fds, fps, mouse query, per-cell pump, grid lines, toggle key, restart-on-period) is the exact behavior to reproduce, but driven through `Widget` instead of inline `CellState`/`PTYProcess`.

- [ ] **Step 1: Create the Dashboard header**

Create `include/stow/dashboard.hpp`:

```cpp
// Stow dashboard: owns grid + window(s) + widgets, runs the render/poll loop.
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <ctime>

#include <X11/keysym.h>
#include <poll.h>
#include <unistd.h>

#include "grid.hpp"
#include "hud_config.hpp"
#include "widget.hpp"
#include "widgets.hpp"
#include "xwindow.hpp"
#include "monitor.hpp"

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

// build the widget for one CellSpec (factory used by from_config)
inline std::unique_ptr<Widget> make_widget(const CellSpec& c, int default_period) {
	int period = (c.period > 0) ? c.period : default_period;
	if(c.is_hud || c.widget == "hud") return std::make_unique<HudWidget>(c.fields);
	if(c.widget == "number") return std::make_unique<NumberWidget>(c.cmd, c.label, period);
	if(c.widget == "bar") return std::make_unique<BarWidget>(c.cmd, c.label, c.max, period);
	// default: command
	std::vector<std::string> parts = split_cmd(c.cmd);
	if(parts.empty()) return nullptr;
	std::string cmd = parts[0];
	std::vector<std::string> args(parts.begin() + 1, parts.end());
	return std::make_unique<CommandWidget>(cmd, args, period);
}

struct PlacedWidget {
	std::unique_ptr<Widget> widget;
	Rect region;     // pixel rect (monitor-relative offset already applied for multi-window: 0,0)
	int grid_index;  // index into geoms
};

class Dashboard {
public:
	explicit Dashboard(const GridConfig& grid) : _grid(grid) {}

	// configuration knobs (set before run())
	int monitor = -1;
	int period = 1;
	bool single_window = false;
	bool grid_lines = true;
	bool fit_to_cells = true;
	std::string toggle_key = "super+h";

	// place a widget at grid (row,col)
	void add(int row, int col, std::unique_ptr<Widget> w) {
		_pending.push_back({row, col, std::move(w)});
	}

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

	void run();   // defined below

private:
	struct Pending { int row, col; std::unique_ptr<Widget> widget; };

	GridConfig _grid;
	std::vector<Pending> _pending;
};

// run() ports src/shud.cpp's main loop. See that file for the reference behavior.
inline void Dashboard::run() {
	GridLayout grid(_grid);

	// --- monitor geometry (mirror shud.cpp) ---
	unsigned int screen_w = 0, screen_h = 0;
	int monitor_x = 0, monitor_y = 0;
	{
		Display* tmp = XOpenDisplay(nullptr);
		if(!tmp) { std::cerr << "dashboard: cannot open display\n"; return; }
		int scr = DefaultScreen(tmp);
		screen_w = DisplayWidth(tmp, scr);
		screen_h = DisplayHeight(tmp, scr);
		auto mgr = MonitorManager::create(tmp);
		const Monitor* mon = mgr->at(monitor);
		if(mon) { monitor_x = mon->x; monitor_y = mon->y; screen_w = mon->width; screen_h = mon->height; }
		XCloseDisplay(tmp);
	}

	unsigned int grid_w = 0, grid_h = 0;
	grid.total_size(screen_w, screen_h, grid_w, grid_h);
	std::vector<Rect> geoms = grid.calculate(screen_w, screen_h);
	if(geoms.empty()) { std::cerr << "dashboard: failed to build grid\n"; return; }

	GridLayout::GridLines lines;
	if(single_window && grid_lines) lines = grid.get_grid_lines(screen_w, screen_h);

	// --- windows ---
	ShXWindowPr shared;
	if(single_window) {
		WindowConfig wc;
		wc.title = "shud";
		wc.overlay = true;
		wc.use_fixed_geometry = true;
		wc.fixed_x = monitor_x; wc.fixed_y = monitor_y;
		wc.fixed_w = fit_to_cells ? grid_w : screen_w;
		wc.fixed_h = fit_to_cells ? grid_h : screen_h;
		shared = XWindow::create(wc);
		shared->setup();
	}

	// place pending widgets into geom slots
	std::vector<PlacedWidget> placed;
	std::vector<ShXWindowPr> cell_windows(geoms.size());  // per-cell windows (multi-window mode)
	for(Pending& p : _pending) {
		int idx = grid.cell_index(p.row, p.col);
		if(idx < 0 || idx >= static_cast<int>(geoms.size())) continue;
		ShXWindowPr win;
		Rect region;
		if(single_window) {
			win = shared;
			region = geoms[idx];
		} else {
			WindowConfig wc;
			wc.title = "shud";
			wc.overlay = true;
			wc.use_fixed_geometry = true;
			wc.fixed_x = monitor_x + geoms[idx].x;
			wc.fixed_y = monitor_y + geoms[idx].y;
			wc.fixed_w = geoms[idx].width;
			wc.fixed_h = geoms[idx].height;
			win = XWindow::create(wc);
			win->setup();
			cell_windows[idx] = win;
			region = Rect{0, 0, geoms[idx].width, geoms[idx].height};
		}
		placed.push_back({std::move(p.widget), region, idx});
		// store the window pointer alongside (parallel vector)
		_win_for.push_back(win);
	}

	Display* dpy = shared ? shared->_dpy : (placed.empty() ? nullptr : _win_for[0]->_dpy);
	if(!dpy) { std::cerr << "dashboard: no windows created\n"; return; }
	Window root = shared ? shared->_root : _win_for[0]->_root;

	// --- toggle key grab (mirror shud.cpp parse_keybind/XGrabKey) ---
	// (Reuse shud's parse_keybind logic; for brevity it is inlined in src/shud.cpp.
	//  Port that function here verbatim as a file-local helper `parse_keybind`.)
	unsigned int tmod = 0; KeySym tsym = NoSymbol; KeyCode tcode = 0;
	if(parse_keybind(toggle_key, tmod, tsym)) {
		tcode = XKeysymToKeycode(dpy, tsym);
		if(tcode) {
			XGrabKey(dpy, tcode, tmod, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | LockMask, root, False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, tcode, tmod | Mod2Mask | LockMask, root, False, GrabModeAsync, GrabModeAsync);
		}
	}
	XSync(dpy, False);

	// --- main loop (mirror shud.cpp) ---
	DashStats stats;
	int frames = 0;
	auto last_fps = std::chrono::steady_clock::now();
	auto run_start = std::chrono::steady_clock::now();

	while(true) {
		if(_timeout_sec > 0.0 &&
			std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count() >= _timeout_sec) break;

		for(PlacedWidget& pw : placed) pw.widget->update();

		// poll widget fds
		std::vector<struct pollfd> pfds;
		for(PlacedWidget& pw : placed) {
			int fd = pw.widget->fd();
			if(fd >= 0) pfds.push_back({fd, POLLIN | POLLHUP | POLLERR, 0});
		}
		if(!pfds.empty()) poll(pfds.data(), pfds.size(), 100);
		else usleep(100 * 1000);

		// fps + mouse + time
		stats.now = std::time(nullptr);
		frames++;
		auto now_c = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now_c - last_fps).count();
		if(elapsed >= 1.0) { stats.fps = frames / elapsed; frames = 0; last_fps = now_c; }
		{
			Window qr, qc; int wx, wy; unsigned int mask;
			if(XQueryPointer(dpy, root, &qr, &qc, &stats.mouse_x, &stats.mouse_y, &wx, &wy, &mask) == False) {
				stats.mouse_x = 0; stats.mouse_y = 0;
			}
		}

		// render each widget into its region
		for(size_t i = 0; i < placed.size(); i++) {
			PlacedWidget& pw = placed[i];
			XWindow* target = single_window ? shared.get() : _win_for[i].get();
			RenderCtx ctx{target, pw.region, &stats};
			pw.widget->render(ctx);
			if(!single_window) _win_for[i]->run();
		}

		if(single_window && shared) {
			if(grid_lines) {
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xforeground.pixel);
				for(int hy : lines.horizontal_y) XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, 0, hy, grid_w, hy);
				for(int vx : lines.vertical_x) XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, vx, 0, vx, grid_h);
			}
			shared->run();
		}

		// toggle key handling
		while(XPending(dpy)) {
			XEvent ev; XNextEvent(dpy, &ev);
			if(ev.type == KeyPress && tcode && ev.xkey.keycode == tcode) {
				_interactive = !_interactive;
				if(shared) shared->set_clickthrough(true);
				else for(auto& w : _win_for) if(w) w->set_clickthrough(true);
			}
		}
	}
}

}  // namespace stow
```

NOTE: this header references two members not yet declared (`_win_for`, `_timeout_sec`, `_interactive`) and a `parse_keybind` helper. Add to the `Dashboard` private section: `std::vector<ShXWindowPr> _win_for; double _timeout_sec = 0.0; bool _interactive = false;` and a public setter `void set_timeout(double s) { _timeout_sec = s; }`. Port `parse_keybind` from `src/shud.cpp` as a file-local `inline` function in `dashboard.hpp` (copy it verbatim — it will be REMOVED from shud.cpp in Task 7, keeping one copy). Add `#include <iostream>` for the cerr calls.

- [ ] **Step 2: Build the dashboard via a temporary compile check**

Create a throwaway TU is unnecessary; instead verify by building `shud` after Task 7 wires it. For THIS task, add a compile smoke test: temporarily ensure the header compiles by building `test_widgets` after adding `#include "stow/dashboard.hpp"` to the TOP of `test/test_widgets.cpp` (it will pull X11 in, which is fine — test_widgets links stowlib).

Run: `cmake --build build --target test_widgets 2>&1 | tail -15`
Expected: compiles cleanly. Fix any compile errors (missing members/includes) until `Built target test_widgets`. Then REMOVE the temporary `#include "stow/dashboard.hpp"` from `test_widgets.cpp` (dashboard is exercised by shud + test_dashboard, not the headless value test) and rebuild to confirm still green.

- [ ] **Step 3: Commit**

```bash
git add include/stow/dashboard.hpp
git commit -m "dashboard: orchestrator porting shud's render/poll loop"
```

---

### Task 7: Wire shud to Dashboard; replace test_grid with test_dashboard

**Files:**
- Modify: `src/shud.cpp`
- Create: `test/test_dashboard.cpp`
- Delete: `test/test_grid.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Rewrite `src/shud.cpp`**

Replace the ENTIRE contents of `src/shud.cpp` with:

```cpp
// shud - stow hud: grid-based overlay dashboard driven by a config file
#include "stow/hud_config.hpp"
#include "stow/dashboard.hpp"

#include <iostream>

static int x_error_handler(Display* dpy, XErrorEvent* ev) {
	char buf[256];
	XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
	std::cerr << "X error: " << buf << " (opcode=" << static_cast<int>(ev->request_code) << ")\n";
	return 0;
}

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: shud <config.ini> [timeout_sec]\n";
		return 1;
	}
	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) {
		std::cerr << "shud: " << (cfg.error.empty() ? "invalid config" : cfg.error) << "\n";
		return 1;
	}

	XSetErrorHandler(x_error_handler);

	stow::Dashboard dash = stow::Dashboard::from_config(cfg);
	if(argc > 2) dash.set_timeout(std::atof(argv[2]));
	dash.run();
	return 0;
}
```

This deletes shud's now-duplicated `parse_keybind`, `split_cmd`, `format_time_now`, `CellState`, `HudState`, the loop, etc. — all of it lives in `dashboard.hpp`/`widgets.hpp` now.

- [ ] **Step 2: Build shud and run bounded**

Run: `cmake --build build --target shud 2>&1 | tail -6`
Expected: `Built target shud`.

Run: `timeout 3 ./build/bin/shud test/hud_test.ini 2; echo "exit=$?"`
Expected: no `shud: invalid config`; renders ~2s; exits cleanly (the `set_timeout(2)` breaks the loop → `exit=0`) OR is killed by the outer `timeout 3` if no display differences (`exit` 0 or 124). Must NOT crash immediately. (If no X display, reports "cannot open display" — environment note, not a code failure; the build must succeed.)

- [ ] **Step 3: Create test_dashboard.cpp**

Create `test/test_dashboard.cpp`:

```cpp
// bounded integration demo: build a Dashboard from the fixture config and run briefly
#include "stow/hud_config.hpp"
#include "stow/dashboard.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
	if(argc < 2) { std::cerr << "usage: test_dashboard <config> [timeout_sec]\n"; return 1; }
	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) { std::cerr << "test_dashboard: " << cfg.error << "\n"; return 1; }
	stow::Dashboard dash = stow::Dashboard::from_config(cfg);
	dash.set_timeout(argc > 2 ? std::atof(argv[2]) : 2.0);
	dash.run();
	std::cout << "test_dashboard ok\n";
	return 0;
}
```

- [ ] **Step 4: Update CMake — add test_dashboard, remove test_grid**

In `test/CMakeLists.txt`:
- In `test_list`, replace the line `test_grid.cpp` with `test_dashboard.cpp`.
- Replace the `add_test(NAME test_grid ...)` line with:
```cmake
add_test(NAME test_dashboard   COMMAND test_dashboard ${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini 2)
```

- [ ] **Step 5: Delete the superseded test_grid**

```bash
git rm test/test_grid.cpp
```

- [ ] **Step 6: Build everything + run full suite**

Run: `cmake -S . -B build >/dev/null && cmake --build build 2>&1 | tail -3`
Expected: clean build.

Run: `(cd build && ctest --output-on-failure 2>&1 | tail -15)`
Expected: `100% tests passed`. The test count changes (test_grid removed, test_widgets + test_dashboard added → 10 tests). `test_dashboard` runs the real dashboard for 2s and exits 0.

- [ ] **Step 7: Commit**

```bash
git add src/shud.cpp test/test_dashboard.cpp test/CMakeLists.txt
git commit -m "shud: drive via Dashboard; replace test_grid with test_dashboard"
```

---

## Self-Review

**Spec coverage (Widget/Dashboard layer of the spec):**
- `Widget` base (update/render/fd/dirty) + `DashStats` + `RenderCtx` → Task 1. ✓
- `Dashboard` (add, from_config, run) owning grid/windows/loop → Task 6. ✓
- v1 widgets Command/Hud/Number/Bar → Tasks 4–5. ✓
- Data-source model (run cmd, parse last line; Number=number/text, Bar=fraction) → Tasks 2–3, 5. ✓
- Both front-ends build identical Dashboard (INI via from_config; C++ via add) → Task 6. ✓
- Per-cell `period` consumed; appearance fields (fg/bg/font/alpha/align) parsed in Plan 1 — NOTE: this plan consumes `period`, `widget`, `cmd`, `label`, `fields`, `max`. fg/bg/font/alpha/align are NOT yet applied (would require per-widget font/color state on XWindow). Flagged as a follow-up below, consistent with the spec listing them as inheritance that "this plan consumes" — partial: only structural fields are wired; appearance is deferred. Added to follow-ups.
- Plot/Tab v2 → explicitly out of scope, interface accommodates them. ✓
- `test_dashboard` replaces `test_grid`; headless value tests in `test_widgets` → Tasks 1–3, 7. ✓

**Placeholder scan:** Tasks 6 contains explicit NOTEs directing the engineer to port `parse_keybind` verbatim from `src/shud.cpp` and to add named members — these are concrete instructions referencing an existing in-repo file, not "TODO". No "TBD"/"implement later". Task 6's compile-smoke approach is concrete. Acceptable.

**Type consistency:** `Widget`/`RenderCtx`/`DashStats` names match across Tasks 1, 4, 5, 6. `number_text`/`bar_fraction` (Task 3) used by Number/Bar render (Task 5). `make_widget`/`from_config`/`add`/`run`/`set_timeout`/`_win_for`/`_timeout_sec` consistent within Task 6. `capture_command` (Task 2) used in Task 5. `ShXWindowPr`/`ShPTYProcessPr`/`pump_region`/`draw_region`/`get_color` are confirmed-existing APIs.

**Follow-ups (not blocking this plan):**
- Per-cell appearance (fg/bg/font/alpha/align) is parsed but still not applied — wiring it needs per-widget XftColor/XftFont on `XWindow` (today `draw_region` uses the window's single configured fg/font). Worth a focused follow-up once the dashboard renders.
- Plot + Tab widgets (v2).
- `dirty()` is defined but the loop always redraws; a future optimization can honor it.

**Risk note:** Tasks 4–6 are X-integration with no headless test; correctness is gated by the bounded `test_dashboard` run + manual `shud` run + code review. The value logic (Tasks 1–3) is fully headless-tested. If the `enable_shared_from_this` change in Task 5 conflicts with how `XWindow::create` constructs instances (it uses `make_shared`, which is compatible), fall back to passing `ShXWindowPr` through `RenderCtx` instead of a raw pointer — noted here so the implementer isn't blocked.
