// grid runner: split screen into cells and run commands serially
#include "stow/config.hpp"
#include "stow/grid.hpp"
#include "stow/monitor.hpp"
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
#include <cstdlib>
#include <iomanip>
#include <unistd.h>
#include <poll.h>

struct GridFileConfig {
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
	int period = 1;
	int monitor = -1;  // -1 = primary, 0+ = specific monitor index
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

static GridFileConfig load_config(const std::string& path) {
	GridFileConfig cfg;
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
		} else if(key == "period") {
			cfg.period = std::stoi(value);
		} else if(key == "monitor") {
			cfg.monitor = std::stoi(value);
		}
	}
	return cfg;
}

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

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: test_grid <config file> [timeout_sec]\n";
		return 1;
	}

	// optional timeout: 0 = run until killed (interactive default)
	double timeout_sec = argc > 2 ? std::atof(argv[2]) : 0.0;

	GridFileConfig file_cfg = load_config(argv[1]);
	if(file_cfg.rows <= 0 || file_cfg.cols <= 0) {
		std::cerr << "invalid grid config\n";
		return 1;
	}
	if(static_cast<int>(file_cfg.row_heights.size()) != file_cfg.rows ||
		static_cast<int>(file_cfg.col_widths.size()) != file_cfg.cols) {
		std::cerr << "row_heights/col_widths must match rows/cols\n";
		return 1;
	}
	if(static_cast<int>(file_cfg.cells.size()) != file_cfg.rows * file_cfg.cols) {
		std::cerr << "cell count must match rows*cols\n";
		return 1;
	}

	// Create grid layout configuration
	stow::GridConfig grid_cfg;
	grid_cfg.rows = file_cfg.rows;
	grid_cfg.cols = file_cfg.cols;
	grid_cfg.row_heights = file_cfg.row_heights;
	grid_cfg.col_widths = file_cfg.col_widths;
	grid_cfg.fit_to_cells = file_cfg.fit_to_cells;

	stow::GridLayout grid(grid_cfg);

	// base window for screen size
	stow::WindowConfig base_cfg;
	base_cfg.overlay = true;

	ShXWindowPr base = XWindow::create(base_cfg);
	base->setup();

	// Get monitor geometry
	int monitor_x = 0;
	int monitor_y = 0;
	unsigned int screen_w = base->_screen_width;
	unsigned int screen_h = base->_screen_height;

	auto monitor_mgr = stow::MonitorManager::create(base->_dpy);
	const stow::Monitor* mon = monitor_mgr->at(file_cfg.monitor);
	if (mon) {
		monitor_x = mon->x;
		monitor_y = mon->y;
		screen_w = mon->width;
		screen_h = mon->height;
		std::cout << "Using monitor " << mon->index << " (" << mon->name << "): "
		          << mon->width << "x" << mon->height << " at " << mon->x << "," << mon->y << "\n";
	}

	unsigned int grid_w = 0;
	unsigned int grid_h = 0;
	grid.total_size(screen_w, screen_h, grid_w, grid_h);

	std::vector<stow::Rect> geoms = grid.calculate(screen_w, screen_h);
	if(geoms.empty()) {
		std::cerr << "failed to build grid\n";
		return 1;
	}

	stow::GridLayout::GridLines lines;
	if(file_cfg.single_window && file_cfg.grid_lines) {
		lines = grid.get_grid_lines(screen_w, screen_h);
	}

	std::vector<CellState> cells;
	cells.reserve(geoms.size());
	std::vector<bool> hud_cells(geoms.size(), false);

	ShXWindowPr shared;
	if (file_cfg.single_window) {
		stow::WindowConfig shared_cfg;
		shared_cfg.overlay = !file_cfg.buttons;
		shared_cfg.use_fixed_geometry = true;
		shared_cfg.fixed_x = monitor_x;
		shared_cfg.fixed_y = monitor_y;
		shared_cfg.fixed_w = file_cfg.fit_to_cells ? grid_w : screen_w;
		shared_cfg.fixed_h = file_cfg.fit_to_cells ? grid_h : screen_h;

		shared = XWindow::create(shared_cfg);
		shared->setup();
	}

	for (size_t i = 0; i < geoms.size(); i++) {
		ShXWindowPr xwin;
		if (file_cfg.single_window) {
			xwin = shared;
		} else {
			stow::WindowConfig cell_cfg;
			cell_cfg.overlay = true;
			cell_cfg.use_fixed_geometry = true;
			cell_cfg.fixed_x = monitor_x + geoms[i].x;
			cell_cfg.fixed_y = monitor_y + geoms[i].y;
			cell_cfg.fixed_w = geoms[i].width;
			cell_cfg.fixed_h = geoms[i].height;

			xwin = XWindow::create(cell_cfg);
			xwin->setup();
		}
		CellState cell;
		cell.win = xwin;
		cells.push_back(cell);
		if(i < file_cfg.cells.size() && is_hud_cell(file_cfg.cells[i])) {
			hud_cells[i] = true;
		}
	}

	HudState hud;

	auto run_start = std::chrono::steady_clock::now();
	while(true) {
		if(timeout_sec > 0.0 &&
			std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count() >= timeout_sec) {
			break;
		}
		for(size_t i = 0; i < file_cfg.cells.size(); i++) {
			if(i < hud_cells.size() && hud_cells[i]) continue;
			if(!cells[i].cmd.empty()) continue;
			std::vector<std::string> parts = split_cmd(file_cfg.cells[i]);
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
					if(file_cfg.single_window) {
						drew_any |= cells[i].proc->pump_region(
							cells[i].win,
							geoms[i].x,
							geoms[i].y,
							geoms[i].width,
							geoms[i].height);
					} else {
						cells[i].proc->pump(cells[i].win);
					}
				}
				if(cells[i].proc->is_done()) {
					cells[i].done = true;
					cells[i].restart_at = now + file_cfg.period;
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
			if(file_cfg.single_window) {
				shared->draw_region(hud_text.str(), geoms[i].x, geoms[i].y, geoms[i].width, geoms[i].height);
			} else {
				cells[i].win->draw(hud_text.str());
				cells[i].win->run();
			}
			drew_hud = true;
		}
		if(file_cfg.single_window && (drew_any || drew_hud) && shared) {
			if(file_cfg.grid_lines) {
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xforeground.pixel);
				for(size_t i = 0; i < lines.horizontal_y.size(); i++) {
					XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, 0, lines.horizontal_y[i],
						grid_w, lines.horizontal_y[i]);
				}
				for(size_t i = 0; i < lines.vertical_x.size(); i++) {
					XDrawLine(shared->_dpy, shared->_drawable, shared->_xgc, lines.vertical_x[i], 0,
						lines.vertical_x[i], grid_h);
				}
			}
			if(file_cfg.buttons) {
				int bx = file_cfg.button_x;
				int by = file_cfg.button_y;
				unsigned int bw = file_cfg.button_w;
				unsigned int bh = file_cfg.button_h;
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xbackground.pixel);
				XFillRectangle(shared->_dpy, shared->_drawable, shared->_xgc, bx, by, bw, bh);
				XSetForeground(shared->_dpy, shared->_xgc, shared->_xforeground.pixel);
				XDrawRectangle(shared->_dpy, shared->_drawable, shared->_xgc, bx, by, bw, bh);

				XGlyphInfo ex;
				XftTextExtentsUtf8(shared->_dpy, shared->_xfont,
					(unsigned char*)file_cfg.button_label.c_str(), file_cfg.button_label.size(), &ex);
				int tx = bx + (bw - ex.xOff) / 2;
				int ty = by + (bh - (shared->_xfont->ascent + shared->_xfont->descent)) / 2;
				XftDrawStringUtf8(shared->_xdraw, &shared->_xforeground, shared->_xfont,
					tx, ty + shared->_xfont->ascent,
					(unsigned char*)file_cfg.button_label.c_str(), file_cfg.button_label.size());
			}
			shared->run();
		}

		if(file_cfg.single_window && file_cfg.buttons && shared) {
			while(XPending(shared->_dpy)) {
				XEvent ev;
				XNextEvent(shared->_dpy, &ev);
				if(ev.type == ButtonPress) {
					int mx = ev.xbutton.x;
					int my = ev.xbutton.y;
					if(mx >= file_cfg.button_x && mx < file_cfg.button_x + (int)file_cfg.button_w &&
						my >= file_cfg.button_y && my < file_cfg.button_y + (int)file_cfg.button_h) {
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
