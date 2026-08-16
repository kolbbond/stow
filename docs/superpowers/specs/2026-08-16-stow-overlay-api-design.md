# Stow Overlay API — Design

Date: 2026-08-16
Status: Draft (awaiting review)

## Goal

Turn stow from "two binaries plus a pile of headers" into a **library with a
public API**, so another program can compile in the overlay and HUD features by
including one header and linking one target.

The centerpiece is a **lightweight click-through overlay**: a single window you
draw text and shapes into, with no X11 in the caller's sight.

```cpp
#include <stow/overlay.hpp>

int main() {
  stow::Overlay ov({.anchor = stow::Anchor::TopRight,
                    .font   = "monospace:size=12",
                    .alpha  = 0.8});
  ov.show();
  while (ov.pump())
    ov.set_text(read_status());
}
```

`stow` and `shud` are rebuilt as consumers of that API, so the boundary is
exercised by real code rather than asserted.

## Non-goals

- **No daemon and no IPC.** A push-model HUD daemon (`stowd` + `stowctl`,
  producers writing to named slots over a socket) was explored and deferred. It
  is a natural second consumer of this library and can be designed on top of it
  later; nothing here should block it, but no protocol is specified now.
- **No new third-party dependencies.** X11 + Xft + freetype, as today.
- **No C ABI.** The public surface is C++20. An `extern "C"` shim is a later
  question, driven by an actual non-C++ consumer.
- **No threading.** The caller owns the loop; the overlay is used from one
  thread. `Overlay::spawn()` on a render thread was considered and rejected —
  X11 plus threads buys a class of bugs the use case does not need.
- **No Windows work in this pass.** `win32window.hpp` / `win32*process.hpp` stay
  where they are, unbuilt on POSIX. The pimpl boundary makes a Win32 backend
  *possible* later; implementing it is out of scope.
- **No rewrite of the terminal emulator.** `PTYProcess` / `ScreenBuffer` move
  behind the line unchanged.

## Architecture

Three tiers. Each is usable alone; a consumer pays only for the tier it takes.

```
  Tier 3   stow::Hud        ini file -> full dashboard      (shud's engine)
              |
  Tier 2   stow::Grid       named cells, widgets, layout
              |
  Tier 1   stow::Overlay    one click-through window        <- the lightweight core
```

The seam mostly exists already. `layout.hpp`, `grid.hpp`, `config.hpp`,
`color_span.hpp` and `capture.hpp` are pure logic with no X11. Only three
headers include `X11/`: `xwindow.hpp`, `stow/monitor.hpp`, `stow/dashboard.hpp`.
This design draws the line where it nearly falls today.

### File layout

```
include/stow/          PUBLIC — the API. No X11 include anywhere in this tree.
    overlay.hpp          Tier 1: Overlay, OverlayConfig, Color, Caps
    screen.hpp           pointer(), monitors(), screen_size()
    text.hpp             ColorSpan, Line
    config.hpp           WindowConfig, Anchor, Position      (exists, clean)
    layout.hpp           Rect, AnchorLayout, PositionLayout  (exists, clean)
    grid.hpp             Tier 2: GridConfig, GridLayout      (exists, clean)
    widget.hpp           Tier 2: Widget, RenderCtx
    hud.hpp              Tier 3: Hud, HudConfig, CellSpec

src/                   PRIVATE — X11 lives here; never installed.
    overlay.cpp          Overlay pimpl -> x11 backend
    x11/window.cpp       from include/xwindow.hpp
    x11/monitor.cpp      from include/stow/monitor.hpp
    x11/screen.cpp       pointer()/monitors() X11 implementation
    hud/dashboard.cpp    from include/stow/dashboard.hpp
    hud/widgets.cpp      from include/stow/widgets.hpp
    hud/config.cpp       ini parsing from include/stow/hud_config.hpp
    proc/pty.cpp         from include/ptyprocess.hpp
    proc/pipe.cpp        from include/pipeprocess.hpp
    proc/screen_buffer.cpp
    bin/stow.cpp         the watch-like CLI      (consumer of the API)
    bin/shud.cpp         the ini HUD             (consumer of the API)
```

