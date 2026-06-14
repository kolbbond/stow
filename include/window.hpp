// Abstract Window base class for cross-platform support
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "platform.hpp"
#include "color_span.hpp"

class StowWindow;
using ShWindowPtr = std::shared_ptr<StowWindow>;

class StowWindow {
public:
	virtual ~StowWindow() = default;

	// Factory method - returns platform-specific implementation
	static ShWindowPtr create();

	// Lifecycle
	virtual void setup() = 0;
	virtual void run() = 0;

	// Drawing
	virtual void draw(const std::string& text) = 0;
	virtual void draw_region(const std::string& text, int rx, int ry, unsigned int rw, unsigned int rh) = 0;
	virtual void draw_spans(const std::vector<std::vector<ColorSpan>>& lines) = 0;
	virtual void draw_region_spans(const std::vector<std::vector<ColorSpan>>& lines, int rx, int ry,
		unsigned int rw, unsigned int rh) = 0;

	// Properties (common across platforms)
	unsigned int _screen_width = 0;
	unsigned int _screen_height = 0;
	unsigned int _window_width = 0;
	unsigned int _window_height = 0;
	bool _hidden = true;
	bool _dirty = true;
	bool _overlay = true;
	bool _override_redirect = true;
	bool _transparent_background = true;
	bool _fullscreen = false;
	bool _borderless = false;
	bool _use_fixed_geometry = false;
	int _fixed_x = 0;
	int _fixed_y = 0;
	unsigned int _fixed_w = 0;
	unsigned int _fixed_h = 0;
};
