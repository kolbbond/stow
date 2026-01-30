// grid runner: split screen into cells and run commands serially - POSIX/X11 only
#include "platform.hpp"

#if STOW_POSIX

#include "ptyprocess.hpp"
#include "xwindow.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cctype>
#include <iomanip>
#include <unistd.h>
#include <poll.h>

struct GridConfig {
	int rows = 0;
	int cols = 0;
	std::vector<int> row_heights;
	std::vector<int> col_widths;
	std::vector<std::string> cells;
	bool single_window = false;
	bool grid_lines = true;
	bool fit_to_cells = true;
	bool buttons = false;
	int button_x = 10;
	int button_y = 10;
	unsigned int button_w = 120;
	unsigned int button_h = 30;
	std::string button_label = "Restart";
};

static std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static bool is_hud_cell(const std::string& s) {
	std::string t = trim(s);
	if(t.empty()) return false;
	for(char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return t == "hud";
}

static std::string format_time_now() {
	std::time_t now = std::time(nullptr);
	std::tm* local = std::localtime(&now);
	char buf[32];
	if(local && std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", local)) {
		return std::string(buf);
	}
	return "unknown";
}

static std::vector<int> parse_csv_ints(const std::string& s) {
	std::vector<int> out;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) {
		item = trim(item);
		if(item.empty()) continue;
		out.push_back(std::stoi(item));
	}
	return out;
}

static std::vector<std::string> split_cmd(const std::string& s) {
	std::vector<std::string> out;
	std::string cur;
	char quote = 0;
	for(size_t i = 0; i < s.size(); i++) {
		char c = s[i];
		if(quote) {
			if(c == quote) {
				quote = 0;
			} else {
				cur.push_back(c);
			}
			continue;
		}
		if(c == '"' || c == '\'') {
			quote = c;
			continue;
		}
		if(c == ' ' || c == '\t') {
			if(!cur.empty()) {
				out.push_back(cur);
				cur.clear();
			}
			continue;
		}
		cur.push_back(c);
	}
	if(!cur.empty()) out.push_back(cur);
	return out;
}

static GridConfig load_config(const std::string& path) {
	GridConfig cfg;
	std::ifstream in(path);
	if(!in) {
		std::cerr << "cannot open config: " << path << "\n";
		return cfg;
	}
	std::string line;
	while(std::getline(in, line)) {
		line = trim(line);
		if(line.empty() || line[0] == '#') continue;
		size_t eq = line.find('=');
		if(eq == std::string::npos) continue;
		std::string key = trim(line.substr(0, eq));
		std::string value = trim(line.substr(eq + 1));
		if(key == "rows") {
			cfg.rows = std::stoi(value);
		} else if(key == "cols") {
			cfg.cols = std::stoi(value);
		} else if(key == "row_heights") {
			cfg.row_heights = parse_csv_ints(value);
		} else if(key == "col_widths") {
			cfg.col_widths = parse_csv_ints(value);
		} else if(key == "cell") {
			cfg.cells.push_back(value);
		} else if(key == "single_window") {
			cfg.single_window = (value == "1" || value == "true" || value == "yes");
		} else if(key == "grid_lines") {
			cfg.grid_lines = !(value == "0" || value == "false" || value == "no");
		} else if(key == "fit_to_cells") {
			cfg.fit_to_cells = (value == "1" || value == "true" || value == "yes");
		} else if(key == "buttons") {
			cfg.buttons = (value == "1" || value == "true" || value == "yes");
		} else if(key == "button_x") {
			cfg.button_x = std::stoi(value);
		} else if(key == "button_y") {
			cfg.button_y = std::stoi(value);
		} else if(key == "button_w") {
			cfg.button_w = static_cast<unsigned int>(std::stoi(value));
		} else if(key == "button_h") {
			cfg.button_h = static_cast<unsigned int>(std::stoi(value));
		} else if(key == "button_label") {
			cfg.button_label = value;
		}
	}
	return cfg;
}

struct CellGeom {
	int x;
	int y;
	unsigned int w;
	unsigned int h;
};