An include of `<X11/...>` under `include/` is a design violation. This is
mechanically checkable and becomes a CI grep (see Testing).

### Tier 1: `stow::Overlay`

The public type. Holds a pimpl to the X11 backend; the header names no X11 type.

```cpp
namespace stow {

struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;
  static constexpr Color red();     // and a small set of named helpers
  static Color parse(std::string_view);   // "#rrggbb" / "#rrggbbaa"
};

struct Size { unsigned w = 0, h = 0; };

// What the compositor actually gave us. Never a reason to fail construction.
struct Caps {
  bool clickthrough  = false;   // XShape input region applied
  bool transparency  = false;   // ARGB visual obtained
  bool override_redirect = false;
};

struct OverlayConfig {
  Anchor      anchor = Anchor::Custom;
  Position    x, y;                 // used when anchor == Custom
  Size        size;                 // 0,0 => size to content
  int         monitor = -1;         // -1 => primary
  std::string font  = "monospace:size=10";
  Color       fg    = Color::parse("#00a080");
  Color       bg    = Color::parse("#000000");
  double      alpha = 0.8;
  bool        clickthrough = true;
  bool        on_top       = true;
  bool        borderless   = true;
  std::string title = "stow";
};

class Overlay {
public:
  // Degrades rather than throws. See "Error handling".
  // Empty only when there is no usable display; *err then holds the reason.
  static std::optional<Overlay> create(const OverlayConfig& = {}, Error* err = nullptr);
  explicit Overlay(const OverlayConfig& = {});   // same, but throws Error instead

  Overlay(Overlay&&) noexcept;
  Overlay& operator=(Overlay&&) noexcept;
  Overlay(const Overlay&) = delete;
  ~Overlay();

  // Lifecycle
  void show();
  void hide();
  void close();
  bool open() const;
  Caps caps() const;

  // Geometry (runtime, not just at construction)
  void move_to(int x, int y);
  void resize(Size);
  Rect geometry() const;

  // Immediate-mode drawing. begin() clears, end() presents.
  void begin();
  void rect(Rect, Color, int thickness = 0);   // thickness 0 => filled
  void text(int x, int y, std::string_view, Color = {});
  void spans(const std::vector<std::vector<ColorSpan>>&);
  void end();

  // Convenience: equivalent to begin(); spans(...)/text(...); end();
  void set_text(std::string_view);
  void set_spans(const std::vector<std::vector<ColorSpan>>&);

  // Event pump. Returns false once the overlay is closed.
  bool pump();                                  // non-blocking
  bool pump(std::chrono::milliseconds timeout); // waits up to timeout on X events

  // Convenience loop for pure-HUD programs. Literally while(pump(period)) cb(*this);
  void run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb);

private:
  struct Impl;
  std::unique_ptr<Impl> _p;
};

}  // namespace stow
```

Design notes:

- **Immediate mode is the primitive.** A cursor-follower changes content every
  frame; a retained display list would be the wrong default for it. `set_text()`
  remains a one-liner over `begin/text/end`, so the watch-style use is no more
  verbose than today.
- **`pump()` has two forms.** Argument-less returns immediately, for embedding
  in a caller's existing loop. The timeout form blocks on X events up to the
  timeout, which is how a caller gets a frame rate without a busy-spin.
- **Move semantics, not `shared_ptr`.** `XWindow::create()` returning
  `ShXWindowPr` exists because widgets hold back-references to the window. Once
  `RenderCtx` carries an `Overlay&`, the shared ownership has no remaining
  justification, and a move-only RAII handle is the smaller thing to explain.

### `stow/screen.hpp`

Pointer and monitor position are screen facts, not properties of a window.
`XQueryPointer` is currently inline inside `Dashboard`'s loop
(`dashboard.hpp:239`), unreachable from Tier 1.

```cpp
namespace stow {
struct Point { int x = 0, y = 0; };
struct Monitor { int index; Rect bounds; std::string name; bool primary; };

Point                pointer();          // global cursor position
std::vector<Monitor> monitors();         // XRandR, Xinerama, or single fallback
Monitor              primary_monitor();
}
```

`MonitorManager` already implements the hard part; this is a free-function face
over it with the `Display*` moved to `src/x11/`.

