# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STW (Stow) is an X11 text overlay window manager written in C++ that displays command output in a transparent, click-through overlay window. It supports ANSI colors through pseudo-terminal emulation and uses Xfixes for input passthrough.

## Build Commands

```bash
# Build
mkdir -p build && cd build
cmake ..
make

# Run tests
ctest

# Run specific test
./build/bin/test_stow
./build/bin/test_xwindow
./build/bin/test_process
./build/bin/test_fullscreen
./build/bin/test_grid
```

Dependencies: freetype, X11, Xft, Xext, Xfixes

## Architecture

### Core Components

**XWindow** (`include/xwindow.hpp`): X11 window management
- Creates ARGB transparent windows with alpha blending
- Renders colored text spans using Xft
- Implements click-through using Xfixes shape regions
- Handles positioning (absolute, percentage, relative)

**Process hierarchy** (`include/process.hpp`, `include/pipeprocess.hpp`, `include/ptyprocess.hpp`):
- `Process`: Abstract base class for command execution
- `PipeProcess`: Simple pipe-based subprocess I/O
- `PTYProcess`: Full pseudo-terminal with ANSI escape sequence parsing and terminal emulation

**Config** (`include/config.h`): Global configuration struct (`gconf`) with defaults for geometry, colors, fonts, transparency

### Key Patterns

```cpp
// Window creation and rendering
ShXWindowPr xwin = XWindow::create();
xwin->setup();
xwin->draw_spans(colored_spans);  // vector<vector<ColorSpan>>
xwin->run();

// Process execution
ShProcessPr process = PTYProcess::create();
process->setup();
process->start_cmd("command", args);
process->read_text(xwin);  // Polls, reads, and renders to window
```

### PTYProcess Terminal Emulation

PTYProcess maintains a `ScreenBuffer` with per-cell color state and implements:
- ANSI escape sequences (CSI, OSC, SGR)
- 16 basic colors, 256-color palette, 24-bit true color
- Cursor movement, line clearing, scrolling

The `pump()` method reads from the PTY, updates the screen buffer, and converts to `ColorSpan` vectors for rendering.

### Main Execution Loop

See `test/test_stow.cpp` for the canonical pattern: create XWindow once, then repeatedly create PTYProcess instances that execute commands and render output, sleeping for `gconf.period` between runs.

## Development Requirements

- **Overlay mode**: Use click-through (Xfixes) and transparency when running on native X11
- **WSLg compatibility**: Must also work on WSLg for development, where Xfixes or ARGB visuals may not be fully supported. Gracefully degrade when overlay features are unavailable - the window should still display and render text even without click-through or transparency.

## Codebase Notes

- Legacy C implementation exists in `src/stow.cpp` (reference only)
- Modern C++ implementation is in test files and headers
- Current branch is `refactor` - actively modernizing from C to C++
- Uses smart pointers (`ShXWindowPr`, `ShProcessPr`) for resource management
