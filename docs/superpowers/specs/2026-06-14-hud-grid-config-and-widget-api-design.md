# HUD/Grid Config + Widget API — Design

Date: 2026-06-14
Status: Draft (awaiting review)

## Goal

Turn `shud` from a hardcoded grid-of-commands runner into a small **dashboard
library** with two front-ends:

1. A **declarative config** (sectioned INI) that describes a grid of widgets.
2. A **C++ API** (`stow::Dashboard` + `stow::Widget`) that the user can call
   directly to build a dashboard in code.

Both front-ends produce the same thing: a `Dashboard` that places `Widget`s in
grid cells and runs the render/poll loop. This is the "hook into it as a lib"
requirement — config is just one way to construct what the API exposes.

A secondary, enabling goal: **eliminate the duplicated config parser** that
currently exists nearly verbatim in `src/shud.cpp` and `test/test_grid.cpp`.

## Non-goals

- No new third-party dependencies (stay on X11 + Xft + freetype). INI is parsed
  by hand, as today.
- No change to `XWindow`'s rendering model. Widgets draw through the primitives
  it already exposes.
- Not building every widget in one pass. See "Widget set & phasing".

## Architecture

Three layers, each independently understandable and testable:

```
┌─────────────────────────────────────────────┐
│ Front-ends                                    │
│   • HudConfig::load(path)  → builds Dashboard │  declarative (INI)
│   • Dashboard API           ← user C++ code   │  programmatic
└───────────────────────┬───────────────────────┘
                        │ constructs
┌───────────────────────▼───────────────────────┐
│ Widget API  (the library)                      │
│   Widget (abstract): update(), render(ctx),fd()│
│   Dashboard: owns grid + window(s) + widgets,  │
│              runs the poll/render/toggle loop  │
│   Concrete widgets: Command, Hud, Number, Bar  │
└───────────────────────┬───────────────────────┘
                        │ draws via
┌───────────────────────▼───────────────────────┐
│ XWindow (existing)                             │
│   draw_region / draw_region_spans / clip /     │
│   get_color + raw _dpy/_drawable/_xgc/_xdraw   │
└─────────────────────────────────────────────────┘
```

### Files

New:
- `include/stow/hud_config.hpp` — sectioned-INI parser + parsed types.
- `include/stow/widget.hpp` — `Widget` base, `RenderCtx`, `DashStats`.
- `include/stow/widgets.hpp` — concrete widgets (Command, Hud, Number, Bar).
- `include/stow/dashboard.hpp` — `Dashboard` orchestrator (the extracted loop).
- `test/test_hud_config.cpp` — headless parser unit tests (added to ctest).
- `test/test_dashboard.cpp` — bounded-timeout integration demo (see Testing).

Changed:
- `src/shud.cpp` — collapses to: parse args → `HudConfig::load` →
  `Dashboard::from_config` → `run()`. The ~300-line main loop moves into
  `Dashboard`.
- `test/test_grid.cpp` — becomes a thin demo over the shared module (or is
  retired in favour of `test_dashboard` — decided at implementation time; it
  must no longer carry its own copy of the parser).

## Layer 1 — Config format & types

Sectioned INI. Section kinds: `[grid]` (one), `[cell]` (repeated), `[hud]`
(repeated). A `[hud]` is sugar for a cell whose widget is the built-in HUD.

```ini
[grid]
rows = 2
cols = 2
row_heights = 50,50      ; percentages; omitted => uniform
col_widths  = 50,50
monitor = 0
single_window = 1
grid_lines = 1
fit_to_cells = 1
period = 1               ; default refresh (s) for cells without their own
toggle_key = super+h

[cell]
at = 0,0                 ; row,col. If omitted, fills next free cell in order.
widget = command         ; default when 'cmd' present; see widget table
cmd = btop
fg = #00ff00             ; per-cell appearance override (else inherits default)
period = 2

[cell]
at = 0,1
widget = number
label = cores
cmd = nproc              ; stdout parsed as the number

[cell]
at = 1,1
widget = bar
label = cpu
cmd = /home/ohr4/bin/cpu_pct.sh   ; stdout parsed as number
max = 100

[hud]
at = 1,0
fields = time, fps, mouse
fg = #ffaa00
```