### Tier 2 and Tier 3

Largely as they are today, with two changes:

- `RenderCtx` carries `Overlay& win` instead of `XWindow* win`.
- `BarWidget` draws through `Overlay::rect()` instead of reaching into
  `w._dpy, w._drawable, w._xgc` (`widgets.hpp:125-127`). That reach-through is
  the clearest evidence the drawing API was missing; `rect()` is its fix.

`HudConfig` ini parsing, `GridLayout`, and the widget set are unchanged in
behavior.

### What happens to existing files

Nothing is deleted except one class. Most code moves behind the line.

| Today | Becomes | Note |
|---|---|---|
| `include/xwindow.hpp` (507 lines, all inline) | `src/x11/window.cpp` | private; `Overlay` is the public face |
| `include/window.hpp` (`StowWindow`) | **deleted** | see below |
| `include/stow/monitor.hpp` | `src/x11/monitor.cpp` + `include/stow/screen.hpp` | X11 half private, query half public |
| `include/stow/dashboard.hpp` | `src/hud/dashboard.cpp` | Tier 3 internals |
| `include/stow/widgets.hpp` | `src/hud/widgets.cpp` | drops the raw-handle reach-through |
| `include/stow/hud_config.hpp` | `include/stow/hud.hpp` (types) + `src/hud/config.cpp` (parser) | |
| `include/stow/{layout,grid,config}.hpp` | stay public, unchanged | already X11-free |
| `include/color_span.hpp` | `include/stow/text.hpp` | namespaced into `stow::` |
| `include/{ptyprocess,pipeprocess,screen_buffer}.hpp` | `src/proc/` | private; not part of the overlay API |
| `include/{error,platform}.hpp` | `include/stow/error.hpp`, `src/platform.hpp` | |
| `include/win32*.hpp` | untouched | not built; out of scope |
| `src/stow.cpp`, `src/shud.cpp` | `src/bin/` | now API consumers |

**`StowWindow` is deleted.** It is an abstract base whose only purpose is
swapping in a Win32 implementation at compile time. Once X11 is behind a pimpl,
`Overlay::Impl` is the swap point, and the vtable indirection stops earning its
keep. Removing it also removes the public `_screen_width` / `_hidden` /
`_override_redirect` data members that currently form an accidental API surface.

**Naming.** Everything public moves into `namespace stow`. Today `XWindow`,
`StowWindow`, `ColorSpan`, `ShXWindowPr` and `gconf` sit at global scope while
newer types are namespaced; that split ends.

## Build

`stowlib` (INTERFACE) becomes a compiled static library:

```cmake
add_library(stow STATIC ${STOW_SOURCES})
target_include_directories(stow
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${FREETYPE_INCLUDE_DIRS})
target_link_libraries(stow PRIVATE freetype X11 Xft Xext Xfixes util)
add_library(stow::stow ALIAS stow)
```

The `PRIVATE` on the link libraries is the change that makes this an API: today
`INTERFACE` pushes X11 include paths and link flags onto everyone who includes a
header. After this, a consumer writes:

```cmake
find_package(stow REQUIRED)
target_link_libraries(mytool PRIVATE stow::stow)
```

Install ships `include/stow/*.hpp`, `libstow.a`, and a generated
`stow-config.cmake` so `find_package(stow)` resolves from an unrelated project.

`legacy/` note: `src/bak/stow.cpp` and `bak/` are reference-only and stay
excluded from the build, as today.

## Error handling

The CLAUDE.md requirement is explicit: on WSLg, Xfixes and ARGB visuals may be
unavailable, and the window must still display and render text. So **capability
loss degrades; it does not fail.**

- Missing Xfixes/XShape → `Caps::clickthrough = false`, window still renders.
- No ARGB visual → `Caps::transparency = false`, opaque background, still renders.
- **No display at all** is the one genuine failure. `Overlay::create()` returns
  an empty `std::optional` and fills the optional `Error*`; the throwing
  constructor exists for callers who prefer the short form.

**The public headers stay C++20**, matching the current `CMAKE_CXX_STANDARD`.
`std::expected` would read better than `optional` + out-param here, but it is
C++23, and the point of this work is a library that is cheap to link — pushing a
language-version requirement onto every consumer is a poor trade for one return
type. The library's own `src/` may use C++23 freely; only `include/stow/` is
constrained.

