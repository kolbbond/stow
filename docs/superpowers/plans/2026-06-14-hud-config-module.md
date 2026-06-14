# HUD Config Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the duplicated ad-hoc INI parser in `shud.cpp`/`test_grid.cpp` with one shared, sectioned-INI config module (`stow::HudConfig`) that supports per-cell properties and configurable HUD fields.

**Architecture:** A new header-only module `include/stow/hud_config.hpp` defines parsed types (`CellSpec`, `HudConfig`) and a hand-written sectioned-INI parser (`[grid]`/`[cell]`/`[hud]`). It depends only on the existing `stow::GridConfig`. `shud.cpp` is cut over to consume it; its render loop is unchanged. A headless unit test (`test/test_hud_config.cpp`) drives development TDD-style.

**Tech Stack:** C++20, header-only (`stowlib` is an INTERFACE target), CTest. No new dependencies — INI is parsed by hand.

**Scope note:** This is Plan 1 of 2. Plan 2 (Widget API + `Dashboard`) builds on the types created here. This plan leaves the build green and `shud` working with the new format.

**Format rules (authoritative — the spec examples are illustrative):**
- Sections: `[grid]` (once), `[cell]` (repeated), `[hud]` (repeated).
- Comments: **full-line only**, starting with `#` or `;`. Inline/trailing comments are NOT supported (a command value may contain `#`/`;`).
- `key = value`, whitespace-trimmed both sides.

---

### Task 1: Config types + load stub (headless test scaffold)

**Files:**
- Create: `include/stow/hud_config.hpp`
- Create: `test/test_hud_config.cpp`
- Modify: `test/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `test/test_hud_config.cpp`:

```cpp
// headless unit tests for the sectioned-INI config parser
#include "stow/hud_config.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

// write `content` to a temp file, return its path
static std::string write_tmp(const std::string& name, const std::string& content) {
	std::string path = "/tmp/stow_test_" + name;
	std::ofstream out(path);
	out << content;
	out.close();
	return path;
}

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
	std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++g_failures; } } while(0)

static void test_missing_file() {
	stow::HudConfig cfg = stow::HudConfig::load("/tmp/stow_does_not_exist.ini");
	CHECK(!cfg.valid());
	CHECK(!cfg.error.empty());
}

int main() {
	test_missing_file();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all hud_config tests passed\n";
	return 0;
}
```

Add to `test/CMakeLists.txt` `test_list` (after `test_layout.cpp`):

```cmake
    test_layout.cpp
    test_hud_config.cpp
    test_clickthrough.cpp
```

And register it (add with the other `add_test` lines, before `test_clickthrough`):

```cmake
add_test(NAME test_hud_config  COMMAND test_hud_config)
```

- [ ] **Step 2: Run test to verify it fails (compile error — header missing)**

Run: `cmake -S . -B build >/dev/null && cmake --build build --target test_hud_config 2>&1 | tail -5`
Expected: FAIL — `fatal error: stow/hud_config.hpp: No such file or directory`

- [ ] **Step 3: Write minimal implementation**

Create `include/stow/hud_config.hpp`:

```cpp
// Stow HUD dashboard config: sectioned-INI parser ([grid]/[cell]/[hud])
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>

#include "grid.hpp"

namespace stow {

// One cell's parsed config. Sentinels (empty string / -1 / 0) mean "inherit".
struct CellSpec {
	int row = -1, col = -1;            // from "at = r,c"; -1 => assigned by fill order
	std::string widget;                // "command"|"hud"|"number"|"bar"; resolved in finalize
	std::string cmd;                   // data source / command to run
	std::string label;                 // number/bar label
	std::vector<std::string> fields;   // hud fields (e.g. time, fps, mouse)
	double max = 100.0;                // bar scale
	int period = -1;                   // -1 => inherit HudConfig::period
	std::string fg, bg, font;          // appearance overrides
	double alpha = -1.0;
	char align = 0;
	bool is_hud = false;
};

struct HudConfig {
	GridConfig grid;
	int monitor = -1;
	int period = 1;
	bool single_window = false;
	bool grid_lines = true;
	bool fit_to_cells = true;
	std::string toggle_key = "super+h";
	std::vector<CellSpec> cells;
	std::string error;                 // non-empty => parse/validation failure

