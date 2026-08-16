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
