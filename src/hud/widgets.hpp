// Stow concrete dashboard widgets + their pure value helpers.
#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <ctime>
#include <memory>

#include "stow/widget.hpp"
#include "stow/capture.hpp"
#include "proc/ptyprocess.hpp"

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
		ctx.win->text_in(ctx.region, out.str());
	}

private:
	std::vector<std::string> _fields;
};

// Runs a command every `period` seconds, shows its last stdout line + a label.
class NumberWidget : public Widget {
public:
	NumberWidget(std::string cmd, std::string label, int period)
		: _cmd(std::move(cmd)), _label(std::move(label)), _period(period < 1 ? 1 : period) {}

	void update() override {
		std::time_t t = std::time(nullptr);
		if(t - _last < _period && !_value.empty()) return;
		_last = t;
		_value = capture_command(_cmd);
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win) return;
		ctx.win->text_in(ctx.region, number_text(_label, _value));
	}

private:
	std::string _cmd, _label, _value;
	int _period;
	std::time_t _last = 0;
};

// Runs a command every `period` seconds, draws a horizontal gauge of value/max.
class BarWidget : public Widget {
public:
	BarWidget(std::string cmd, std::string label, double max, int period, Color fg = Color::parse("#00a080"))
		: _cmd(std::move(cmd)), _label(std::move(label)), _max(max), _period(period < 1 ? 1 : period), _fg(fg) {}

	void update() override {
		std::time_t t = std::time(nullptr);
		if(t - _last < _period && !_value.empty()) return;
		_last = t;
		_value = capture_command(_cmd);
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win) return;
		Overlay& w = *ctx.win;
		const Rect& r = ctx.region;
		double frac = bar_fraction(_value, _max);

		// label + value text on top
		w.text_in(r, number_text(_label, _value));

		// gauge: outline + filled portion along the bottom of the cell
		int pad = 4;
		int bh = 12;
		int bx = r.x + pad;
		int by = r.y + static_cast<int>(r.height) - bh - pad;
		int bw = static_cast<int>(r.width) - 2 * pad;
		if(bw <= 0 || by <= r.y) return;

		// Previously this used the window's raw _xforeground pixel. That is an
		// allocated X pixel value, not an RGB triple; the configured foreground
		// color is the correct equivalent and needs no X11 handles.
		w.rect(Rect{bx, by, static_cast<unsigned int>(bw), static_cast<unsigned int>(bh)}, _fg, 1);
		int fillw = static_cast<int>(frac * bw);
		if(fillw > 0) w.rect(Rect{bx, by, static_cast<unsigned int>(fillw), static_cast<unsigned int>(bh)}, _fg, 0);
	}

private:
	std::string _cmd, _label, _value;
	double _max;
	int _period;
	Color _fg;
	std::time_t _last = 0;
};

// Streams a command's full output (PTY, ANSI colors) into its cell, restarting
// every `period` seconds. Wraps the existing PTYProcess behavior.
class CommandWidget : public Widget {
public:
	CommandWidget(std::string cmd, std::vector<std::string> args, int period)
		: _cmd(std::move(cmd)), _args(std::move(args)), _period(period < 1 ? 1 : period) {}

	int fd() const override { return _proc ? _proc->fd() : -1; }

	void update() override {
		std::time_t now = std::time(nullptr);
		if(!_proc) {
			_proc = PTYProcess::create();
			_proc->setup();
			_proc->start_cmd(_cmd, _args);
			_done = false;
		} else if(_done && now >= _restart_at) {
			_proc = PTYProcess::create();
			_proc->setup();
			_proc->start_cmd(_cmd, _args);
			_done = false;
		}
		if(_proc && _proc->is_done() && !_done) {
			_done = true;
			_restart_at = now + _period;
		}
	}

	void render(RenderCtx& ctx) override {
		if(!ctx.win || !_proc || _done) return;
		_proc->pump_region(*ctx.win, ctx.region);
	}

private:
	std::string _cmd;
	std::vector<std::string> _args;
	int _period;
	ShPTYProcessPr _proc;
	bool _done = false;
	std::time_t _restart_at = 0;
};

}  // namespace stow