### Parsed types (`hud_config.hpp`)

```cpp
namespace stow {

struct CellSpec {
    int row = -1, col = -1;          // from "at = r,c"; -1 => fill order
    std::string widget = "command";  // command|hud|number|bar  (extensible)
    std::string cmd;                 // data source (command to run)
    std::string label;               // number/bar label
    std::vector<std::string> fields; // hud fields
    double max = 100.0;              // bar scale
    int period = -1;                 // -1 => inherit grid.period
    // appearance overrides; empty/sentinel => inherit window default
    std::string fg, bg, font;
    double alpha = -1.0;
    char align = 0;
};

struct HudConfig {
    GridConfig grid;                 // reuse existing stow::GridConfig
    int monitor = -1;
    int period = 1;
    bool single_window = false, grid_lines = true, fit_to_cells = true;
    std::string toggle_key = "super+h";
    std::vector<CellSpec> cells;
    std::string error;               // non-empty => parse/validation failure

    static HudConfig load(const std::string& path);
    bool valid() const;              // error.empty() && grid.valid() && ...
};

}  // namespace stow
```

Decisions:
- **`at = row,col`** for explicit placement; fill-order fallback keeps simple
  configs terse. Unmentioned cells are blank.
- **Appearance inherits** from the global `WindowConfig` default via sentinels
  (empty string / `-1` / `0`), so a cell specifies only what differs.
- `widget` defaults to `command` when `cmd` is set, to `hud` in a `[hud]`
  section.

### Backward compatibility

The legacy flat format is **not** kept. `test/hud_test.ini` is migrated to the
sectioned format. Rationale: it is the only config in the repo, and supporting
both doubles the parser surface for no real user. (Open for review.)

## Layer 2 — Widget API (the library)

```cpp
namespace stow {

// Shared, per-frame state widgets may read (HUD fields, etc.)
struct DashStats {
    double fps = 0.0;
    int mouse_x = 0, mouse_y = 0;
    std::time_t now = 0;
};

// What a widget needs to draw itself this frame.
struct RenderCtx {
    XWindow& win;          // target window (shared or per-cell)
    Rect region;           // pixel rect within the window to draw into
    const DashStats& stats;
};

class Widget {
public:
    virtual ~Widget() = default;
    virtual void update() {}            // refresh state (poll process, sample)
    virtual void render(RenderCtx& ctx) = 0;   // draw into ctx.region
    virtual int fd() const { return -1; }      // pollable fd, or -1 if none
    virtual bool dirty() const { return true; }// skip redraw when false (opt)
};

}  // namespace stow
```

`render()` must confine drawing to `ctx.region`; it uses
`win.set_clip_region(region)` / `clear_region` and the existing draw helpers.
This is exactly the `pump_region`/`draw_region` contract shud already relies on.

### Dashboard orchestrator (`dashboard.hpp`)

`Dashboard` owns the grid, the window(s), and the widgets, and runs the loop
currently inlined in `shud.cpp` main:

```cpp
class Dashboard {
public:
    explicit Dashboard(const GridConfig& grid);

    // programmatic placement
    void add(int row, int col, std::unique_ptr<Widget> w);

    // construct everything from parsed config (builds widgets per CellSpec)
    static Dashboard from_config(const HudConfig& cfg);

    void run();    // setup windows, then the poll/render/fps/toggle loop
    void stop();   // exit run() (used by tests / signal)

    // knobs mirrored from config (single_window, grid_lines, monitor,
    // toggle_key, period) settable before run()
};
```

