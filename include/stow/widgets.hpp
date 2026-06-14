// Stow concrete dashboard widgets + their pure value helpers.
#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>

#include "widget.hpp"
#include "xwindow.hpp"

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

// Renders a set of built-in status fields. `fields` is the ordered list from config.
class HudWidget : public Widget {
public:
	explicit HudWidget(std::vector<std::string> fields) : _fields(std::move(fields)) {
		if(_fields.empty()) _fields = {"time", "fps", "mouse"};
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win || !ctx.stats) return;
		const DashStats& s = *ctx.stats;
		std::ostringstream out;
		out << std::fixed << std::setprecision(1);
		for(const std::string& f : _fields) {
			if(f == "time") {
				char buf[32];
				std::tm* lt = std::localtime(&s.now);
				if(lt && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt)) out << "time: " << buf << "\n";
				else out << "time: ?\n";
			} else if(f == "fps") {
				out << "fps: " << s.fps << "\n";
			} else if(f == "mouse") {
				out << "mouse: " << s.mouse_x << "," << s.mouse_y << "\n";
			} else {
				out << f << ": ?\n";  // unknown field shown literally
			}
		}
		const Rect& r = ctx.region;
		ctx.win->draw_region(out.str(), r.x, r.y, r.width, r.height);
	}

private:
	std::vector<std::string> _fields;
};

}  // namespace stow
