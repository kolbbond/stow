# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STW (Stow) is a **C++ library for click-through overlay windows on X11**, plus
two binaries built on it (`stow`, `shud`). The overlay is transparent,
always-on-top, and passes input through to whatever is underneath.

## Build Commands

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build
```

`ctest` runs **only the headless tests by default** — nothing appears on
screen. The X11 tests map real windows over the live desktop and are opt-in:

```bash
cmake -S . -B build -DSTOW_TEST_X11=ON
ctest --test-dir build            # everything
ctest --test-dir build -L x11     # just the windowed ones
```

Respect that default. Someone is usually sitting at this machine, and a
routine `ctest` that throws overlay windows around for 20 seconds takes their
screen away.

Without `$DISPLAY`, X11 tests return 77 and report *skipped*, not failed.

Dependencies: freetype, X11, Xft, Xext, Xfixes (+ Xrandr/Xinerama, optional).

## Architecture

Three tiers, each usable alone:

```
  stow::Hud       ini file -> grid dashboard      (src/hud/, public face stow/hud.hpp)
     |
  stow::Grid      cells, widgets, layout          (stow/grid.hpp, stow/widget.hpp)
     |
  stow::Overlay   one click-through window        (stow/overlay.hpp)  <- the core
```

### The one rule

```
include/stow/     PUBLIC API — no X11 include may appear here, ever
src/              private implementation; never installed
  x11/              window.hpp (the real X11 code), monitor, screen
  proc/             pty/pipe subprocess + terminal emulation
  hud/              dashboard, widgets, capture
  bin/              stow and shud
```

`test_no_x11_in_public_headers` enforces this and will fail the build's test
suite if broken. `stow::Overlay` hides X11 behind a pimpl (`src/overlay.cpp`);
`XWindow` in `src/x11/window.hpp` is the implementation body, not API.

CMake: `stow` is the static library (X11 linked `PRIVATE`, so consumers inherit
no include path). `stowlegacy` is an **internal** interface target that adds
`src/` to the include path — used by the `stow` binary and internals tests, and
never installed. `shud` links `stow::stow` alone on purpose: it is the standing
proof that the whole feature set is reachable from the public API.

### Canonical usage

```cpp
#include <stow/overlay.hpp>

stow::Overlay ov({.anchor = stow::Anchor::TopRight, .font = "monospace:size=12"});
ov.show();
while (ov.pump()) ov.set_text(status());
```

Immediate mode, for per-frame content:

```cpp
ov.begin();
ov.rect({0, 0, 120, 40}, stow::Color::red(), 2);
ov.text(6, 24, "hello");
ov.end();
ov.pump(std::chrono::milliseconds(16));
```

The caller owns the loop. `pump()` is non-blocking; `pump(timeout)` waits on
the X connection.

## Gotchas worth knowing

- **`Error::Code::Ok`, not `None`.** X11 `#define`s `None`, so a consumer that
  includes X11 first would not compile.
- **Alpha on 32-bit visuals.** `XftColorAllocValue` fills `pixel` from the
  visual's RGB masks only — Xlib's `Visual` has no alpha mask — so alpha comes
  back zero and anything drawn with core X11 (`XFillRectangle`) is invisible.
  `XWindow::solid_pixel()` forces it opaque. Xft *text* is unaffected because it
  renders from the `XRenderColor`.
- **Zero-sized windows.** `XMoveResizeWindow` with a zero extent is a BadValue.
  `XWindow::run()` treats zero size as hidden, which is also what stow wants
  when a command produces no output.
- **XWayland resets `ShapeInput` on resize**, so click-through must be
  re-applied after every move/resize (`XWindow::run()` does this). Use
  `XShapeCombineRectangles`, not `XFixesSetWindowShapeRegion` — the latter does
  not work for input passthrough on Hyprland/XWayland.
- **Capability loss degrades, never fails.** Missing XShape or ARGB must still
  render (WSLg requirement). Report it via `caps()`; only "no display" is an
  error.
- **Public headers are C++20.** `src/` may use newer.

## Testing notes

Tests split two ways:

- **Headless** (`test_*_unit`, `test_hud_config`, `test_no_x11_in_public_headers`) —
  real assertions via `test/check.hpp`, no display needed, run by default.
- **X11** (`test_overlay`, `test_cursor`, `test_screen`, `test_hud`, and the
  older demos) — need `$DISPLAY`, gated behind `-DSTOW_TEST_X11=ON`.

`test/check.hpp` is a hand-rolled `CHECK`/`CHECK_EQ`/`CHECK_REPORT` harness —
no third-party test framework, by design.

**A caution learned the hard way:** the alpha bug above shipped through a full
green suite because every test only checked that drawing did not *crash*, never
what was actually painted. When touching rendering, assert on pixel values
(`Overlay::debug_solid_pixel`) or capture the window with `xwd`.

## Codebase notes

- `include/win32*.hpp` are unbuilt legacy files kept for a possible Win32
  backend. They are not on the build path and are excluded from the public-API
  header check.
- `src/bak/`, `bak/` are reference-only and not built.
- `test/hud.ini` is in an old unsectioned format and does not load;
  `test/hud_test.ini` is the current one.
- Design and plan documents live in `docs/superpowers/`.