struct CellState {
	ShXWindowPr win;
	std::string cmd;
	std::vector<std::string> args;
	ShPTYProcessPr proc;
	bool done = false;
	time_t restart_at = 0;
};

struct HudState {
	int frames = 0;
	double fps = 0.0;
	std::chrono::steady_clock::time_point last_fps = std::chrono::steady_clock::now();
};

static std::vector<CellGeom> build_grid(const GridConfig& cfg, unsigned int screen_w, unsigned int screen_h,
	unsigned int& out_w, unsigned int& out_h) {
	std::vector<CellGeom> geoms;
	if(cfg.rows <= 0 || cfg.cols <= 0) return geoms;
	if(static_cast<int>(cfg.row_heights.size()) != cfg.rows) return geoms;
	if(static_cast<int>(cfg.col_widths.size()) != cfg.cols) return geoms;

	std::vector<unsigned int> row_px(cfg.rows, 0);
	std::vector<unsigned int> col_px(cfg.cols, 0);

	unsigned int acc_h = 0;
	for(int i = 0; i < cfg.rows; i++) {
		row_px[i] = (screen_h * cfg.row_heights[i]) / 100;
		acc_h += row_px[i];
	}
	if(!cfg.fit_to_cells && acc_h < screen_h && cfg.rows > 0) {
		row_px[cfg.rows - 1] += (screen_h - acc_h);
		acc_h = screen_h;
	}

	unsigned int acc_w = 0;
	for(int i = 0; i < cfg.cols; i++) {
		col_px[i] = (screen_w * cfg.col_widths[i]) / 100;
		acc_w += col_px[i];
	}
	if(!cfg.fit_to_cells && acc_w < screen_w && cfg.cols > 0) {
		col_px[cfg.cols - 1] += (screen_w - acc_w);
		acc_w = screen_w;
	}

	out_w = acc_w;
	out_h = acc_h;

	int y = 0;
	for(int r = 0; r < cfg.rows; r++) {
		int x = 0;
		for(int c = 0; c < cfg.cols; c++) {
			CellGeom g;
			g.x = x;
			g.y = y;
			g.w = col_px[c];
			g.h = row_px[r];
			geoms.push_back(g);
			x += col_px[c];
		}
		y += row_px[r];
	}
	return geoms;
}

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: test_grid <config file>\n";
		return 1;
	}

	GridConfig cfg = load_config(argv[1]);
	if(cfg.rows <= 0 || cfg.cols <= 0) {
		std::cerr << "invalid grid config\n";
		return 1;
	}
	if(static_cast<int>(cfg.row_heights.size()) != cfg.rows ||
		static_cast<int>(cfg.col_widths.size()) != cfg.cols) {
		std::cerr << "row_heights/col_widths must match rows/cols\n";
		return 1;
	}
	if(static_cast<int>(cfg.cells.size()) != cfg.rows * cfg.cols) {
		std::cerr << "cell count must match rows*cols\n";
		return 1;
	}

	// base window for screen size
	ShXWindowPr base = XWindow::create();
	base->_overlay = true;
	base->_override_redirect = false;
	base->_transparent_background = true;
	base->setup();

	unsigned int screen_w = base->_screen_width;
	unsigned int screen_h = base->_screen_height;
	unsigned int grid_w = 0;
	unsigned int grid_h = 0;
	std::vector<CellGeom> geoms = build_grid(cfg, screen_w, screen_h, grid_w, grid_h);
	if(geoms.empty()) {
		std::cerr << "failed to build grid\n";
		return 1;
	}

	std::vector<int> row_ys;
	std::vector<int> col_xs;
	if(cfg.single_window && cfg.grid_lines) {
		int y = 0;
		for(int r = 0; r < cfg.rows; r++) {
			y += geoms[r * cfg.cols].h;
			row_ys.push_back(y);
		}
		int x = 0;
		for(int c = 0; c < cfg.cols; c++) {
			x += geoms[c].w;
			col_xs.push_back(x);
		}
	}

	std::vector<CellState> cells;
	cells.reserve(geoms.size());
	std::vector<bool> hud_cells(geoms.size(), false);

	ShXWindowPr shared;
	if(cfg.single_window) {
		shared = XWindow::create();
		shared->_overlay = !cfg.buttons;
		shared->_override_redirect = false;
		shared->_transparent_background = true;
		shared->_use_fixed_geometry = true;
		shared->_fixed_x = 0;
		shared->_fixed_y = 0;
		shared->_fixed_w = cfg.fit_to_cells ? grid_w : screen_w;
		shared->_fixed_h = cfg.fit_to_cells ? grid_h : screen_h;
		shared->setup();
	}

	for(size_t i = 0; i < geoms.size(); i++) {
		ShXWindowPr xwin;
		if(cfg.single_window) {
			xwin = shared;
		} else {
			xwin = XWindow::create();
			xwin->_overlay = true;
			xwin->_override_redirect = false;
			xwin->_transparent_background = true;
			xwin->_use_fixed_geometry = true;
			xwin->_fixed_x = geoms[i].x;
			xwin->_fixed_y = geoms[i].y;
			xwin->_fixed_w = geoms[i].w;
			xwin->_fixed_h = geoms[i].h;
			xwin->setup();
		}
		CellState cell;
		cell.win = xwin;
		cells.push_back(cell);
		if(i < cfg.cells.size() && is_hud_cell(cfg.cells[i])) {
			hud_cells[i] = true;
		}
	}

	HudState hud;

	while(true) {
		for(size_t i = 0; i < cfg.cells.size(); i++) {
			if(i < hud_cells.size() && hud_cells[i]) continue;
			if(!cells[i].cmd.empty()) continue;
			std::vector<std::string> parts = split_cmd(cfg.cells[i]);
			if(parts.empty()) continue;
			cells[i].cmd = parts[0];
			for(size_t j = 1; j < parts.size(); j++) cells[i].args.push_back(parts[j]);
			cells[i].proc = PTYProcess::create();
			cells[i].proc->setup();
			cells[i].proc->start_cmd(cells[i].cmd, cells[i].args);
		}

		std::vector<struct pollfd> pfds;
		pfds.reserve(cells.size());
		for(size_t i = 0; i < cells.size(); i++) {
			if(cells[i].proc && !cells[i].done) {
				struct pollfd pfd;
				pfd.fd = cells[i].proc->fd();
				pfd.events = POLLIN | POLLHUP | POLLERR;
				pfd.revents = 0;
				pfds.push_back(pfd);
			}
		}

		int timeout_ms = 100;
		if(!pfds.empty()) {
			poll(pfds.data(), pfds.size(), timeout_ms);
		} else {
			usleep(timeout_ms * 1000);
		}

		time_t now = time(NULL);
		hud.frames++;
		auto hud_now = std::chrono::steady_clock::now();
		double hud_elapsed = std::chrono::duration<double>(hud_now - hud.last_fps).count();
		if(hud_elapsed >= 1.0) {
			hud.fps = hud.frames / hud_elapsed;
			hud.frames = 0;
			hud.last_fps = hud_now;
		}

		int mouse_x = 0;
		int mouse_y = 0;
		Window root = 0;
		Window child = 0;
		int win_x = 0;
		int win_y = 0;
		unsigned int mask = 0;
		Display* hud_dpy = shared ? shared->_dpy : base->_dpy;
		Window hud_root = shared ? shared->_root : base->_root;
		if(XQueryPointer(hud_dpy, hud_root, &root, &child, &mouse_x, &mouse_y,
			   &win_x, &win_y, &mask) == False) {
			mouse_x = 0;
			mouse_y = 0;
		}
		size_t pidx = 0;
		bool drew_any = false;
		for(size_t i = 0; i < cells.size(); i++) {
			if(i < hud_cells.size() && hud_cells[i]) continue;
			if(cells[i].proc && !cells[i].done) {
				bool should_pump = true;
				if(pidx < pfds.size()) {
					if(!(pfds[pidx].revents & (POLLIN | POLLHUP | POLLERR))) {
						should_pump = false;
					}
					pidx++;
				}
				if(should_pump) {
					if(cfg.single_window) {
						drew_any |= cells[i].proc->pump_region(
							cells[i].win,
							geoms[i].x,
							geoms[i].y,
							geoms[i].w,
							geoms[i].h);
					} else {
						cells[i].proc->pump(cells[i].win);
					}
				}
				if(cells[i].proc->is_done()) {
					cells[i].done = true;
					cells[i].restart_at = now + gconf.period;
				}
			} else if(cells[i].done && now >= cells[i].restart_at) {
				cells[i].proc = PTYProcess::create();
				cells[i].proc->setup();
				cells[i].proc->start_cmd(cells[i].cmd, cells[i].args);
				cells[i].done = false;
			}
		}

		bool drew_hud = false;
		for(size_t i = 0; i < hud_cells.size(); i++) {
			if(!hud_cells[i]) continue;
			std::ostringstream hud_text;
			hud_text << std::fixed << std::setprecision(1);
			hud_text << "fps: " << hud.fps << "\n";
			hud_text << "mouse: " << mouse_x << "," << mouse_y << "\n";
			hud_text << "time: " << format_time_now() << "\n";
			if(cfg.single_window) {
				shared->draw_region(hud_text.str(), geoms[i].x, geoms[i].y, geoms[i].w, geoms[i].h);
			} else {
				cells[i].win->draw(hud_text.str());
				cells[i].win->run();
			}
			drew_hud = true;
		}
		if(cfg.single_window && (drew_any || drew_hud) && shared) {
			if(cfg.grid_lines) {
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xforeground.pixel);
				for(size_t i = 0; i + 1 < row_ys.size(); i++) {
					XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, 0, row_ys[i],
						grid_w, row_ys[i]);
				}
				for(size_t i = 0; i + 1 < col_xs.size(); i++) {
					XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, col_xs[i], 0,
						col_xs[i], grid_h);
				}
			}
			if(cfg.buttons) {
				int bx = cfg.button_x;
				int by = cfg.button_y;
				unsigned int bw = cfg.button_w;
				unsigned int bh = cfg.button_h;
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xbackground.pixel);
				XFillRectangle(shared->_dpy, shared->_drawable, shared->_xgc, bx, by, bw, bh);
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xforeground.pixel);
				XDrawRectangle(shared->_dpy, shared->_drawable, shared->_xgc, bx, by, bw, bh);

				XGlyphInfo ex;
				XftTextExtentsUtf8(shared->_dpy, shared->_xfont,
					(unsigned char*)cfg.button_label.c_str(), cfg.button_label.size(), &ex);
				int tx = bx + (bw - ex.xOff) / 2;
				int ty = by + (bh - (shared->_xfont->ascent + shared->_xfont->descent)) / 2;
				XftDrawStringUtf8(shared->_xdraw, &shared->_xforeground, shared->_xfont,
					tx, ty + shared->_xfont->ascent,
					(unsigned char*)cfg.button_label.c_str(), cfg.button_label.size());
			}
			shared->run();
		}

		if(cfg.single_window && cfg.buttons && shared) {
			while(XPending(shared->_dpy)) {
				XEvent ev;
				XNextEvent(shared->_dpy, &ev);
				if(ev.type == ButtonPress) {
					int mx = ev.xbutton.x;
					int my = ev.xbutton.y;
					if(mx >= cfg.button_x && mx < cfg.button_x + (int)cfg.button_w &&
						my >= cfg.button_y && my < cfg.button_y + (int)cfg.button_h) {
						time_t now = time(NULL);
						for(size_t i = 0; i < cells.size(); i++) {
							if(cells[i].proc) {
								cells[i].done = true;
								cells[i].restart_at = now;
							}
						}
					}
				}
			}
		}
	}
}

#else // STOW_WINDOWS

#include <iostream>

int main() {
	std::cout << "test_grid is only available on POSIX/X11 platforms\n";
	return 0;
}

#endif // STOW_POSIX