`Error` carries a code and a message. The X error handler currently installed by
hand in `src/shud.cpp:8-13` moves into the library, so consumers do not have to
know that X11 error handling is global process state.

## Testing

The current suite is ten demo runners that mostly cannot fail — `ctest` passing
carries little information. Split it:

**Headless unit tests, real assertions, run in CI.** No `$DISPLAY` needed:

- `test_layout` — anchor and position resolution, edge/negative/percent cases
- `test_grid` — cell rects, rounding, `fit_to_cells` on and off
- `test_hud_config` — ini parsing, validation, error strings
- `test_screen_buffer` — ANSI/SGR handling, scrolling, 256-color and truecolor
- `test_color` — `Color::parse` round-trips, malformed input

**X11 integration tests, timeout-bounded, skipped when `$DISPLAY` is unset**
(skipped, not failed — a headless CI box should be green):

- `test_cursor` — the red-box pointer follower; doubles as the click-through check
- `test_overlay` — construct, show, draw, move, resize, close; assert `caps()`
- `test_hud` — `shud` against `hud_test.ini` with a timeout, as today

**A grep test.** `include/` must contain no `X11/` include. One line in CI, and
it is what keeps the boundary from eroding.

The `test_cursor` demo, in full, is the spec's acceptance example:

```cpp
#include <stow/overlay.hpp>
#include <stow/screen.hpp>
#include <format>

int main() {
  stow::Overlay ov({.size = {120, 40}, .clickthrough = true});
  ov.show();

  while (ov.pump(std::chrono::milliseconds(16))) {
    auto p = stow::pointer();
    ov.move_to(p.x + 12, p.y + 12);

    ov.begin();
    ov.rect({0, 0, 120, 40}, stow::Color::red(), 2);
    ov.text(6, 20, std::format("{},{}", p.x, p.y));
    ov.end();
  }
}
```

**Compositor risk, stated plainly.** A click-through window that tracks the
pointer is straightforward on native X11. On Hyprland/XWayland — the actual
development machine — pointer coordinates and window placement are the parts
most likely to misbehave, and `ShapeInput` is known to reset on resize (see the
project memory: use `XShapeCombineRectangles`, not
`XFixesSetWindowShapeRegion`, and re-apply after resize). `test_cursor` is
therefore both the demo and the honest smoke test for the overlay path on this
compositor. If following the pointer proves unreliable there, the finding is
reported rather than papered over; the API does not change either way.

## Implementation sequence

Six steps. Each ends with the tree building and `ctest` green, so the repo is
never half-migrated.

1. **Static lib.** `stowlib` INTERFACE → `libstow.a`, X11 linkage `PRIVATE`,
   sources split from headers into `src/`. No API change; pure build move.
2. **`Overlay` pimpl** over the existing `XWindow`, plus `stow/screen.hpp`
   (`pointer()`, `monitors()`). Both old and new paths work at this point.
3. **Immediate-mode drawing:** `begin`/`rect`/`text`/`end`, `move_to`,
   `resize`, `pump(timeout)`, `caps()`.
4. **`test_cursor`** — the red-box follower. First real consumer of Tier 1, and
   the click-through regression check.
5. **Port `stow` and `shud`** onto the public API; `BarWidget` stops touching
   `_dpy`/`_xgc`; `RenderCtx` carries `Overlay&`.
6. **Delete `StowWindow`**, namespace the stragglers, add install rules and
   `stow-config.cmake`, rewrite README around the two examples.

Test rework (headless assertions + the `X11/` grep) lands with the step that
touches each area rather than as a seventh step.

## Open questions

None blocking. Two things deliberately left for later, recorded so they are not
rediscovered as surprises:

- **The push daemon.** `stowd` + `stowctl`, declared slots in an ini, producers
  pushing content to named cells over a unix socket. Wanted, deferred, and
  unblocked by this design — it becomes a third consumer of Tier 2.
- **Win32 backend.** The pimpl makes it possible; the existing `win32*.hpp`
  files are a starting point. Not scheduled.
