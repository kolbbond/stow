# Stow Overlay API Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn stow into a linkable library whose public API is a lightweight click-through overlay, with X11 hidden behind a pimpl and `stow`/`shud` rebuilt as consumers of that API.

**Architecture:** Three tiers — `stow::Overlay` (one click-through window), `stow::Grid` (cells/widgets), `stow::Hud` (ini-driven dashboard). Public headers live in `include/stow/` and name no X11 type; every X11 call moves under `src/`, which is a `PRIVATE` include directory of a new static `libstow.a`. The existing `XWindow` is not rewritten — it becomes the private implementation body that `Overlay::Impl` drives.

**Tech Stack:** C++20, CMake 3.30, X11 + Xft + Xext + Xfixes + freetype, CTest. No new third-party dependencies.

**Spec:** `docs/superpowers/specs/2026-08-16-stow-overlay-api-design.md`

## Global Constraints

Every task's requirements implicitly include these.

- **Public headers (`include/stow/**`) are C++20.** No `std::expected`, no C++23 library features. `src/` may use C++23.
- **No `#include <X11/...>` anywhere under `include/`.** Task 9 adds a CI grep that enforces this; do not introduce a violation before then and do not remove one after.
- **No new third-party dependencies.** X11 + Xft + freetype only. Tests use the hand-rolled `test/check.hpp` from Task 1, not a test framework.
- **Capability loss degrades, it does not fail.** Missing Xfixes/XShape → `caps().clickthrough == false` and the window still renders. No ARGB visual → `caps().transparency == false` and the window still renders. Only "no display at all" is a construction failure. This is a hard requirement from `CLAUDE.md` (WSLg compatibility).
- **Every task ends green.** `cmake --build build && ctest --test-dir build` passes before the commit in each task's final step.
- **POSIX only.** `include/win32*.hpp` stay untouched and unbuilt.
- **Existing behavior is preserved unless a task says otherwise.** This is a refactor; `shud` and `stow` must look and behave the same at the end.

### Build and test commands

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

### Deviations from the spec, decided here

Two, both deliberate:

1. **`Color` gets its own header** (`include/stow/color.hpp`) rather than living in `overlay.hpp` as the spec's file table shows. It is pure value logic and gets headless unit tests in Task 2, before `Overlay` exists.
2. **`stow::Monitor` keeps its existing shape** (`int x, y; unsigned int width, height;`) rather than the spec's `Rect bounds`. The existing struct is already used by `test_layout.cpp` and `dashboard.cpp`; changing its field layout is churn that buys nothing.

### A note on how the X11 code moves

The spec's file table says e.g. `include/xwindow.hpp` → `src/x11/window.cpp`. In practice `XWindow` is 507 lines of **inline** member functions. Mechanically splitting every method into a `.cpp` is large, risky, and not what earns the API boundary.

What actually earns the boundary is: X11 code sitting under `src/` (a `PRIVATE` include dir) and X11 libs linked `PRIVATE`. So these files move to `src/x11/` and `src/proc/` **as headers, still inline**, and only the new public-API translation units (`overlay.cpp`, `screen.cpp`, `hud.cpp`) are true `.cpp` files. A consumer sees no X11 either way. Splitting `XWindow` into a `.cpp` later is optional cleanup, not a prerequisite.

---

### Task 1: Test harness and characterization tests

The current suite is ten demo runners that print and return 0. Before refactoring anything, get a safety net under the pure logic that the refactor will move.

**Files:**
- Create: `test/check.hpp`
- Create: `test/test_layout_unit.cpp`
- Create: `test/test_grid_unit.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (first task)
- Produces: `CHECK(expr)`, `CHECK_EQ(a, b)`, `CHECK_REPORT()` macros in `test/check.hpp`; the skip-code-77 convention for `$DISPLAY`-gated tests. Every later task's tests use these.

- [ ] **Step 1: Write the test harness header**

Create `test/check.hpp`:

```cpp
// Minimal assertion harness. No third-party test framework by design.
// Usage:
//   #include "check.hpp"
//   int main() { CHECK_EQ(2 + 2, 4); CHECK_REPORT(); }
// A test binary returns 0 when every check passed, 1 otherwise.
// Return 77 instead to tell CTest the test was skipped (see SKIP_RETURN_CODE).
#pragma once

#include <cstdio>
#include <sstream>
#include <string>

namespace check {

inline int checks = 0;
inline int failures = 0;

template <typename A, typename B>
void eq(const A& a, const B& b, const char* expr, const char* file, int line) {
	checks++;
	if(a == b) return;
	failures++;
	std::ostringstream os;
	os << file << ":" << line << ": CHECK_EQ failed: " << expr << "\n"
	   << "  actual:   " << a << "\n"
	   << "  expected: " << b << "\n";
	std::fputs(os.str().c_str(), stderr);
}

inline void is_true(bool v, const char* expr, const char* file, int line) {
	checks++;
	if(v) return;
	failures++;
	std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
}

inline int report(const char* name) {
	std::fprintf(stderr, "%s: %d checks, %d failure(s)\n", name, checks, failures);
	return failures == 0 ? 0 : 1;
}

}  // namespace check

#define CHECK_EQ(a, b) ::check::eq((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK(x) ::check::is_true(static_cast<bool>(x), #x, __FILE__, __LINE__)
#define CHECK_REPORT() return ::check::report(__FILE__)
```

- [ ] **Step 2: Write failing assertion tests for layout**

The existing `test_layout.cpp` prints rectangles and never fails. Create `test/test_layout_unit.cpp` alongside it (the demo stays; this is the one that can go red):

```cpp
// Assertions over stow::Position and WindowConfig::resolve_anchor.
// Headless: no X11, no $DISPLAY.
#include "stow/config.hpp"
#include "check.hpp"

int main() {
	// --- Position::parse ---
	stow::Position p = stow::Position::parse("10");
	CHECK_EQ(p.value, 10);
	CHECK_EQ(p.prefix, 0);
	CHECK_EQ(p.suffix, 0);

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
	CHECK_EQ(stow::Position::parse("-25%").resolve(1000, 100), 420);
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
```

- [ ] **Step 3: Write failing assertion tests for the grid**

Create `test/test_grid_unit.cpp`:

```cpp
// Assertions over stow::GridConfig / GridLayout cell arithmetic.
// Headless: no X11, no $DISPLAY.
#include "stow/grid.hpp"
#include "check.hpp"

int main() {
	// --- uniform() splits evenly and absorbs rounding into the last row/col ---
	stow::GridConfig g = stow::GridConfig::uniform(3, 3);
	CHECK_EQ(g.rows, 3);
	CHECK_EQ(g.cols, 3);
	CHECK(g.valid());
	int row_sum = 0, col_sum = 0;
	for(int h : g.row_heights) row_sum += h;
	for(int w : g.col_widths) col_sum += w;
	CHECK_EQ(row_sum, 100);
	CHECK_EQ(col_sum, 100);
	// 100/3 == 33, so the last one carries the remainder
	CHECK_EQ(g.row_heights[2], 34);

	// --- validation rejects malformed configs ---
	stow::GridConfig bad;
	bad.rows = 0;
	bad.cols = 1;
	CHECK(!bad.valid());

	stow::GridConfig mismatched = stow::GridConfig::uniform(2, 2);
	mismatched.row_heights.pop_back();
	CHECK(!mismatched.valid());

	// --- calculate() produces one rect per cell, in row-major order ---
	stow::GridLayout layout(stow::GridConfig::uniform(2, 2));
	std::vector<stow::Rect> cells = layout.calculate(1000, 800);
	CHECK_EQ(cells.size(), size_t(4));
	CHECK_EQ(cells[0].x, 0);
	CHECK_EQ(cells[0].y, 0);
	CHECK_EQ(cells[0].width, 500u);
	CHECK_EQ(cells[0].height, 400u);
	CHECK_EQ(cells[1].x, 500);
	CHECK_EQ(cells[1].y, 0);
	CHECK_EQ(cells[2].x, 0);
	CHECK_EQ(cells[2].y, 400);

	// --- cells tile the area without gaps or overlap ---
	unsigned int area = 0;
	for(const stow::Rect& r : cells) area += r.width * r.height;
	CHECK_EQ(area, 1000u * 800u);

	// --- a 1x1 grid is the whole area ---
	stow::GridLayout single(1, 1);
	std::vector<stow::Rect> one = single.calculate(640, 480);
	CHECK_EQ(one.size(), size_t(1));
	CHECK_EQ(one[0].width, 640u);
	CHECK_EQ(one[0].height, 480u);

	CHECK_REPORT();
}
```

- [ ] **Step 4: Register the new tests**

In `test/CMakeLists.txt`, add both files to `test_list`:

```cmake
set(test_list
    test_xwindow.cpp
    test_process.cpp
    test_stow.cpp
    test_fullscreen.cpp
    test_dashboard.cpp
    test_monitor.cpp
    test_layout.cpp
    test_layout_unit.cpp
    test_grid_unit.cpp
    test_hud_config.cpp
    test_clickthrough.cpp
    test_widgets.cpp
)
```

and register them:

```cmake
add_test(NAME test_layout_unit COMMAND test_layout_unit)
add_test(NAME test_grid_unit   COMMAND test_grid_unit)
```

- [ ] **Step 5: Run the tests — expect real results, not automatic green**

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build -R "unit" --output-on-failure
```

These assert current behavior, so they should PASS. **If one fails, do not edit the test to match the code** until you have read the implementation and decided which is wrong — a red result here is a genuine finding about existing behavior (the `-25%` and rounding cases are the likely candidates). Record what you found in the commit message.

- [ ] **Step 6: Gate the X11 demo tests on `$DISPLAY`**

These tests need a display and currently fail on a headless box. Make them skip instead. At the top of `main()` in `test/test_xwindow.cpp`, `test/test_fullscreen.cpp`, `test/test_clickthrough.cpp`, `test/test_dashboard.cpp`, `test/test_stow.cpp`, and `test/test_widgets.cpp`, add:

```cpp
#include <cstdlib>
// ... inside main(), as the first statements:
	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping\n");
		return 77;  // CTest SKIP_RETURN_CODE
	}
```

Then in `test/CMakeLists.txt`, after the `add_test` calls:

```cmake
set_tests_properties(
    test_xwindow test_fullscreen test_clickthrough test_dashboard test_stow test_widgets
    PROPERTIES SKIP_RETURN_CODE 77)
```

- [ ] **Step 7: Verify the skip path works**

```bash
env -u DISPLAY ctest --test-dir build --output-on-failure
```

Expected: the X11 tests report `Skipped`, the headless tests pass, overall result is success. This is the invariant every later task relies on.

- [ ] **Step 8: Commit**

```bash
git add test/check.hpp test/test_layout_unit.cpp test/test_grid_unit.cpp test/CMakeLists.txt \
        test/test_xwindow.cpp test/test_fullscreen.cpp test/test_clickthrough.cpp \
        test/test_dashboard.cpp test/test_stow.cpp test/test_widgets.cpp
git commit -m "test: assertion harness, unit tests for layout/grid, \$DISPLAY gating

Adds test/check.hpp (no third-party framework) and real assertions over
Position/anchor resolution and grid cell arithmetic, as a safety net before
the API refactor moves that code. X11 demo tests now return 77 to skip when
\$DISPLAY is unset instead of failing on a headless box."
```

---

### Task 2: `stow::Color` and `stow::ColorSpan`

Value types the overlay API needs. Pure logic, headless-testable, no X11.

**Files:**
- Create: `include/stow/color.hpp`
- Create: `include/stow/text.hpp`
- Create: `test/test_color_unit.cpp`
- Modify: `include/color_span.hpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `test/check.hpp` (Task 1)
- Produces:
  - `stow::Color{uint8_t r,g,b,a}` with `Color::parse(std::string_view) -> Color`, `Color::rgb() const -> uint32_t`, and the named helpers `Color::red()/green()/blue()/white()/black()/none()`.
  - `stow::ColorSpan{std::string text; uint32_t rgb;}` in `include/stow/text.hpp`.
  - Global `::ColorSpan` remains a type alias for `stow::ColorSpan` until Task 9, so existing code compiles untouched.

- [ ] **Step 1: Write the failing test**

Create `test/test_color_unit.cpp`:

```cpp
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

	CHECK_REPORT();
}
```

- [ ] **Step 2: Run it to confirm it fails**

Add `test_color_unit.cpp` to `test_list` and `add_test(NAME test_color_unit COMMAND test_color_unit)` in `test/CMakeLists.txt`, then:

```bash
cmake -S . -B build && cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: compile FAILS with `stow/color.hpp: No such file or directory`.