	bool valid() const {
		return error.empty() && grid.valid() &&
		       static_cast<int>(cells.size()) <= grid.rows * grid.cols;
	}

	static HudConfig load(const std::string& path);
};

inline HudConfig HudConfig::load(const std::string& path) {
	HudConfig cfg;
	std::ifstream in(path);
	if(!in) {
		cfg.error = "cannot open config: " + path;
		return cfg;
	}
	return cfg;  // parsing added in later tasks
}

}  // namespace stow
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: PASS — `all hud_config tests passed`

- [ ] **Step 5: Commit**

```bash
git add include/stow/hud_config.hpp test/test_hud_config.cpp test/CMakeLists.txt
git commit -m "hud_config: types + load stub with headless test scaffold"
```

---

### Task 2: Parse helpers + [grid] section

**Files:**
- Modify: `include/stow/hud_config.hpp`
- Modify: `test/test_hud_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `test/test_hud_config.cpp` (new function + call it from `main` before the failure check):

```cpp
static void test_grid_section() {
	std::string path = write_tmp("grid.ini",
		"# comment line\n"
		"[grid]\n"
		"rows = 2\n"
		"cols = 3\n"
		"row_heights = 40,60\n"
		"col_widths = 30,30,40\n"
		"monitor = 1\n"
		"single_window = 1\n"
		"grid_lines = 0\n"
		"fit_to_cells = 1\n"
		"period = 5\n"
		"toggle_key = ctrl+space\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.grid.rows == 2);
	CHECK(cfg.grid.cols == 3);
	CHECK(cfg.grid.row_heights.size() == 2 && cfg.grid.row_heights[1] == 60);
	CHECK(cfg.grid.col_widths.size() == 3 && cfg.grid.col_widths[2] == 40);
	CHECK(cfg.monitor == 1);
	CHECK(cfg.single_window == true);
	CHECK(cfg.grid_lines == false);
	CHECK(cfg.period == 5);
	CHECK(cfg.toggle_key == "ctrl+space");
}
```

In `main`, add `test_grid_section();` before the failure check.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: FAIL — e.g. `FAIL: cfg.grid.rows == 2`

- [ ] **Step 3: Write minimal implementation**

In `hud_config.hpp`, add an internal `detail` namespace **above** `HudConfig::load` (after the `HudConfig` struct definition):

```cpp
namespace detail {

inline std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

inline std::string lower(std::string s) {
	for(char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

inline bool truthy(const std::string& v) {
	std::string l = lower(v);
	return l == "1" || l == "true" || l == "yes" || l == "on";
}

inline std::vector<int> csv_ints(const std::string& s) {
	std::vector<int> out;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) {
		item = trim(item);
		if(item.empty()) continue;
		try { out.push_back(std::stoi(item)); } catch(...) { /* caller validates */ }
	}
	return out;
}

inline std::vector<std::string> csv_strs(const std::string& s) {
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) {
		item = trim(item);
		if(!item.empty()) out.push_back(item);
	}
	return out;
}

inline int to_int(const std::string& v, int fallback) {
	try { return std::stoi(v); } catch(...) { return fallback; }
}

}  // namespace detail
```

Then replace the body of `HudConfig::load` (keep the open-file guard) with:

```cpp
inline HudConfig HudConfig::load(const std::string& path) {
	HudConfig cfg;
	std::ifstream in(path);
	if(!in) {
		cfg.error = "cannot open config: " + path;
		return cfg;
	}

	auto add_err = [&cfg](const std::string& m) {
		if(!cfg.error.empty()) cfg.error += "; ";
		cfg.error += m;
	};

	std::string line;
	std::string section;  // "", "grid", "cell", "hud"

	while(std::getline(in, line)) {
		line = detail::trim(line);
		if(line.empty() || line[0] == '#' || line[0] == ';') continue;

		if(line.front() == '[' && line.back() == ']') {
			section = detail::lower(detail::trim(line.substr(1, line.size() - 2)));
			continue;
		}

		size_t eq = line.find('=');
		if(eq == std::string::npos) { add_err("malformed line: " + line); continue; }
		std::string key = detail::trim(line.substr(0, eq));
		std::string val = detail::trim(line.substr(eq + 1));

		if(section == "grid") {
			if(key == "rows") cfg.grid.rows = detail::to_int(val, 0);
			else if(key == "cols") cfg.grid.cols = detail::to_int(val, 0);
			else if(key == "row_heights") cfg.grid.row_heights = detail::csv_ints(val);
			else if(key == "col_widths") cfg.grid.col_widths = detail::csv_ints(val);
			else if(key == "monitor") cfg.monitor = detail::to_int(val, -1);
			else if(key == "period") cfg.period = detail::to_int(val, 1);
			else if(key == "single_window") cfg.single_window = detail::truthy(val);
			else if(key == "grid_lines") cfg.grid_lines = detail::truthy(val);
			else if(key == "fit_to_cells") cfg.fit_to_cells = detail::truthy(val);
			else if(key == "toggle_key") cfg.toggle_key = val;
			else add_err("unknown grid key: " + key);
		}
	}

	return cfg;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: PASS — `all hud_config tests passed`

- [ ] **Step 5: Commit**

```bash
git add include/stow/hud_config.hpp test/test_hud_config.cpp
git commit -m "hud_config: parse [grid] section"
```

---

### Task 3: Parse [cell] and [hud] sections

**Files:**
- Modify: `include/stow/hud_config.hpp`
- Modify: `test/test_hud_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `test/test_hud_config.cpp` and call from `main`:

```cpp
static void test_cell_and_hud() {
	std::string path = write_tmp("cells.ini",
		"[grid]\n"
		"rows = 2\n"
		"cols = 2\n"
		"row_heights = 50,50\n"
		"col_widths = 50,50\n"
		"[cell]\n"
		"at = 0,0\n"
		"cmd = btop\n"
		"fg = #00ff00\n"
		"period = 2\n"
		"[cell]\n"
		"at = 0,1\n"
		"widget = number\n"
		"label = cores\n"
		"cmd = nproc\n"
		"[hud]\n"
		"at = 1,0\n"
		"fields = time, fps, mouse\n"
		"fg = #ffaa00\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.cells.size() == 3);
	// cell 0: command (default widget)
	CHECK(cfg.cells[0].row == 0 && cfg.cells[0].col == 0);
	CHECK(cfg.cells[0].cmd == "btop");
	CHECK(cfg.cells[0].widget == "command");
	CHECK(cfg.cells[0].fg == "#00ff00");
	CHECK(cfg.cells[0].period == 2);
	// cell 1: explicit number widget
	CHECK(cfg.cells[1].widget == "number");
	CHECK(cfg.cells[1].label == "cores");
	CHECK(cfg.cells[1].cmd == "nproc");
	// cell 2: hud
	CHECK(cfg.cells[2].is_hud == true);
	CHECK(cfg.cells[2].widget == "hud");
	CHECK(cfg.cells[2].fields.size() == 3 && cfg.cells[2].fields[1] == "fps");
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: FAIL — `FAIL: cfg.cells.size() == 3`

- [ ] **Step 3: Write minimal implementation**

In `HudConfig::load`, when a section header is read, create a cell for `cell`/`hud`. Replace the section-header branch:

```cpp
		if(line.front() == '[' && line.back() == ']') {
			section = detail::lower(detail::trim(line.substr(1, line.size() - 2)));
			if(section == "cell" || section == "hud") {
				cfg.cells.emplace_back();
				CellSpec& c = cfg.cells.back();
				c.is_hud = (section == "hud");
				c.widget = c.is_hud ? "hud" : "";  // command default resolved in finalize
			}
			continue;
		}
```

Add a `cell`/`hud` key handler after the `grid` block (still inside the `while`):

```cpp
		else if(section == "cell" || section == "hud") {
			if(cfg.cells.empty()) { add_err("key outside cell: " + key); continue; }
			CellSpec& c = cfg.cells.back();
			if(key == "at") {
				std::vector<int> rc = detail::csv_ints(val);
				if(rc.size() == 2) { c.row = rc[0]; c.col = rc[1]; }
				else add_err("bad 'at' (want r,c): " + val);
			}
			else if(key == "widget") c.widget = detail::lower(val);
			else if(key == "cmd") c.cmd = val;
			else if(key == "label") c.label = val;
			else if(key == "fields") c.fields = detail::csv_strs(val);
			else if(key == "max") { try { c.max = std::stod(val); } catch(...) { add_err("bad max: " + val); } }
			else if(key == "period") c.period = detail::to_int(val, -1);
			else if(key == "fg") c.fg = val;
			else if(key == "bg") c.bg = val;
			else if(key == "font") c.font = val;
			else if(key == "alpha") { try { c.alpha = std::stod(val); } catch(...) { add_err("bad alpha: " + val); } }
			else if(key == "align") c.align = val.empty() ? 0 : val[0];
			else add_err("unknown cell key: " + key);
		}
```

Before `return cfg;`, resolve the default widget for command cells:

```cpp
	for(CellSpec& c : cfg.cells) {
		if(!c.is_hud && c.widget.empty()) c.widget = "command";
	}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: PASS — `all hud_config tests passed`

- [ ] **Step 5: Commit**

```bash
git add include/stow/hud_config.hpp test/test_hud_config.cpp
git commit -m "hud_config: parse [cell] and [hud] sections"
```

---

### Task 4: Finalize — uniform grid default, fill-order placement, validation

**Files:**
- Modify: `include/stow/hud_config.hpp`
- Modify: `test/test_hud_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `test/test_hud_config.cpp` and call from `main`:

```cpp
static void test_finalize() {
	// no row_heights/col_widths => uniform; cells without 'at' => fill order
	std::string path = write_tmp("finalize.ini",
		"[grid]\n"
		"rows = 1\n"
		"cols = 2\n"
		"[cell]\n"
		"cmd = date\n"
		"[cell]\n"
		"cmd = uptime\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.valid());
	CHECK(cfg.grid.row_heights.size() == 1 && cfg.grid.row_heights[0] == 100);
	CHECK(cfg.grid.col_widths.size() == 2);
	CHECK(cfg.cells[0].row == 0 && cfg.cells[0].col == 0);  // fill order
	CHECK(cfg.cells[1].row == 0 && cfg.cells[1].col == 1);
}

static void test_validation_errors() {
	// rows/cols zero => invalid
	std::string p1 = write_tmp("bad_grid.ini", "[grid]\nrows = 0\ncols = 2\n");
	stow::HudConfig c1 = stow::HudConfig::load(p1);
	CHECK(!c1.valid());

	// more cells than grid slots => invalid
	std::string p2 = write_tmp("toomany.ini",
		"[grid]\nrows = 1\ncols = 1\n[cell]\ncmd=a\n[cell]\ncmd=b\n");
	stow::HudConfig c2 = stow::HudConfig::load(p2);
	CHECK(!c2.valid());

	// explicit 'at' out of range => error recorded
	std::string p3 = write_tmp("oob.ini",
		"[grid]\nrows = 1\ncols = 1\n[cell]\nat = 5,5\ncmd=a\n");
	stow::HudConfig c3 = stow::HudConfig::load(p3);
	CHECK(!c3.valid());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: FAIL — `FAIL: cfg.grid.row_heights.size() == 1 ...`

- [ ] **Step 3: Write minimal implementation**

Replace the pre-return finalize block (the widget-default loop from Task 3) with the full finalize:

```cpp
	// --- finalize ---
	// default to uniform percentages when not given
	if(cfg.grid.rows > 0 && cfg.grid.cols > 0) {
		GridConfig uni = GridConfig::uniform(cfg.grid.rows, cfg.grid.cols);
		if(cfg.grid.row_heights.empty()) cfg.grid.row_heights = uni.row_heights;
		if(cfg.grid.col_widths.empty()) cfg.grid.col_widths = uni.col_widths;
	}
	cfg.grid.fit_to_cells = cfg.fit_to_cells;

	// resolve command default + fill-order placement
	int next = 0;
	for(CellSpec& c : cfg.cells) {
		if(!c.is_hud && c.widget.empty()) c.widget = "command";
		if(c.row < 0 || c.col < 0) {
			if(cfg.grid.cols > 0) { c.row = next / cfg.grid.cols; c.col = next % cfg.grid.cols; }
		}
		next++;
		if(cfg.grid.rows > 0 && cfg.grid.cols > 0 &&
			(c.row >= cfg.grid.rows || c.col >= cfg.grid.cols)) {
			add_err("cell out of grid range: " + std::to_string(c.row) + "," + std::to_string(c.col));
		}
	}

	if(cfg.grid.rows <= 0 || cfg.grid.cols <= 0) add_err("grid rows/cols must be > 0");
```

(Note: `GridConfig::uniform` and `GridConfig::valid` already exist in `grid.hpp`. `valid()` checks `row_heights.size()==rows` etc., which the uniform fill satisfies.)

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: PASS — `all hud_config tests passed`

- [ ] **Step 5: Commit**

```bash
git add include/stow/hud_config.hpp test/test_hud_config.cpp
git commit -m "hud_config: finalize uniform grid, fill-order placement, validation"
```

---

### Task 5: Migrate hud_test.ini to sectioned format

**Files:**
- Modify: `test/hud_test.ini`
- Modify: `test/test_hud_config.cpp`

- [ ] **Step 1: Write the failing test**

Add to `test/test_hud_config.cpp` and call from `main`. This loads the real fixture by an absolute path injected at compile time:

```cpp
#ifdef HUD_TEST_INI
static void test_real_fixture() {
	stow::HudConfig cfg = stow::HudConfig::load(HUD_TEST_INI);
	CHECK(cfg.error.empty());
	CHECK(cfg.valid());
	CHECK(cfg.grid.rows >= 1 && cfg.grid.cols >= 1);
	bool has_hud = false;
	for(const auto& c : cfg.cells) if(c.is_hud) has_hud = true;
	CHECK(has_hud);
}
#endif
```

In `main`, guard the call:

```cpp
#ifdef HUD_TEST_INI
	test_real_fixture();
#endif
```

In `test/CMakeLists.txt`, pass the fixture path as a define for this one target. Add after the `add_test` lines:

```cmake
target_compile_definitions(test_hud_config PRIVATE
    HUD_TEST_INI="${CMAKE_CURRENT_SOURCE_DIR}/hud_test.ini")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake -S . -B build >/dev/null && cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: FAIL — the old flat `hud_test.ini` has no `[grid]` section, so `rows/cols` stay 0 → `!cfg.valid()`.

- [ ] **Step 3: Write minimal implementation**

Replace `test/hud_test.ini` entirely with the sectioned format:

```ini
# Simple HUD overlay test (sectioned format)
[grid]
rows = 1
cols = 2
row_heights = 100
col_widths = 50,50
single_window = 1
grid_lines = 1
fit_to_cells = 1
monitor = 0
period = 1

[cell]
at = 0,0
cmd = date +"%A %B %d, %Y  %H:%M:%S"

[hud]
at = 0,1
fields = time, fps, mouse
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_hud_config >/dev/null 2>&1 && ./build/bin/test_hud_config`
Expected: PASS — `all hud_config tests passed`

- [ ] **Step 5: Commit**

```bash
git add test/hud_test.ini test/test_hud_config.cpp test/CMakeLists.txt
git commit -m "hud_config: migrate hud_test.ini to sectioned format + fixture test"
```

---

### Task 6: Cut shud.cpp over to HudConfig

**Files:**
- Modify: `src/shud.cpp`

This removes shud's private `GridFileConfig`, `load_config`, `is_hud_cell`, `parse_csv_ints`, and `split_cmd` (command splitting now needs to stay, see below) and consumes `stow::HudConfig`. The render loop is otherwise unchanged. `split_cmd` and `parse_keybind` stay local to shud (loop-time concerns, not config parsing).

- [ ] **Step 1: Replace the config plumbing**

Edit `src/shud.cpp`:

1. Add the include near the other stow includes:

```cpp
#include "stow/hud_config.hpp"
```

2. Delete the `GridFileConfig` struct, the `is_hud_cell` function, the `parse_csv_ints` function, and the `load_config` function (they are fully replaced by `stow::HudConfig`). **Keep** `trim`, `split_cmd`, `parse_keybind`, `format_time_now`, `CellState`, `HudState`, and `x_error_handler`.

3. In `main`, replace the parse + validation block (from `GridFileConfig file_cfg = load_config(argv[1]);` through the three `if(...) return 1;` checks) with:

```cpp
	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) {
		std::cerr << "shud: " << (cfg.error.empty() ? "invalid config" : cfg.error) << "\n";
		return 1;
	}
```

4. Replace every later use of `file_cfg.<field>` with the new sources:
   - `file_cfg.rows` / `file_cfg.cols` / `file_cfg.row_heights` / `file_cfg.col_widths` / `file_cfg.fit_to_cells` → `cfg.grid.rows` / `cfg.grid.cols` / `cfg.grid.row_heights` / `cfg.grid.col_widths` / `cfg.grid.fit_to_cells`.
   - `file_cfg.monitor` → `cfg.monitor`; `file_cfg.single_window` → `cfg.single_window`; `file_cfg.grid_lines` → `cfg.grid_lines`; `file_cfg.period` → `cfg.period`; `file_cfg.toggle_key` → `cfg.toggle_key`.
   - The grid build can now use the parsed grid directly:

```cpp
	stow::GridLayout grid(cfg.grid);
```

   (Delete the manual `stow::GridConfig grid_cfg; grid_cfg.rows = ...;` block.)

5. Replace cell-content access. The old loop used `file_cfg.cells[i]` (a `std::string`) and `is_hud_cell(...)`. Cells are now `stow::CellSpec` placed by `at`/fill-order. Build an index from grid position to `CellSpec`:

   After `std::vector<stow::Rect> geoms = grid.calculate(...)` and before the window-creation loop, add:

```cpp
	// map grid cell index -> CellSpec (by row,col)
	std::vector<const stow::CellSpec*> cell_at(geoms.size(), nullptr);
	for(const stow::CellSpec& c : cfg.cells) {
		int idx = grid.cell_index(c.row, c.col);
		if(idx >= 0 && idx < static_cast<int>(cell_at.size())) cell_at[idx] = &c;
	}
```

6. In the window-creation loop, replace the hud-detection:

```cpp
		if(cell_at[i] && cell_at[i]->is_hud) {
			hud_cells[i] = true;
		}
```

7. In the main loop's command-start block, replace the `split_cmd(file_cfg.cells[i])` usage:

```cpp
		for(size_t i = 0; i < cells.size(); i++) {
			if(i < hud_cells.size() && hud_cells[i]) continue;
			if(!cells[i].cmd.empty()) continue;
			if(!cell_at[i] || cell_at[i]->cmd.empty()) continue;
			std::vector<std::string> parts = split_cmd(cell_at[i]->cmd);
			if(parts.empty()) continue;
			cells[i].cmd = parts[0];
			for(size_t j = 1; j < parts.size(); j++) cells[i].args.push_back(parts[j]);
			cells[i].proc = PTYProcess::create();
			cells[i].proc->setup();
			cells[i].proc->start_cmd(cells[i].cmd, cells[i].args);
		}
```

   (Loops that iterate `file_cfg.cells.size()` become `cells.size()`, which equals `geoms.size()`.)

- [ ] **Step 2: Build shud**

Run: `cmake --build build --target shud 2>&1 | tail -8`
Expected: PASS — `Built target shud`, no errors.

- [ ] **Step 3: Run shud against the migrated fixture (bounded)**

Run: `timeout 3 ./build/bin/shud test/hud_test.ini; echo "exit=$?"`
Expected: prints monitor/overlay/window diagnostics and the `toggle interactive:` line, renders for 3s, then `timeout` kills it (`exit=124`). No `shud: invalid config` and no crash before the timeout.

- [ ] **Step 4: Run the full test suite**

Run: `(cd build && ctest --output-on-failure 2>&1 | tail -15)`
Expected: PASS — `100% tests passed` (9 tests now, including `test_hud_config`).

- [ ] **Step 5: Commit**

```bash
git add src/shud.cpp
git commit -m "shud: consume stow::HudConfig, drop duplicated parser"
```

---

### Task 7: Point test_grid.cpp at the shared parser (kill the last duplicate)

**Files:**
- Modify: `test/test_grid.cpp`
- Modify: `test/CMakeLists.txt` (pass fixture path so the no-arg case still works under ctest)

`test_grid.cpp` still carries its own copy of `GridFileConfig`/`load_config`/`is_hud_cell`/`parse_csv_ints`. Plan 2 will retire this file in favour of `test_dashboard`; for now, remove its duplicated parser so the repo has exactly one config parser.

- [ ] **Step 1: Replace test_grid's config plumbing**

Apply the **same** edits as Task 6 steps to `test/test_grid.cpp`:
- add `#include "stow/hud_config.hpp"`,
- delete its `GridFileConfig`, `load_config`, `is_hud_cell`, `parse_csv_ints`,
- replace `load_config(argv[1])` + validation with `stow::HudConfig cfg = stow::HudConfig::load(argv[1]); if(!cfg.valid()) { std::cerr << "test_grid: " << cfg.error << "\n"; return 1; }`,
- build `stow::GridLayout grid(cfg.grid);`,
- add the `cell_at` index and use it exactly as in Task 6 steps 5–7,
- keep the `timeout_sec` loop break added previously.

- [ ] **Step 2: Build test_grid**

Run: `cmake --build build --target test_grid 2>&1 | tail -5`
Expected: PASS — `Built target test_grid`.

- [ ] **Step 3: Run the full suite**

Run: `(cd build && ctest --output-on-failure 2>&1 | tail -15)`
Expected: PASS — `100% tests passed`. (`test_grid` is already registered with `hud_test.ini 3`.)

- [ ] **Step 4: Verify only one parser remains**

Run: `grep -rl "load_config" src test`
Expected: no output (the function name no longer exists anywhere).

- [ ] **Step 5: Commit**

```bash
git add test/test_grid.cpp
git commit -m "test_grid: use shared stow::HudConfig parser"
```

---

## Self-Review

**Spec coverage (config-module portion of the spec):**
- Sectioned INI `[grid]`/`[cell]`/`[hud]` → Tasks 2–3. ✓
- `CellSpec`/`HudConfig` types with inheritance sentinels → Task 1, 3. ✓
- `at=` placement + fill-order fallback → Task 4. ✓
- `load()` never throws; `error` accumulation; `valid()` → Tasks 1–4 (try/catch in helpers, `add_err`). ✓
- Migrate `hud_test.ini`, no dual format → Task 5. ✓
- Kill duplicated parser (shud + test_grid) → Tasks 6–7. ✓
- HUD `fields` configurable list parsed → Task 3 (`fields` via `csv_strs`). Rendering of arbitrary fields is Plan 2; shud's existing HUD render stays for now.
- Widget API / Dashboard / Number-Bar-Plot-Tab widgets → **Plan 2** (out of scope here, by design).

**Placeholder scan:** No TBD/TODO; every code step shows full code. ✓

**Type consistency:** `HudConfig`/`CellSpec` field names identical across Tasks 1, 3, 4, 6, 7. `detail::` helpers (`trim`, `lower`, `truthy`, `csv_ints`, `csv_strs`, `to_int`) defined in Task 2, used in 3–4. `grid.cell_index(row,col)` and `GridConfig::uniform`/`valid` are confirmed-existing APIs. ✓

**Known follow-ups (Plan 2):** configurable HUD field rendering, per-cell appearance actually applied to windows (parsed here, consumed there), Widget API, `Dashboard` loop extraction, `test_dashboard`, retire `test_grid`.