Responsibilities moved here from `shud.cpp` main (no behavior change):
window creation (single vs per-cell), monitor geometry, `poll()` over widget
fds, fps accounting, mouse query, grid-line drawing, the `toggle_key` grab and
interactive/overlay switch, and per-widget period/restart.

This makes the programmatic path real:

```cpp
stow::GridConfig g = stow::GridConfig::uniform(2, 2);
stow::Dashboard dash(g);
dash.add(0, 0, std::make_unique<stow::CommandWidget>("btop"));
dash.add(0, 1, std::make_unique<stow::NumberWidget>("nproc", "cores"));
dash.add(1, 0, std::make_unique<stow::BarWidget>("cpu_pct.sh", "cpu", 100));
dash.run();
```

## Widget set & phasing

The `Widget` interface is the stable contract. Concrete widgets ship in phases
so v1 is buildable and verifiable; the rest are designed-in via the same
interface, not afterthoughts.

| Widget   | Phase | Draws with | Data source | Notes |
|----------|-------|-----------|-------------|-------|
| Command  | v1    | `draw_region_spans` (PTY → ColorSpans) | PTYProcess (existing) | Wraps today's behavior |
| Hud      | v1    | `draw_region` text | `DashStats` + built-in fields | `fields = time,fps,mouse` |
| Number   | v1    | `draw_region` text (large) | command stdout → parsed number/text | `label` + value |
| Bar      | v1    | `XFillRectangle` + text | command stdout → number, `max` | horizontal gauge |
| Plot     | v2    | `XDrawLine` polyline | command stdout → number, ring-buffer history | sparkline; needs sample history |
| Tab      | v2    | delegates to child widgets | child widgets | needs focus/input routing; reuses existing key grab + ButtonPress infra |

v1 spans the three rendering modes (colored text, plain/large text, geometric
fill+line), which proves the API is sufficient for v2's plot/tab. **Plot and Tab
are explicitly out of scope for the first implementation** but their interfaces
(history buffer for Plot, child-widget container + input routing for Tab) are
noted so v1 does not paint us into a corner.

### Data source model

A widget that shows a value runs its `cmd` every `period` seconds via the
existing `PTYProcess`, and parses the **last non-empty stdout line**:
- Number: parse as `double`; if non-numeric, show raw text.
- Bar: parse as `double`, normalize against `max` → fill fraction.
- Plot (v2): push parsed `double` into a fixed-size ring buffer.

Command widget consumes the full streamed output as today (no parsing).

## Error handling

- `HudConfig::load` **never throws**. Bad lines (non-int where int expected,
  unknown keys, malformed `at`) append to `HudConfig::error` and parsing
  continues; `valid()` returns false if `error` is non-empty or the grid is
  invalid.
- `shud.cpp` prints `cfg.error` and exits non-zero (same shape as today's
  `std::cerr` + `return 1`).
- A widget whose `cmd` fails to spawn renders an error string in its cell rather
  than aborting the dashboard.

## Testing

- `test/test_hud_config.cpp` — **headless** (no X), fast, deterministic. Covers:
  full valid config round-trips to expected `HudConfig`; `at=` placement;
  fill-order fallback; per-cell appearance inherit vs override; each widget kind
  parses its keys; bad/missing required keys set `error` and `valid()==false`;
  unknown `fields`/keys handled without crashing. Registered in ctest.
- `Dashboard`/widget tests reuse the existing X-demo convention: a `timeout_sec`
  arg bounds the loop (as just added to `test_grid`), so they self-terminate
  under ctest where a display is available.
- Migrated `test/hud_test.ini` doubles as an integration fixture for
  `test_dashboard`.

## Open questions for review

1. Keep the legacy flat INI working, or migrate-only (current plan: migrate)?
2. Is the v1 widget set (Command, Hud, Number, Bar) the right cut, or should
   Plot be pulled into v1?
3. Retire `test_grid.cpp` in favour of `test_dashboard`, or keep it as a thin
   demo?
```