- [ ] **Step 3: Implement `stow::Color`**

Create `include/stow/color.hpp`:

```cpp
// stow::Color - RGBA color value. Pure logic; no X11.
#pragma once

#include <cstdint>
#include <string_view>

namespace stow {

struct Color {
	std::uint8_t r = 0, g = 0, b = 0, a = 255;

	constexpr bool operator==(const Color&) const = default;

	// 0xRRGGBB, alpha dropped. This is the form the Xft color cache keys on.
	constexpr std::uint32_t rgb() const {
		return (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b);
	}

	static constexpr Color from_rgb(std::uint32_t v, std::uint8_t alpha = 255) {
		return Color{std::uint8_t((v >> 16) & 0xff), std::uint8_t((v >> 8) & 0xff), std::uint8_t(v & 0xff), alpha};
	}

	// "#rrggbb", "#rrggbbaa", or the same without the leading '#'.
	// Malformed input yields opaque black. Parsing a color is not worth an
	// exception or an error channel; a visibly wrong color is a good enough signal.
	static constexpr Color parse(std::string_view s) {
		if(!s.empty() && s.front() == '#') s.remove_prefix(1);
		if(s.size() != 6 && s.size() != 8) return black();

		std::uint32_t v = 0;
		for(char ch : s) {
			int d;
			if(ch >= '0' && ch <= '9') d = ch - '0';
			else if(ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
			else if(ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
			else return black();
			v = (v << 4) | std::uint32_t(d);
		}

		if(s.size() == 6) return from_rgb(v);
		return Color{std::uint8_t((v >> 24) & 0xff), std::uint8_t((v >> 16) & 0xff), std::uint8_t((v >> 8) & 0xff),
			std::uint8_t(v & 0xff)};
	}

	static constexpr Color red() { return {255, 0, 0, 255}; }
	static constexpr Color green() { return {0, 255, 0, 255}; }
	static constexpr Color blue() { return {0, 0, 255, 255}; }
	static constexpr Color white() { return {255, 255, 255, 255}; }
	static constexpr Color black() { return {0, 0, 0, 255}; }
	static constexpr Color none() { return {0, 0, 0, 0}; }
};

}  // namespace stow
```

- [ ] **Step 4: Implement `stow::ColorSpan` and alias the old name**

Create `include/stow/text.hpp`:

```cpp
// stow::ColorSpan - a run of text sharing one color. Pure logic; no X11.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stow {

struct ColorSpan {
	std::string text;
	std::uint32_t rgb = 0;
};

// One rendered line is a sequence of spans; a frame is a sequence of lines.
using Line = std::vector<ColorSpan>;
using Lines = std::vector<Line>;

}  // namespace stow
```

Replace the body of `include/color_span.hpp` so existing code keeps compiling:

```cpp
// ColorSpan - platform-independent colored text span.
// Deprecated: the type now lives in <stow/text.hpp> as stow::ColorSpan.
// This alias keeps pre-namespace code compiling; removed in the cleanup task.
#pragma once

#include "stow/text.hpp"

using ColorSpan = stow::ColorSpan;
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure
```

Expected: `test_color_unit` PASSES, and every previously-passing test still passes — `ColorSpan` is now an alias, so `xwindow.hpp`, `ptyprocess.hpp` and the widgets must all still build unchanged. If anything broke, the alias is wrong; fix the alias, not the callers.

- [ ] **Step 6: Commit**

```bash
git add include/stow/color.hpp include/stow/text.hpp include/color_span.hpp \
        test/test_color_unit.cpp test/CMakeLists.txt
git commit -m "feat: stow::Color and stow::ColorSpan value types

Adds the public value types the overlay API draws with. ColorSpan moves into
namespace stow; the old global name stays as an alias so existing code is
untouched. Color::parse degrades to black on malformed input rather than
throwing."
```

---

### Task 3: `stow::Overlay` — the static library and the public window

The core of the plan. Creates `libstow.a`, moves X11 under `src/`, and puts `Overlay` in front of `XWindow`.

**Files:**
- Create: `include/stow/error.hpp`
- Create: `include/stow/overlay.hpp`
- Create: `src/overlay.cpp`
- Create: `test/test_overlay.cpp`
- Move: `include/xwindow.hpp` → `src/x11/window.hpp`
- Move: `include/stow/monitor.hpp` → `src/x11/monitor.hpp`
- Move: `include/{ptyprocess,pipeprocess,screen_buffer,process}.hpp` → `src/proc/`
- Move: `include/{error,platform}.hpp` → `src/legacy_error.hpp`, `src/platform.hpp`
- Modify: `CMakeLists.txt`, `test/CMakeLists.txt`
- Modify: every file that includes a moved header (see Step 4)

**Interfaces:**
- Consumes: `stow::Color`, `stow::ColorSpan` (Task 2); `stow::WindowConfig`, `stow::Anchor`, `stow::Position` (existing `include/stow/config.hpp`); `stow::Rect` (existing `include/stow/layout.hpp`)
- Produces:
  - CMake target `stow` (STATIC) with alias `stow::stow`. `include/` is `PUBLIC`, `src/` is `PRIVATE`, X11 libs are `PRIVATE`.
  - `stow::Error{Code code; std::string message;}` with `Code::None|NoDisplay|NoFont|NoColor`.
  - `stow::Size{unsigned w, h;}`, `stow::Caps{bool clickthrough, transparency, override_redirect;}`, `stow::OverlayConfig` (fields per the spec).
  - `stow::Overlay` with: `static std::optional<Overlay> create(const OverlayConfig&, Error* err)`, explicit throwing ctor, move-only, `show/hide/close/open/caps/set_text/set_spans/pump()`.
  - Drawing (`begin/rect/text/end`), geometry (`move_to/resize/geometry`), `pump(timeout)` and `run()` arrive in Task 5.

- [ ] **Step 1: Write the failing test**

Create `test/test_overlay.cpp`:

```cpp
// stow::Overlay lifecycle. Needs $DISPLAY; skips (77) without one.
#include "stow/overlay.hpp"
#include "check.hpp"

#include <cstdlib>

int main() {
	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping\n");
		return 77;
	}

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

	// Capabilities are reported, never asserted true: on WSLg or a
	// compositor without XShape/ARGB these are legitimately false and the
	// overlay must still work. We assert only that querying is safe.
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
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cmake -S . -B build && cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: compile FAILS with `stow/overlay.hpp: No such file or directory`.

- [ ] **Step 3: Move the X11 and process headers under `src/`**

```bash
mkdir -p src/x11 src/proc
git mv include/xwindow.hpp        src/x11/window.hpp
git mv include/stow/monitor.hpp   src/x11/monitor.hpp
git mv include/ptyprocess.hpp     src/proc/ptyprocess.hpp
git mv include/pipeprocess.hpp    src/proc/pipeprocess.hpp
git mv include/process.hpp        src/proc/process.hpp
git mv include/screen_buffer.hpp  src/proc/screen_buffer.hpp
git mv include/platform.hpp       src/platform.hpp
git mv include/error.hpp          src/legacy_error.hpp
```

`include/window.hpp` stays for now — `src/x11/window.hpp` still inherits `StowWindow`. It is deleted in Task 9. Move it too so it is out of the public tree:

```bash
git mv include/window.hpp src/x11/stow_window.hpp
```

- [ ] **Step 4: Fix the includes in the moved files and their users**

Paths that changed, and every place referencing them:

| In file | Old include | New include |
|---|---|---|
| `src/x11/window.hpp` | `"platform.hpp"` | `"platform.hpp"` (unchanged — same dir now) |
| `src/x11/window.hpp` | `"error.hpp"` | `"legacy_error.hpp"` |
| `src/x11/window.hpp` | `"window.hpp"` | `"x11/stow_window.hpp"` |
| `src/x11/window.hpp` | `"stow/config.hpp"` | unchanged (public, still on the include path) |
| `src/x11/stow_window.hpp` | `"platform.hpp"`, `"color_span.hpp"` | `"platform.hpp"`, `"stow/text.hpp"` |
| `src/proc/*.hpp` | `"error.hpp"`, `"platform.hpp"`, `"color_span.hpp"`, `"screen_buffer.hpp"` | `"legacy_error.hpp"`, `"platform.hpp"`, `"stow/text.hpp"`, `"proc/screen_buffer.hpp"` |
| `include/stow/dashboard.hpp` | `"xwindow.hpp"`, `"monitor.hpp"` | `"x11/window.hpp"`, `"x11/monitor.hpp"` |
| `include/stow/widgets.hpp` | `"ptyprocess.hpp"` etc. | `"proc/ptyprocess.hpp"` |
| `src/stow.cpp` | `"ptyprocess.hpp"`, `"pipeprocess.hpp"`, `"xwindow.hpp"`, `"stow/monitor.hpp"` | `"proc/ptyprocess.hpp"`, `"proc/pipeprocess.hpp"`, `"x11/window.hpp"`, `"x11/monitor.hpp"` |
| `test/*.cpp` | any of the above | same rewrites |

Find every one:

```bash
grep -rn 'include "\(xwindow\|ptyprocess\|pipeprocess\|process\|screen_buffer\|error\|platform\|window\|color_span\)\.hpp"' \
     include/ src/ test/
grep -rn 'stow/monitor\.hpp' include/ src/ test/
```

`include/stow/dashboard.hpp` and `include/stow/widgets.hpp` now include X11 headers that live under `src/`. That is temporary and correct for this step: they still compile because `src/` is on the `PRIVATE` include path of the library, and they move out of `include/` in Task 7. **Until then, only `libstow` and the test binaries can include them.**

- [ ] **Step 5: Implement `stow::Error`**

Create `include/stow/error.hpp`:

```cpp
// stow::Error - failure reason from library construction. No X11.
#pragma once

#include <string>

namespace stow {

struct Error {
	enum class Code {
		None = 0,
		NoDisplay,  // could not open an X display; the one true failure
		NoFont,     // requested font could not be loaded
		NoColor,    // a color could not be allocated
	};

	Code code = Code::None;
	std::string message;

	explicit operator bool() const { return code != Code::None; }
};

}  // namespace stow
```

- [ ] **Step 6: Implement the public overlay header**

Create `include/stow/overlay.hpp`. This header must not name a single X11 type:

```cpp
// stow::Overlay - a lightweight click-through overlay window.
//
//   #include <stow/overlay.hpp>
//   stow::Overlay ov({.anchor = stow::Anchor::TopRight});
//   ov.show();
//   while (ov.pump()) ov.set_text(status());
//
// The platform (X11) is entirely behind the pimpl; this header names no X11 type.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "stow/color.hpp"
#include "stow/config.hpp"  // Anchor, Position
#include "stow/error.hpp"
#include "stow/layout.hpp"  // Rect
#include "stow/text.hpp"    // ColorSpan, Lines

namespace stow {

struct Size {
	unsigned int w = 0, h = 0;
	constexpr bool operator==(const Size&) const = default;
	constexpr bool empty() const { return w == 0 || h == 0; }
};

// What the compositor actually granted. Never a reason to fail construction:
// a WSLg or XWayland session may deny any of these and must still render.
struct Caps {
	bool clickthrough = false;
	bool transparency = false;
	bool override_redirect = false;
};

struct OverlayConfig {
	Anchor anchor = Anchor::Custom;
	Position x, y;              // used when anchor == Custom
	Size size;                  // empty => size to content
	int monitor = -1;           // -1 => primary
	std::string font = "monospace:size=10";
	Color fg = Color::parse("#00a080");
	Color bg = Color::parse("#000000");
	double alpha = 0.8;
	bool clickthrough = true;
	bool on_top = true;
	bool borderless = true;
	int border_px = 2;          // ignored when borderless
	char align = 'l';           // 'l' | 'r' | 'c'
	std::string title = "stow";
};

class Overlay {
public:
	// Returns nullopt only when there is no usable display; *err then holds
	// the reason. Losing click-through or transparency is reported through
	// caps(), not through failure.
	static std::optional<Overlay> create(const OverlayConfig& cfg = {}, Error* err = nullptr);

	// Same, but throws stow::Error instead of returning nullopt.
	explicit Overlay(const OverlayConfig& cfg = {});

	Overlay(Overlay&&) noexcept;
	Overlay& operator=(Overlay&&) noexcept;
	Overlay(const Overlay&) = delete;
	Overlay& operator=(const Overlay&) = delete;
	~Overlay();

	// Lifecycle
	void show();
	void hide();
	void close();
	bool open() const;  // shown and not closed
	Caps caps() const;

	// Content
	void set_text(std::string_view text);
	void set_spans(const Lines& lines);

	// Services X events and presents. Returns false once closed.
	bool pump();

private:
	struct Impl;
	std::unique_ptr<Impl> _p;

	explicit Overlay(std::unique_ptr<Impl> p);
};

}  // namespace stow
```

- [ ] **Step 7: Implement the pimpl**

Create `src/overlay.cpp`. It drives the existing `XWindow` rather than reimplementing it:

```cpp
#include "stow/overlay.hpp"

#include "x11/window.hpp"

#include <X11/extensions/shape.h>

#include <sstream>

namespace stow {

struct Overlay::Impl {
	std::shared_ptr<XWindow> win;
	OverlayConfig cfg;
	Caps caps;
	bool shown = false;
	bool closed = false;
};

namespace {

// Map the public config onto the WindowConfig XWindow already understands.
WindowConfig to_window_config(const OverlayConfig& c) {
	WindowConfig w;
	w.anchor = c.anchor;
	w.px = c.x;
	w.py = c.y;
	w.monitor = c.monitor;
	w.font = c.font;
	w.alpha = c.alpha;
	w.align = c.align;
	w.border_px = c.border_px;
	w.borderless = c.borderless;
	w.overlay = c.clickthrough;
	w.on_top = c.on_top;
	w.title = c.title;

	// stow::Color -> the "#rrggbb" strings XftColorAllocName wants.
	char buf[8];
	std::snprintf(buf, sizeof(buf), "#%06x", c.fg.rgb());
	w.fg = buf;
	std::snprintf(buf, sizeof(buf), "#%06x", c.bg.rgb());
	w.bg = buf;

	// A non-empty size means fixed geometry; empty means size-to-content.
	if(!c.size.empty()) {
		w.use_fixed_geometry = true;
		w.fixed_w = c.size.w;
		w.fixed_h = c.size.h;
	}
	return w;
}

Caps probe_caps(XWindow& w, const OverlayConfig& cfg) {
	Caps caps;
	caps.transparency = (w._depth == 32);
	caps.override_redirect = w._override_redirect;

	int event_base = 0, error_base = 0;
	caps.clickthrough = cfg.clickthrough && XShapeQueryExtension(w._dpy, &event_base, &error_base);
	return caps;
}

}  // namespace

std::optional<Overlay> Overlay::create(const OverlayConfig& cfg, Error* err) {
	if(err) *err = Error{};

	// XWindow::setup() calls Error::die() on failure, which exits the process.
	// Probe the display first so a headless caller gets an error instead of
	// an exit. This is the one failure mode the library reports.
	Display* probe = XOpenDisplay(nullptr);
	if(!probe) {
		if(err) *err = Error{Error::Code::NoDisplay, "cannot open X display"};
		return std::nullopt;
	}
	XCloseDisplay(probe);

	auto impl = std::make_unique<Impl>();
	impl->cfg = cfg;
	impl->win = XWindow::create(to_window_config(cfg));
	impl->win->setup();
	impl->caps = probe_caps(*impl->win, cfg);

	return Overlay(std::move(impl));
}

Overlay::Overlay(const OverlayConfig& cfg) {
	Error err;
	std::optional<Overlay> ov = create(cfg, &err);
	if(!ov) throw err;
	_p = std::move(ov->_p);
}

Overlay::Overlay(std::unique_ptr<Impl> p) : _p(std::move(p)) {}
Overlay::Overlay(Overlay&&) noexcept = default;
Overlay& Overlay::operator=(Overlay&&) noexcept = default;
Overlay::~Overlay() = default;

void Overlay::show() {
	_p->shown = true;
	_p->win->_hidden = false;
	_p->win->_dirty = true;
}

void Overlay::hide() {
	_p->shown = false;
	_p->win->_hidden = true;
	_p->win->run();  // unmaps
}

void Overlay::close() {
	_p->shown = false;
	_p->closed = true;
}

bool Overlay::open() const { return _p->shown && !_p->closed; }

Caps Overlay::caps() const { return _p->caps; }

void Overlay::set_text(std::string_view text) {
	if(_p->closed) return;
	_p->win->draw(std::string(text));
	_p->win->_dirty = true;
}

void Overlay::set_spans(const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_spans(lines);
	_p->win->_dirty = true;
}

bool Overlay::pump() {
	if(_p->closed) return false;

	// Drain pending X events without blocking.
	while(XPending(_p->win->_dpy)) {
		XEvent ev;
		XNextEvent(_p->win->_dpy, &ev);
		if(ev.type == Expose) _p->win->_dirty = true;
	}

	if(_p->shown) _p->win->run();
	return true;
}

}  // namespace stow
```

- [ ] **Step 8: Convert `stowlib` into the static library**

In `CMakeLists.txt`, replace the `add_library(stowlib INTERFACE)` block and everything from it down to the `stow`/`shud` executables with:

```cmake
# stow library - public API in include/, platform code private in src/
add_library(stow STATIC
    src/overlay.cpp
)
add_library(stow::stow ALIAS stow)

target_include_directories(stow
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${FREETYPE_INCLUDE_DIRS}
)

# PRIVATE is the point: consumers link `stow` and inherit no X11.
target_link_libraries(stow PRIVATE ${STOW_PLATFORM_LIBS})
if(X11_Xrandr_FOUND)
    target_link_libraries(stow PRIVATE ${X11_Xrandr_LIB})
endif()
if(X11_Xinerama_FOUND)
    target_link_libraries(stow PRIVATE ${X11_Xinerama_LIB})
endif()
target_compile_definitions(stow PRIVATE ${STOW_COMPILE_DEFINITIONS})

# Transitional: stow and shud still include X11 headers directly, so they
# need the private include dir and the X11 libs. Task 7 and Task 8 port them
# onto the public API and this target goes away.
add_library(stowlegacy INTERFACE)
target_include_directories(stowlegacy INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${FREETYPE_INCLUDE_DIRS}
)
target_link_libraries(stowlegacy INTERFACE ${STOW_PLATFORM_LIBS})
if(X11_Xrandr_FOUND)
    target_link_libraries(stowlegacy INTERFACE ${X11_Xrandr_LIB})
endif()
if(X11_Xinerama_FOUND)
    target_link_libraries(stowlegacy INTERFACE ${X11_Xinerama_LIB})
endif()
target_compile_definitions(stowlegacy INTERFACE ${STOW_COMPILE_DEFINITIONS})

# executables
add_executable(stow_bin src/stow.cpp)
set_target_properties(stow_bin PROPERTIES OUTPUT_NAME stow)
target_link_libraries(stow_bin PRIVATE stowlegacy)

add_executable(shud src/shud.cpp)
target_link_libraries(shud PRIVATE stowlegacy)
```

The executable target is renamed `stow_bin` because the library now owns the name `stow`; `OUTPUT_NAME` keeps the binary called `stow`. Update the install line:

```cmake
install(TARGETS stow_bin shud DESTINATION bin)
```

- [ ] **Step 9: Point the tests at the right targets**

In `test/CMakeLists.txt`, replace `stowlib` with `stowlegacy` in the `foreach` loop, then link the new API test against the real library instead:

```cmake
    target_link_libraries(${name} stowlegacy)
```

and after the loop:

```cmake
# test_overlay is a consumer of the public API only - it must build with
# nothing but the installed headers and libstow.a. If it needs stowlegacy,
# the API has a hole.
target_link_libraries(test_overlay PRIVATE stow::stow)
```

Add `test_overlay.cpp` to `test_list`, and:

```cmake
add_test(NAME test_overlay COMMAND test_overlay)
set_tests_properties(test_overlay PROPERTIES SKIP_RETURN_CODE 77)
```

Because `test_overlay` links only `stow::stow` and not `stowlegacy`, it gets no X11 include path — which is exactly the property being tested. Remove it from the generic `foreach` list if the loop would also link `stowlegacy` to it; give it its own `add_executable` if simpler.

- [ ] **Step 10: Build and run**

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: everything builds; `test_overlay` PASSES under a display and reports `Skipped` without one. The `caps:` line it prints on this machine (Hyprland/XWayland) is worth reading — record the values in the commit message.

- [ ] **Step 11: Verify the boundary actually holds**

This is the real acceptance check for the task — compile a consumer that has no X11 anywhere near it:

```bash
cat > /tmp/consumer_check.cpp <<'EOF'
#include <stow/overlay.hpp>
int main() { stow::OverlayConfig c; c.title = "x"; return int(c.alpha); }
EOF
g++ -std=c++20 -Iinclude -fsyntax-only /tmp/consumer_check.cpp && echo "BOUNDARY OK"
```

Expected: `BOUNDARY OK`. No `-I` for X11, no freetype path, no link step. If this fails, a public header is still pulling in X11 and the task is not done.

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "feat: stow::Overlay and libstow.a

Converts the INTERFACE target into a static library whose public API is
include/stow/ and whose X11 implementation is private under src/. X11 libs
are now PRIVATE, so a consumer links stow::stow and inherits no X11.

Overlay wraps the existing XWindow through a pimpl - the window code is moved,
not rewritten. Capability loss (no XShape, no ARGB) is reported via caps()
rather than failing, per the WSLg requirement.

stow/shud still include X11 directly and build against a transitional
stowlegacy target; they are ported in later tasks."
```

---

### Task 4: `stow::screen` — pointer and monitor queries

`XQueryPointer` is currently trapped inside `Dashboard`'s loop (`dashboard.cpp:239`), unreachable from Tier 1. The cursor demo in Task 6 needs it.

**Files:**
- Create: `include/stow/screen.hpp`
- Create: `src/x11/screen.cpp`
- Create: `test/test_screen.cpp`
- Modify: `CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `stow::Monitor` (existing, now at `src/x11/monitor.hpp`), `stow::Rect`
- Produces: `stow::Point{int x, y;}`; free functions `stow::pointer() -> Point`, `stow::monitors() -> std::vector<Monitor>`, `stow::primary_monitor() -> Monitor`, `stow::screen_size() -> Size`. `stow::Monitor` becomes a **public** type declared in `include/stow/screen.hpp` with its existing field layout.

- [ ] **Step 1: Write the failing test**

Create `test/test_screen.cpp`:

```cpp
// stow screen queries. Needs $DISPLAY; skips (77) without one.
#include "stow/screen.hpp"
#include "check.hpp"

#include <cstdlib>

int main() {
	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping\n");
		return 77;
	}

	// --- screen_size is non-degenerate ---
	stow::Size s = stow::screen_size();
	CHECK(s.w > 0);
	CHECK(s.h > 0);

	// --- at least one monitor, indices are dense and ordered ---
	std::vector<stow::Monitor> mons = stow::monitors();
	CHECK(!mons.empty());
	for(size_t i = 0; i < mons.size(); i++) {
		CHECK_EQ(mons[i].index, int(i));
		CHECK(mons[i].width > 0);
		CHECK(mons[i].height > 0);
	}

	// --- exactly one primary, and primary_monitor() agrees with it ---
	int primaries = 0;
	for(const stow::Monitor& m : mons)
		if(m.primary) primaries++;
	CHECK_EQ(primaries, 1);
	CHECK(stow::primary_monitor().primary);

	// --- the pointer is somewhere on the desktop ---
	stow::Point p = stow::pointer();
	CHECK(p.x >= 0);
	CHECK(p.y >= 0);
	CHECK(p.x < int(s.w));
	CHECK(p.y < int(s.h));

	// --- and it falls inside some monitor ---
	bool inside = false;
	for(const stow::Monitor& m : mons)
		if(m.contains(p.x, p.y)) inside = true;
	CHECK(inside);

	// --- repeated calls are stable and do not leak a display connection ---
	for(int i = 0; i < 200; i++) (void)stow::pointer();
	CHECK(true);  // reaching here without EMFILE is the assertion

	CHECK_REPORT();
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: FAILS with `stow/screen.hpp: No such file or directory`.

- [ ] **Step 3: Write the public header**

Create `include/stow/screen.hpp`:

```cpp
// Screen queries: pointer position and monitor geometry.
// These are facts about the display, not properties of a window, so they are
// free functions rather than Overlay methods. No X11 in this header.
#pragma once

#include <string>
#include <vector>

#include "stow/overlay.hpp"  // Size

namespace stow {

struct Point {
	int x = 0, y = 0;
	constexpr bool operator==(const Point&) const = default;
};

struct Monitor {
	int index = 0;
	std::string name;
	int x = 0;
	int y = 0;
	unsigned int width = 0;
	unsigned int height = 0;
	bool primary = false;

	bool contains(int px, int py) const {
		return px >= x && px < x + int(width) && py >= y && py < y + int(height);
	}
};

// Global cursor position, in root-window coordinates.
Point pointer();

// All monitors, index-ordered. Never empty on a working display: falls back to
// a single monitor covering the whole screen when neither XRandR nor Xinerama
// is available.
std::vector<Monitor> monitors();

// The monitor flagged primary, or index 0 if none is.
Monitor primary_monitor();

// Total desktop size across all monitors.
Size screen_size();

}  // namespace stow
```

`stow::Monitor` is now declared here. Delete the duplicate definition from `src/x11/monitor.hpp` and have that file `#include "stow/screen.hpp"` instead, so `MonitorManager` keeps working on the same type. Its `refresh()` / `at()` logic is unchanged.

- [ ] **Step 4: Implement the queries**

Create `src/x11/screen.cpp`:

```cpp
#include "stow/screen.hpp"

#include "x11/monitor.hpp"

#include <X11/Xlib.h>

namespace stow {
namespace {

// One process-wide display connection for queries. Opening a display per call
// exhausts file descriptors under a per-frame caller like the cursor demo.
// Closed at exit by the OS; nothing here owns a window.
Display* query_display() {
	static Display* dpy = XOpenDisplay(nullptr);
	return dpy;
}

}  // namespace

Point pointer() {
	Display* dpy = query_display();
	if(!dpy) return {};

	Window root = DefaultRootWindow(dpy);
	Window ret_root = 0, ret_child = 0;
	int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
	unsigned int mask = 0;

	if(!XQueryPointer(dpy, root, &ret_root, &ret_child, &root_x, &root_y, &win_x, &win_y, &mask)) return {};
	return Point{root_x, root_y};
}

std::vector<Monitor> monitors() {
	Display* dpy = query_display();
	if(!dpy) return {};

	auto mgr = MonitorManager::create(dpy);
	std::vector<Monitor> out = mgr->all();

	if(out.empty()) {
		Monitor m;
		m.index = 0;
		m.name = "screen";
		m.width = DisplayWidth(dpy, DefaultScreen(dpy));
		m.height = DisplayHeight(dpy, DefaultScreen(dpy));
		m.primary = true;
		out.push_back(m);
	}

	// Guarantee the ordering and primary invariants the API promises.
	for(size_t i = 0; i < out.size(); i++) out[i].index = int(i);
	bool any_primary = false;
	for(const Monitor& m : out) any_primary = any_primary || m.primary;
	if(!any_primary) out[0].primary = true;

	return out;
}

Monitor primary_monitor() {
	std::vector<Monitor> mons = monitors();
	if(mons.empty()) return {};
	for(const Monitor& m : mons)
		if(m.primary) return m;
	return mons[0];
}

Size screen_size() {
	Display* dpy = query_display();
	if(!dpy) return {};
	int s = DefaultScreen(dpy);
	return Size{static_cast<unsigned int>(DisplayWidth(dpy, s)), static_cast<unsigned int>(DisplayHeight(dpy, s))};
}

}  // namespace stow
```

If `MonitorManager` has no `all()` accessor returning `std::vector<Monitor>`, add one returning `_monitors` by value.

- [ ] **Step 5: Add the source and the test**

In `CMakeLists.txt`, extend the library sources:

```cmake
add_library(stow STATIC
    src/overlay.cpp
    src/x11/screen.cpp
)
```

In `test/CMakeLists.txt`, give `test_screen` the same public-API-only treatment as `test_overlay` (link `stow::stow`, not `stowlegacy`), then:

```cmake
add_test(NAME test_screen COMMAND test_screen)
set_tests_properties(test_screen PROPERTIES SKIP_RETURN_CODE 77)
```

- [ ] **Step 6: Run to verify it passes**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R test_screen --output-on-failure
```

Expected: PASS. If the "pointer falls inside some monitor" check fails on Hyprland/XWayland, **do not delete the check** — it is the first sign of the coordinate-space risk the spec flagged. Report it, and note the actual pointer and monitor values in the commit message.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: stow::pointer(), monitors(), screen_size()

Lifts XQueryPointer out of Dashboard's render loop into a public free-function
API, and promotes stow::Monitor to a public type. Query calls share one
process-wide display connection so a per-frame caller does not exhaust fds."
```

---

### Task 5: Immediate-mode drawing and runtime geometry

`Overlay` can currently only replace its whole content. The cursor demo needs to draw a box, move the window every frame, and pace itself.

**Files:**
- Modify: `include/stow/overlay.hpp`
- Modify: `src/overlay.cpp`
- Modify: `src/x11/window.hpp`
- Modify: `test/test_overlay.cpp`

**Interfaces:**
- Consumes: everything from Task 3
- Produces, added to `stow::Overlay`:
  - `void begin()`, `void rect(Rect, Color, int thickness = 0)`, `void text(int x, int y, std::string_view, Color = {})`, `void spans(const Lines&)`, `void end()`
  - `void move_to(int x, int y)`, `void resize(Size)`, `Rect geometry() const`
  - `bool pump(std::chrono::milliseconds timeout)`
  - `void run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb)`
  - New on `XWindow` (private): `void fill_rect(int,int,unsigned,unsigned,unsigned int rgb)`, `void draw_rect_outline(int,int,unsigned,unsigned,unsigned int rgb,int thickness)`, `void draw_text_at(int,int,const std::string&,unsigned int rgb)`, `void present()`, `void set_geometry(int,int,unsigned,unsigned)`

- [ ] **Step 1: Write the failing test**

Append to `test/test_overlay.cpp`, before `CHECK_REPORT()` (and re-open an overlay, since the earlier block closes one):

```cpp
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
	draw_ov.rect({0, 0, 240, 100}, stow::Color::red(), 2);       // outline
	draw_ov.rect({10, 10, 40, 20}, stow::Color::blue(), 0);      // filled
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
```

Add `#include <chrono>` at the top of the test.

- [ ] **Step 2: Run it to confirm it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: FAILS — `'begin' is not a member of 'stow::Overlay'`.

- [ ] **Step 3: Add the drawing primitives to `XWindow`**

In `src/x11/window.hpp`, add these public methods. They reuse the existing `_xgc`, `_drawable`, `get_color()` and `_xfont`:

```cpp
	// --- immediate-mode primitives (used by stow::Overlay) ---

	// Clear the backing pixmap to the (possibly transparent) background.
	void begin_frame() {
		clear_drawable();
	}

	void fill_rect(int x, int y, unsigned int w, unsigned int h, unsigned int rgb) {
		XSetForeground(_dpy, _xgc, get_color(rgb)->pixel);
		XFillRectangle(_dpy, _drawable, _xgc, x, y, w, h);
	}

	void draw_rect_outline(int x, int y, unsigned int w, unsigned int h, unsigned int rgb, int thickness) {
		if(thickness < 1) thickness = 1;
		XSetForeground(_dpy, _xgc, get_color(rgb)->pixel);
		for(int i = 0; i < thickness; i++) {
			if(w <= unsigned(2 * i) || h <= unsigned(2 * i)) break;
			XDrawRectangle(_dpy, _drawable, _xgc, x + i, y + i, w - 2 * i - 1, h - 2 * i - 1);
		}
	}

	// y is the text baseline offset from the top of the requested position,
	// matching the rest of the drawing code (ascent is added internally).
	void draw_text_at(int x, int y, const std::string& s, unsigned int rgb) {
		XftColor* c = get_color(rgb);
		XftDrawStringUtf8(_xdraw, c, _xfont, x, y, reinterpret_cast<const unsigned char*>(s.c_str()), s.size());
	}

	// Explicit geometry, bypassing content measurement.
	void set_geometry(int x, int y, unsigned int w, unsigned int h) {
		unsigned int prev_w = _window_width;
		unsigned int prev_h = _window_height;
		_use_fixed_geometry = true;
		_fixed_x = x;
		_fixed_y = y;
		_fixed_w = w;
		_fixed_h = h;
		_window_width = w;
		_window_height = h;
		_hidden = false;
		resize_drawable_if_needed(prev_w, prev_h);
		_dirty = true;
	}
```

`resize_drawable_if_needed` and `clear_drawable` are currently `private`; move them (and only them) above the `private:` marker, or make `stow::Overlay::Impl` a friend. Prefer moving them — a friend declaration on a class that is about to be an implementation detail is not worth the coupling.

- [ ] **Step 4: Add the public API**

In `include/stow/overlay.hpp`, add to `class Overlay` (and `#include <chrono>`, `#include <functional>`):

```cpp
	// --- geometry (runtime) ---
	void move_to(int x, int y);
	void resize(Size s);
	Rect geometry() const;

	// --- immediate-mode drawing ---
	// begin() clears the frame, end() presents it. Draw calls between the two
	// are painted in issue order.
	void begin();
	void rect(Rect r, Color c, int thickness = 0);  // thickness 0 => filled
	void text(int x, int y, std::string_view s, Color c = Color::white());
	void spans(const Lines& lines);
	void end();

	// --- loop ---
	bool pump();                                        // non-blocking
	bool pump(std::chrono::milliseconds timeout);       // waits up to timeout on X events
	void run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb);
```

- [ ] **Step 5: Implement them**

In `src/overlay.cpp`, add `#include <poll.h>` and:

```cpp
void Overlay::move_to(int x, int y) {
	Rect g = geometry();
	_p->win->set_geometry(x, y, g.width, g.height);
}

void Overlay::resize(Size s) {
	Rect g = geometry();
	_p->win->set_geometry(g.x, g.y, s.w, s.h);
}

Rect Overlay::geometry() const {
	Rect r;
	r.x = _p->win->_fixed_x;
	r.y = _p->win->_fixed_y;
	r.width = _p->win->_window_width;
	r.height = _p->win->_window_height;
	return r;
}

void Overlay::begin() {
	if(_p->closed) return;
	_p->win->begin_frame();
}

void Overlay::rect(Rect r, Color c, int thickness) {
	if(_p->closed) return;
	if(thickness <= 0)
		_p->win->fill_rect(r.x, r.y, r.width, r.height, c.rgb());
	else
		_p->win->draw_rect_outline(r.x, r.y, r.width, r.height, c.rgb(), thickness);
}

void Overlay::text(int x, int y, std::string_view s, Color c) {
	if(_p->closed) return;
	_p->win->draw_text_at(x, y, std::string(s), c.rgb());
}

void Overlay::spans(const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_spans(lines);
}

void Overlay::end() {
	if(_p->closed) return;
	_p->win->_dirty = true;
}

bool Overlay::pump(std::chrono::milliseconds timeout) {
	if(_p->closed) return false;

	// Block on the X connection until an event arrives or the timeout expires.
	// This is what gives a caller a frame rate without a busy-spin.
	if(!XPending(_p->win->_dpy)) {
		struct pollfd pfd = {_p->win->_xfd, POLLIN, 0};
		::poll(&pfd, 1, int(timeout.count()));
	}
	return pump();
}

void Overlay::run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb) {
	while(!_p->closed) {
		if(cb) cb(*this);
		if(_p->closed) break;
		if(!pump(period)) break;
	}
}
```

- [ ] **Step 6: Re-apply click-through after geometry changes**

`XWindow::run()` already re-applies `XShapeCombineRectangles` after `XMoveResizeWindow` (`src/x11/window.hpp`, in `run()`), which is the XWayland workaround recorded in the project memory. Confirm that path is still taken when `_use_fixed_geometry` is true — read the `run()` body and check the `if(_overlay)` re-apply is *after* the move/resize and not inside a branch that fixed geometry skips. If it is skipped, move it so it always runs after `XMoveResizeWindow`.

This is why `test_overlay` asserts `caps().clickthrough` is unchanged across a `resize()`.

- [ ] **Step 7: Run to verify it passes**

```bash
cmake --build build -j$(nproc) && ctest --test-dir build -R test_overlay --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: immediate-mode drawing, runtime geometry, pump(timeout)

Adds begin/rect/text/end, move_to/resize/geometry, a blocking pump(timeout)
built on poll() over the X connection, and a run() convenience loop. Drawing
primitives land on XWindow and are exposed only through Overlay."
```

---

### Task 6: The cursor demo

The spec's acceptance example: a red box that follows the pointer and prints its coordinates. It is both the Tier 1 demo and the click-through regression check.

**Files:**
- Create: `test/test_cursor.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `stow::Overlay` with drawing + geometry (Task 5), `stow::pointer()` (Task 4)
- Produces: nothing consumed by later tasks; this is a leaf.

- [ ] **Step 1: Write the demo**

Create `test/test_cursor.cpp`:

```cpp
// A red box that follows the cursor and shows its coordinates.
//
// This is the spec's acceptance example for the Tier 1 API, and it doubles as
// the click-through regression check: the box tracks the pointer, so if the
// window swallowed input you could not click anything underneath it.
//
// Runs for `duration_sec` (default 3) so it can live in ctest. Needs $DISPLAY.
#include "stow/overlay.hpp"
#include "stow/screen.hpp"
#include "check.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping\n");
		return 77;
	}

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
		// Not a failure: a compositor may legitimately deny it, and the
		// overlay must still render. But it is worth being loud about.
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
		if(last.x >= 0 && p != last) ever_moved = true;
		last = p;

		// Offset so the box sits below-right of the cursor, not under it.
		ov->move_to(p.x + 12, p.y + 12);

		ov->begin();
		ov->rect({0, 0, kW, kH}, stow::Color::red(), 2);
		ov->text(6, 24, std::to_string(p.x) + "," + std::to_string(p.y), stow::Color::white());
		ov->end();

		frames++;
	}

	std::fprintf(stderr, "%d frames in %.1fs; pointer moved: %s\n", frames, duration_sec, ever_moved ? "yes" : "no");

	// The overlay rendered continuously for the whole window.
	CHECK(frames > 10);
	CHECK(ov->open());

	CHECK_REPORT();
}
```

- [ ] **Step 2: Register it**

In `test/CMakeLists.txt`, give `test_cursor` the public-API-only linkage (`stow::stow`, not `stowlegacy`) as with `test_overlay`, then:

```cmake
add_test(NAME test_cursor COMMAND test_cursor 3)
set_tests_properties(test_cursor PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
```

- [ ] **Step 3: Run it and watch the screen**

```bash
cmake --build build -j$(nproc) && ./build/bin/test_cursor 10
```

Move the mouse during the ten seconds. Expected: a red-outlined box with live coordinates trailing the cursor, and clicks landing on whatever is underneath, not on the box.

**Record what actually happens on Hyprland/XWayland.** The spec flagged this as the likely-fragile path. Three outcomes, all reportable, none of which change the API:
- Box tracks correctly → the overlay path works on this compositor; say so.
- Box tracks with an offset or lag → note the observed offset and whether `stow::pointer()` or `move_to()` is the one disagreeing (compare the printed coordinates against `xdotool getmouselocation`).
- Box does not follow at all → report it; do not add compositor-specific workarounds in this task.

- [ ] **Step 4: Run under ctest**

```bash
ctest --test-dir build -R test_cursor --output-on-failure
```

Expected: PASS in about 3 seconds. The `pointer moved: no` line is normal in an unattended run and is not asserted on.

- [ ] **Step 5: Commit**

```bash
git add test/test_cursor.cpp test/CMakeLists.txt
git commit -m "test: cursor-following overlay demo

The spec's Tier 1 acceptance example - a red box tracking the pointer with
live coordinates - built entirely against the public API. Doubles as the
click-through check and as a smoke test for the overlay path on XWayland."
```

Include the Step 3 observation in the commit body.

---

### Task 7: Port the HUD onto the public API

`BarWidget` reaches into `w._dpy, w._drawable, w._xgc` (`widgets.hpp:125-127`). That reach-through is the missing-API evidence the spec called out; `Overlay::rect()` is its replacement.

**Files:**
- Modify: `include/stow/widget.hpp` → moves to `include/stow/widget.hpp` (public, X11-free)
- Modify: `include/stow/widgets.hpp` → moves to `src/hud/widgets.hpp`
- Modify: `include/stow/dashboard.hpp` → moves to `src/hud/dashboard.hpp`
- Modify: `include/stow/hud_config.hpp` (stays public — pure parsing)
- Modify: `test/test_widgets.cpp`, `test/test_dashboard.cpp`

**Interfaces:**
- Consumes: `stow::Overlay` with drawing (Task 5)
- Produces: `stow::RenderCtx` now holds `Overlay* win` instead of `XWindow* win`. `Widget::render(RenderCtx&)` is otherwise unchanged, as are `Widget::update/fd/dirty`.

- [ ] **Step 1: Write the failing test**

Modify `test/test_widgets.cpp` so it drives widgets through an `Overlay` instead of an `XWindow`. Replace its window setup with:

```cpp
#include "stow/overlay.hpp"
#include "stow/widget.hpp"
#include "hud/widgets.hpp"
#include "check.hpp"

#include <cstdlib>

int main() {
	if(!std::getenv("DISPLAY")) {
		std::fprintf(stderr, "no $DISPLAY; skipping\n");
		return 77;
	}

	stow::Overlay ov(stow::OverlayConfig{.size = {400, 200}, .font = "monospace:size=10"});
	ov.show();

	// A bar widget renders through the public drawing API, with no access to
	// _dpy / _xgc / _drawable.
	stow::BarWidget bar("echo 63", "MEM", 100.0, 1);
	bar.update();

	stow::DashStats stats;
	stats.now = std::time(nullptr);

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

	CHECK_REPORT();
}
```

- [ ] **Step 2: Run it to confirm it fails**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: FAILS — `cannot convert 'stow::Overlay*' to 'XWindow*'` in the `ctx.win = &ov;` assignment.

- [ ] **Step 3: Retarget `RenderCtx`**

In `include/stow/widget.hpp`, replace the `XWindow` forward declaration with the overlay:

```cpp
// Stow dashboard widget interface. Public and X11-free.
#pragma once

#include <ctime>

#include "stow/layout.hpp"   // stow::Rect
#include "stow/overlay.hpp"  // stow::Overlay

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
	Overlay* win = nullptr;  // never null at render time
	Rect region;             // pixel rect within the window to draw into
	const DashStats* stats = nullptr;
};

class Widget {
public:
	virtual ~Widget() = default;
	virtual void update() {}
	virtual void render(RenderCtx& ctx) = 0;
	virtual int fd() const { return -1; }
	virtual bool dirty() const { return true; }
};

}  // namespace stow
```

- [ ] **Step 4: Add region-scoped text to `Overlay`**

Widgets draw text inside a cell rect with the config's alignment; `Overlay::text(x, y, ...)` alone cannot express that. Add to `include/stow/overlay.hpp`:

```cpp
	// Draw text laid out inside `region`, honoring the configured alignment and
	// clipping to the region. This is what a grid cell needs.
	void text_in(Rect region, std::string_view text);
	void spans_in(Rect region, const Lines& lines);
```

and in `src/overlay.cpp`:

```cpp
void Overlay::text_in(Rect region, std::string_view text) {
	if(_p->closed) return;
	_p->win->draw_region(std::string(text), region.x, region.y, region.width, region.height);
}

void Overlay::spans_in(Rect region, const Lines& lines) {
	if(_p->closed) return;
	_p->win->draw_region_spans(lines, region.x, region.y, region.width, region.height);
}
```

- [ ] **Step 5: Rewrite `BarWidget::render` against the public API**

Move `include/stow/widgets.hpp` to `src/hud/widgets.hpp` (`git mv`), then replace the X11 block in `BarWidget::render` with:

```cpp
	void render(RenderCtx& ctx) override {
		if(!ctx.win) return;
		Overlay& w = *ctx.win;
		const Rect& r = ctx.region;
		double frac = bar_fraction(_value, _max);

		// label + value text on top
		w.text_in(r, number_text(_label, _value));

		// gauge: outline + filled portion along the bottom of the cell
		int pad = 4;
		int bh = 12;
		int bx = r.x + pad;
		int by = r.y + int(r.height) - bh - pad;
		int bw = int(r.width) - 2 * pad;
		if(bw <= 0 || by <= r.y) return;

		Color c = Color::from_rgb(_fg_rgb);
		w.rect(Rect{bx, by, unsigned(bw), unsigned(bh)}, c, 1);
		int fillw = int(frac * bw);
		if(fillw > 0) w.rect(Rect{bx, by, unsigned(fillw), unsigned(bh)}, c, 0);
	}
```

`_fg_rgb` is a new `unsigned int` member defaulting to `stow::Color::parse("#00a080").rgb()`, set from the cell's `fg` when one is configured. Previously this used `w._xforeground.pixel` — an X pixel value, not an RGB triple; taking the configured foreground color is the correct equivalent and removes the last handle grab.

Update every other widget in the file the same way: `w.draw_region(text, r.x, r.y, r.width, r.height)` becomes `w.text_in(r, text)`, and `w.draw_region_spans(lines, ...)` becomes `w.spans_in(r, lines)`.

- [ ] **Step 6: Move the dashboard and retarget it**

```bash
git mv include/stow/dashboard.hpp src/hud/dashboard.hpp
git mv include/stow/widgets.hpp   src/hud/widgets.hpp   # if not already moved in Step 5
```

In `src/hud/dashboard.hpp`:
- Replace `#include "xwindow.hpp"` / `"monitor.hpp"` with `#include "stow/overlay.hpp"` and `#include "stow/screen.hpp"`.
- Replace the `XQueryPointer` block (formerly `dashboard.hpp:239`) with:

```cpp
			stow::Point mp = stow::pointer();
			stats.mouse_x = mp.x;
			stats.mouse_y = mp.y;
```

- Replace its `ShXWindowPr` members with `std::optional<Overlay>` (or `std::unique_ptr<Overlay>` if the code needs a stable address for `RenderCtx::win`), and its render loop's `win->run()` with `ov.pump()`.
- Keep `X11/keysym.h` and the `parse_keybind` / `XGrabKey` logic where it is — the toggle-key feature still needs X11, and `dashboard.hpp` is now a private header under `src/`, so that is allowed.

- [ ] **Step 7: Run to verify**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/bin/shud test/hud.ini 5
```

Expected: all tests pass, and `shud` renders visually the same as before. **Compare against `git stash` of the working tree if the bar gauge looks different** — the foreground-color change in Step 5 is the one place appearance could shift.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor: widgets and dashboard draw through the public API

RenderCtx now carries stow::Overlay& instead of XWindow*. BarWidget no longer
reaches into _dpy/_drawable/_xgc; it draws via Overlay::rect(), using the
configured foreground color rather than a raw X pixel value. Dashboard gets
the pointer from stow::pointer(). widgets.hpp and dashboard.hpp move to
src/hud/ - they are implementation, not API.

Adds Overlay::text_in/spans_in for region-scoped, alignment-aware drawing,
which is what a grid cell needs and Overlay::text(x,y) could not express."
```

---

### Task 8: Port the `stow` and `shud` binaries

**Files:**
- Move: `src/stow.cpp` → `src/bin/stow.cpp`
- Move: `src/shud.cpp` → `src/bin/shud.cpp`
- Create: `include/stow/hud.hpp`
- Create: `src/hud/hud.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 3–7
- Produces: `stow::Hud` — Tier 3's public face: `static std::optional<Hud> from_file(const std::string& path, Error* err)`, `void set_timeout(double seconds)`, `void run()`. `stow::Hud` wraps the private `Dashboard`.

- [ ] **Step 1: Write the failing test**

Create `test/test_hud.cpp`:

```cpp
// Tier 3: an ini-driven HUD, built through the public API only.
#include "stow/hud.hpp"
#include "check.hpp"

#include <cstdlib>

int main(int argc, char** argv) {
	const char* ini = (argc > 1) ? argv[1] : HUD_TEST_INI;

	// Config parsing is pure and works headless.
	stow::Error err;
	stow::HudConfig cfg = stow::HudConfig::load(ini);
	CHECK(cfg.valid());
	CHECK(cfg.error.empty());
	CHECK(!cfg.cells.empty());

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
```

- [ ] **Step 2: Run it to confirm it fails**

Expected: FAILS with `stow/hud.hpp: No such file or directory`.

- [ ] **Step 3: Write the Tier 3 public header**

Create `include/stow/hud.hpp`:

```cpp
// stow::Hud - Tier 3: a grid dashboard described by an ini file.
//
//   auto hud = stow::Hud::from_file("hud.ini", &err);
//   if (hud) hud->run();
//
// No X11 in this header; Dashboard is private under src/hud/.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "stow/error.hpp"
#include "stow/hud_config.hpp"

namespace stow {

class Hud {
public:
	static std::optional<Hud> from_file(const std::string& path, Error* err = nullptr);
	static std::optional<Hud> from_config(const HudConfig& cfg, Error* err = nullptr);

	Hud(Hud&&) noexcept;
	Hud& operator=(Hud&&) noexcept;
	Hud(const Hud&) = delete;
	~Hud();

	// Stop automatically after `seconds`. <= 0 means run until closed.
	void set_timeout(double seconds);

	// Blocks: renders and polls until closed or the timeout expires.
	void run();

private:
	struct Impl;
	std::unique_ptr<Impl> _p;
	explicit Hud(std::unique_ptr<Impl> p);
};

}  // namespace stow
```

`include/stow/hud_config.hpp` stays public and unchanged — it is pure ini parsing with no X11.

- [ ] **Step 4: Implement it**

Create `src/hud/hud.cpp`:

```cpp
#include "stow/hud.hpp"

#include "hud/dashboard.hpp"

namespace stow {

struct Hud::Impl {
	Dashboard dash;
};

std::optional<Hud> Hud::from_config(const HudConfig& cfg, Error* err) {
	if(err) *err = Error{};
	if(!cfg.valid()) {
		if(err) *err = Error{Error::Code::NoDisplay, cfg.error.empty() ? "invalid config" : cfg.error};
		return std::nullopt;
	}
	auto impl = std::make_unique<Impl>();
	impl->dash = Dashboard::from_config(cfg);
	return Hud(std::move(impl));
}

std::optional<Hud> Hud::from_file(const std::string& path, Error* err) {
	return from_config(HudConfig::load(path), err);
}

Hud::Hud(std::unique_ptr<Impl> p) : _p(std::move(p)) {}
Hud::Hud(Hud&&) noexcept = default;
Hud& Hud::operator=(Hud&&) noexcept = default;
Hud::~Hud() = default;

void Hud::set_timeout(double seconds) { _p->dash.set_timeout(seconds); }
void Hud::run() { _p->dash.run(); }

}  // namespace stow
```

An invalid config is not a display failure; if `Error::Code` needs a `BadConfig` member, add it to `include/stow/error.hpp` and use it here rather than reusing `NoDisplay`.

- [ ] **Step 5: Rewrite the binaries as consumers**

```bash
mkdir -p src/bin && git mv src/stow.cpp src/bin/stow.cpp && git mv src/shud.cpp src/bin/shud.cpp
```

`src/bin/shud.cpp` becomes:

```cpp
// shud - stow hud: grid-based overlay dashboard driven by a config file
#include "stow/hud.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: shud <config.ini> [timeout_sec]\n";
		return 1;
	}

	stow::Error err;
	std::optional<stow::Hud> hud = stow::Hud::from_file(argv[1], &err);
	if(!hud) {
		std::cerr << "shud: " << err.message << "\n";
		return 1;
	}

	if(argc > 2) hud->set_timeout(std::atof(argv[2]));
	hud->run();
	return 0;
}
```

The hand-installed `x_error_handler` disappears from `main` — move it into the library, in `src/overlay.cpp`, installed once via a function-local static in `Overlay::create()`:

```cpp
namespace {
int x_error_handler(Display* dpy, XErrorEvent* ev) {
	char buf[256];
	XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
	std::fprintf(stderr, "stow: X error: %s (opcode=%d)\n", buf, int(ev->request_code));
	return 0;
}

void install_error_handler_once() {
	static bool installed = [] {
		XSetErrorHandler(x_error_handler);
		return true;
	}();
	(void)installed;
}
}  // namespace
```

Call `install_error_handler_once()` at the top of `Overlay::create()`. X error handling is global process state; a consumer should not have to know that.

`src/bin/stow.cpp` keeps its `CLI` parsing but replaces the window/monitor/process block with the `Overlay` equivalent — `XWindow::create(...)`, `MonitorManager`, and `set_monitor_geometry` all go away, since `OverlayConfig::monitor` now carries that:

```cpp
	stow::OverlayConfig ocfg;
	ocfg.anchor = cli.config.window.anchor;
	ocfg.x = cli.config.window.px;
	ocfg.y = cli.config.window.py;
	ocfg.monitor = cli.config.window.monitor;
	ocfg.font = cli.config.window.font;
	ocfg.fg = stow::Color::parse(cli.config.window.fg);
	ocfg.bg = stow::Color::parse(cli.config.window.bg);
	ocfg.alpha = cli.config.window.alpha;
	ocfg.align = cli.config.window.align;
	ocfg.border_px = cli.config.window.border_px;
	ocfg.borderless = cli.config.window.borderless;
	ocfg.clickthrough = cli.config.window.overlay;
	ocfg.on_top = cli.config.window.on_top;
	ocfg.title = cli.config.window.title;

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(ocfg, &err);
	if(!ov) {
		std::fprintf(stderr, "stow: %s\n", err.message.c_str());
		return 1;
	}
	ov->show();
```

The process loop keeps using `PTYProcess`/`PipeProcess`, which now live in `src/proc/`. `Process::read_text()` takes a window — retarget it to `stow::Overlay&` and have it call `ov.set_spans(...)` where it previously called `xwin->draw_spans(...)`. Because `stow.cpp` still includes `proc/`, it keeps linking `stowlegacy` until Task 9.

- [ ] **Step 6: Update CMake**

```cmake
add_library(stow STATIC
    src/overlay.cpp
    src/x11/screen.cpp
    src/hud/hud.cpp
)

add_executable(stow_bin src/bin/stow.cpp)
set_target_properties(stow_bin PROPERTIES OUTPUT_NAME stow)
target_link_libraries(stow_bin PRIVATE stow::stow stowlegacy)

add_executable(shud src/bin/shud.cpp)
target_link_libraries(shud PRIVATE stow::stow)   # public API only
```

`shud` linking `stow::stow` alone is the acceptance signal for this task: Tier 3 is reachable without the private include path.

- [ ] **Step 7: Verify**

```bash
cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
./build/bin/shud test/hud.ini 5
./build/bin/stow -F 'monospace:size=10' date
```

Expected: both binaries behave as before. Kill the `stow` run with Ctrl-C.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor: stow and shud become consumers of the public API

Adds stow::Hud (Tier 3) wrapping the private Dashboard. shud now links
stow::stow alone - Tier 3 is reachable with no private include path. The X
error handler moves into the library, installed once; consumers no longer
need to know X error handling is global process state."
```

---

### Task 9: Remove the legacy surface and enforce the boundary

**Files:**
- Delete: `src/x11/stow_window.hpp` (the old `include/window.hpp`)
- Delete: `include/color_span.hpp`
- Modify: `src/x11/window.hpp`, `src/proc/*.hpp`, and every `ColorSpan` user
- Create: `test/test_no_x11_in_public_headers.sh`
- Modify: `CMakeLists.txt`, `test/CMakeLists.txt`

**Interfaces:**
- Consumes: everything prior
- Produces: no new API. Removes `StowWindow`, `ShWindowPtr`, and the global `::ColorSpan` alias.

- [ ] **Step 1: Write the boundary test**

Create `test/test_no_x11_in_public_headers.sh`:

```sh
#!/bin/sh
# The public API must not leak X11. A consumer compiles against include/ alone,
# with no X11 include path - if an X11 header appears here, that breaks.
set -eu

root="${1:?usage: $0 <source-root>}"
hits=$(grep -rn '#[[:space:]]*include[[:space:]]*[<"]X11/' "$root/include" || true)

if [ -n "$hits" ]; then
	echo "FAIL: X11 includes found under include/:" >&2
	echo "$hits" >&2
	exit 1
fi

echo "OK: no X11 includes under include/"
```

```bash
chmod +x test/test_no_x11_in_public_headers.sh
```

Register it in `test/CMakeLists.txt`:

```cmake
add_test(NAME test_no_x11_in_public_headers
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_no_x11_in_public_headers.sh ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 2: Run it — it should pass already**

```bash
cmake -S . -B build && ctest --test-dir build -R no_x11 --output-on-failure
```

Expected: PASS, because Tasks 3 and 7 moved the offending headers. If it FAILS, a public header still includes X11 — fix that before continuing; this test is the whole point of the refactor.

- [ ] **Step 3: Add a consumer compile test**

The grep proves no X11 *include*; this proves a consumer actually compiles and links. Create `test/consumer/CMakeLists.txt`:

```cmake
# A standalone consumer of the installed package. Deliberately does NOT know
# where X11, freetype, or src/ live - if it builds, the API boundary is real.
cmake_minimum_required(VERSION 3.30)
project(stow_consumer CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(stow REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE stow::stow)
```

and `test/consumer/main.cpp`:

```cpp
// Everything a new CLI needs, using nothing but the public API.
#include <stow/hud.hpp>
#include <stow/overlay.hpp>
#include <stow/screen.hpp>

#include <cstdio>

int main() {
	stow::OverlayConfig cfg;
	cfg.anchor = stow::Anchor::TopRight;
	cfg.size = {120, 40};
	cfg.fg = stow::Color::red();

	stow::Error err;
	std::optional<stow::Overlay> ov = stow::Overlay::create(cfg, &err);
	if(!ov) {
		std::fprintf(stderr, "no display: %s\n", err.message.c_str());
		return 0;  // not a build failure; this test is about compiling+linking
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
```

This is wired up in Task 10, once `find_package(stow)` exists. For now, verify it compiles against the build tree:

```bash
g++ -std=c++20 -Iinclude -c test/consumer/main.cpp -o /tmp/consumer.o && echo "CONSUMER COMPILES"
```

- [ ] **Step 4: Delete `StowWindow`**

`src/x11/window.hpp` declares `class XWindow : public StowWindow, public std::enable_shared_from_this<XWindow>`. Remove both bases:

- Delete `#include "x11/stow_window.hpp"`.
- Change the class head to `class XWindow {`.
- Drop `shared_self()` and the `enable_shared_from_this` base — nothing needs it once `RenderCtx` holds an `Overlay*` (Task 7).
- Move `StowWindow`'s data members (`_screen_width`, `_screen_height`, `_window_width`, `_window_height`, `_hidden`, `_dirty`, `_overlay`, `_override_redirect`, `_transparent_background`, `_fullscreen`, `_borderless`, `_use_fixed_geometry`, `_fixed_x/_fixed_y/_fixed_w/_fixed_h`) directly into `XWindow`.
- Remove the `override` keyword from `setup`, `run`, `draw`, `draw_region`, `draw_spans`, `draw_region_spans` — there is no base to override.
- Delete the trailing `inline ShWindowPtr StowWindow::create()` factory.

```bash
git rm src/x11/stow_window.hpp
```

Then change `std::shared_ptr<XWindow>` in `src/overlay.cpp`'s `Impl` to `std::unique_ptr<XWindow>` and `XWindow::create(cfg)` to a direct construction, since shared ownership no longer has a reason to exist.

- [ ] **Step 5: Retire the global `ColorSpan` alias**

```bash
grep -rln '\bColorSpan\b' include/ src/ test/
```

In each hit that is not already `stow::ColorSpan`, qualify it. Inside `namespace stow` blocks the bare name already resolves; the ones needing edits are `src/x11/window.hpp`, `src/proc/screen_buffer.hpp`, `src/proc/ptyprocess.hpp`, `src/proc/process.hpp` and any test. Then:

```bash
git rm include/color_span.hpp
grep -rn 'color_span.hpp' include/ src/ test/   # must return nothing
```

- [ ] **Step 6: Drop `stowlegacy` if nothing needs it**

```bash
grep -rn 'stowlegacy' CMakeLists.txt test/CMakeLists.txt
```

If `stow_bin` is the only remaining user, it is because `src/bin/stow.cpp` includes `proc/`. Either move the process loop behind a public API (out of scope here) or keep `stowlegacy` solely for `stow_bin` and say so in a comment. Do not keep it for tests: every test should link `stow::stow` by now except the pure-logic ones, which need no library at all.

- [ ] **Step 7: Verify the whole suite**

```bash
rm -rf build && cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
env -u DISPLAY ctest --test-dir build --output-on-failure
```

Expected: clean build from scratch, all tests pass with a display, and all X11 tests report `Skipped` without one while the headless ones still pass.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor: delete StowWindow, retire the global ColorSpan alias

StowWindow existed to swap in a Win32 implementation at compile time; with X11
behind Overlay's pimpl, Overlay::Impl is the swap point and the vtable earns
nothing. Removing it also removes the public _hidden/_override_redirect data
members that formed an accidental API surface. XWindow is now unique_ptr-owned;
shared ownership had no remaining justification.

Adds a test that fails if any X11 include reappears under include/."
```

---

### Task 10: Install, package, and document

Makes the library consumable from an unrelated project — the actual goal.

**Files:**
- Create: `cmake/stow-config.cmake.in`
- Modify: `CMakeLists.txt`
- Modify: `README.md`
- Modify: `CLAUDE.md`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: everything prior
- Produces: an installed package providing `find_package(stow)` → `stow::stow`.

- [ ] **Step 1: Write the install and package rules**

Create `cmake/stow-config.cmake.in`:

```cmake
@PACKAGE_INIT@

include(CMakeFindDependencyMacro)
find_dependency(X11)
find_dependency(Freetype)

include("${CMAKE_CURRENT_LIST_DIR}/stow-targets.cmake")

check_required_components(stow)
```

Append to `CMakeLists.txt`:

```cmake
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS stow
    EXPORT stow-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(TARGETS stow_bin shud RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Public headers only. src/ is never installed.
install(DIRECTORY include/stow DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp")

install(EXPORT stow-targets
    FILE stow-targets.cmake
    NAMESPACE stow::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stow)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/stow-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/stow-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stow)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/stow-config-version.cmake
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/stow-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/stow-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/stow)
```

Set the project version at the top: `project(stow VERSION 0.1.0)`.

- [ ] **Step 2: Test the install end to end**

This is the task's real assertion — install to a throwaway prefix and build the Task 9 consumer against it, as an outside project would:

```bash
rm -rf /tmp/stow-prefix /tmp/stow-consumer-build
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/tmp/stow-prefix
cmake --build build -j$(nproc)
cmake --install build

# the consumer knows nothing but the prefix
cmake -S test/consumer -B /tmp/stow-consumer-build -DCMAKE_PREFIX_PATH=/tmp/stow-prefix
cmake --build /tmp/stow-consumer-build
/tmp/stow-consumer-build/consumer && echo "CONSUMER RAN"
```

Expected: configures, builds, links, and runs. If `find_package` cannot resolve X11 or freetype transitively, the `find_dependency` calls in `stow-config.cmake.in` are wrong — fix those, not the consumer.

- [ ] **Step 3: Confirm no private headers were installed**

```bash
find /tmp/stow-prefix/include -name '*.hpp' | sort
grep -rl 'X11/' /tmp/stow-prefix/include && echo "FAIL: X11 leaked into installed headers" || echo "OK"
```

Expected: only `include/stow/*.hpp` files, and the grep finds nothing.

- [ ] **Step 4: Wire the consumer build into ctest**

In `test/CMakeLists.txt`:

```cmake
# Builds the standalone consumer project against the build tree, proving the
# public API compiles and links with no private include path.
add_test(NAME test_consumer_builds
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/consumer
        -B ${CMAKE_CURRENT_BINARY_DIR}/consumer-build
        -Dstow_DIR=${CMAKE_BINARY_DIR})
```

If exporting from the build tree proves fiddly, drop this test and rely on the manual Step 2 procedure documented in the README instead — do not fake it with a test that always passes.

- [ ] **Step 5: Rewrite the README**

Replace `README.md` with content covering: what stow is now (a library, plus two binaries), the build/install commands, the Tier 1 example from Task 3, the cursor example from Task 6, the `shud` ini example, and a `find_package` snippet. Keep the original upstream `stw` description in a "History" section at the bottom — this is a fork and the attribution should survive. The existing `assets/example.png` reference stays.

- [ ] **Step 6: Update `CLAUDE.md`**

The "Architecture" and "Key Patterns" sections describe the old world — `ShXWindowPr xwin = XWindow::create()`, `process->read_text(xwin)`, "Modern C++ implementation is in test files and headers", and the now-wrong test list under "Build Commands". Rewrite them to describe the three tiers, the `include/` vs `src/` boundary, and the current test names. Add the rule that public headers must not include X11 and that `test_no_x11_in_public_headers` enforces it.

- [ ] **Step 7: Full verification**

```bash
rm -rf build && cmake -S . -B build && cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
env -u DISPLAY ctest --test-dir build --output-on-failure
./build/bin/shud test/hud.ini 5
./build/bin/test_cursor 5
```

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "build: install rules, find_package(stow) support, docs

Installs include/stow and libstow.a with an exported stow::stow target, so an
unrelated project can find_package(stow) and link it. src/ is never installed.
README and CLAUDE.md rewritten around the three-tier API."
```

---

## Self-Review

**Spec coverage** — every section of the spec maps to a task:

| Spec section | Task |
|---|---|
| Tier 1 `Overlay`, `OverlayConfig`, `Caps` | 3, 5 |
| `stow/screen.hpp` — `pointer()`, `monitors()` | 4 |
| Tier 2 — `RenderCtx` carries `Overlay&`, `BarWidget` via `rect()` | 7 |
| Tier 3 — `stow::Hud` | 8 |
| `Color`, `ColorSpan` value types | 2 |
| File layout / X11 moves under `src/` | 3, 7, 8 |
| `StowWindow` deleted, namespacing | 9 |
| Build: STATIC lib, `PRIVATE` X11 linkage | 3 |
| Install, `find_package(stow)` | 10 |
| Error handling: degrade not fail, `Caps`, `Error` | 3 (`Error`, `caps()`), 8 (error handler moves into lib) |
| Testing: headless assertions | 1, 2 |
| Testing: `$DISPLAY`-gated demos | 1 (gating), 3, 4, 6, 8 |
| Testing: `X11/` grep | 9 |
| Acceptance example `test_cursor` | 6 |
| Compositor risk reported not papered over | 4 (Step 6), 6 (Step 3) |
| C++20 public headers | Global Constraints |

**Known gap, stated rather than hidden:** the spec's file table lists `src/proc/*.cpp` and `src/hud/*.cpp` as compiled translation units. This plan moves those files to the same directories but leaves them as inline headers (see "A note on how the X11 code moves"). The API boundary — the thing the spec is actually for — is fully achieved either way; splitting them is optional follow-up cleanup, not a prerequisite, and is deliberately not scheduled.

**Type consistency** — checked across tasks: `Color::parse/rgb/from_rgb` (T2) are used as defined in T3, T5, T7. `Error{Code, message}` (T3) is used in T4, T6, T8, with the `BadConfig` addition flagged inline in T8. `Size{w,h}` (T3) is used by `resize()` (T5) and `screen_size()` (T4). `Rect{x,y,width,height}` is the existing `layout.hpp` type throughout — `width`/`height`, never `w`/`h`. `RenderCtx::win` is `Overlay*` from T7 onward. `pump()` / `pump(timeout)` / `run(period, cb)` signatures match between T3, T5, T6.
