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
