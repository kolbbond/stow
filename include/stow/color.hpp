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

	static constexpr Color red() { return {255, 0, 0, 255}; }
	static constexpr Color green() { return {0, 255, 0, 255}; }
	static constexpr Color blue() { return {0, 0, 255, 255}; }
	static constexpr Color white() { return {255, 255, 255, 255}; }
	static constexpr Color black() { return {0, 0, 0, 255}; }
	static constexpr Color none() { return {0, 0, 0, 0}; }

	// "#rrggbb", "#rrggbbaa", or the same without the leading '#'.
	// Malformed input yields opaque black. Parsing a color is not worth an
	// exception or an error channel; a visibly wrong color is signal enough.
	static constexpr Color parse(std::string_view s) {
		if(!s.empty() && s.front() == '#') s.remove_prefix(1);
		if(s.size() != 6 && s.size() != 8) return black();

		std::uint32_t v = 0;
		for(char ch : s) {
			int d = 0;
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
};

}  // namespace stow
