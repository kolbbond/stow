// shud - stow hud: grid-based overlay HUD that runs commands in cells
#include "stow/config.hpp"
#include "stow/grid.hpp"
#include "stow/hud_config.hpp"
#include "stow/monitor.hpp"
#include "ptyprocess.hpp"
#include "xwindow.hpp"

#include <X11/keysym.h>

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

static std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
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

// Parse a keybind string like "super+h", "ctrl+shift+escape", "F12"
static bool parse_keybind(const std::string& s, unsigned int& mod_mask, KeySym& keysym) {
	mod_mask = 0;
	keysym = NoSymbol;

	std::vector<std::string> parts;
	std::string cur;
	for(char c : s) {
		if(c == '+') {
			if(!cur.empty()) { parts.push_back(cur); cur.clear(); }
		} else {
			cur.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
	}
	if(!cur.empty()) parts.push_back(cur);
	if(parts.empty()) return false;

	for(size_t i = 0; i + 1 < parts.size(); i++) {
		const std::string& m = parts[i];
		if(m == "ctrl" || m == "control") mod_mask |= ControlMask;
		else if(m == "shift") mod_mask |= ShiftMask;
		else if(m == "alt" || m == "mod1") mod_mask |= Mod1Mask;
		else if(m == "super" || m == "mod4" || m == "win") mod_mask |= Mod4Mask;
	}

	const std::string& key = parts.back();
	keysym = XStringToKeysym(key.c_str());
	if(keysym == NoSymbol) {
		std::string cap = key;
		cap[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap[0])));
		keysym = XStringToKeysym(cap.c_str());
	}
	if(keysym == NoSymbol) {
		std::string upper = key;
		for(char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		keysym = XStringToKeysym(upper.c_str());
	}

	return keysym != NoSymbol;
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

static int x_error_handler(Display* dpy, XErrorEvent* ev) {
	char buf[256];
	XGetErrorText(dpy, ev->error_code, buf, sizeof(buf));
	std::cerr << "X error: " << buf
	          << " (opcode=" << static_cast<int>(ev->request_code)
	          << " resource=0x" << std::hex << ev->resourceid << std::dec << ")\n";
	return 0;
}

int main(int argc, char** argv) {
	if(argc < 2) {
		std::cerr << "usage: shud <config.ini>\n";
		return 1;
	}

	stow::HudConfig cfg = stow::HudConfig::load(argv[1]);
	if(!cfg.valid()) {
		std::cerr << "shud: " << (cfg.error.empty() ? "invalid config" : cfg.error) << "\n";
		return 1;
	}

	XSetErrorHandler(x_error_handler);

	// Query screen size and monitors with a temporary display connection
	unsigned int screen_w = 0;
	unsigned int screen_h = 0;
	int monitor_x = 0;
	int monitor_y = 0;
	{
		Display* tmp_dpy = XOpenDisplay(nullptr);
		if(!tmp_dpy) {
			std::cerr << "shud: cannot open display\n";
			return 1;
		}
		int scr = DefaultScreen(tmp_dpy);
		screen_w = DisplayWidth(tmp_dpy, scr);
		screen_h = DisplayHeight(tmp_dpy, scr);

		auto monitor_mgr = stow::MonitorManager::create(tmp_dpy);
		const stow::Monitor* mon = monitor_mgr->at(cfg.monitor);
		if(mon) {
			monitor_x = mon->x;
			monitor_y = mon->y;
			screen_w = mon->width;
			screen_h = mon->height;
			std::cout << "monitor " << mon->index << " (" << mon->name << "): "
			          << mon->width << "x" << mon->height << " at " << mon->x << "," << mon->y << "\n";
		}
		XCloseDisplay(tmp_dpy);
	}

	// Create grid layout
	stow::GridLayout grid(cfg.grid);

	unsigned int grid_w = 0;
	unsigned int grid_h = 0;
	grid.total_size(screen_w, screen_h, grid_w, grid_h);

	std::vector<stow::Rect> geoms = grid.calculate(screen_w, screen_h);
	if(geoms.empty()) {
		std::cerr << "shud: failed to build grid\n";
		return 1;
	}

	stow::GridLayout::GridLines lines;
	if(cfg.single_window && cfg.grid_lines) {
		lines = grid.get_grid_lines(screen_w, screen_h);
	}

	// map grid cell index -> CellSpec (by row,col)
	std::vector<const stow::CellSpec*> cell_at(geoms.size(), nullptr);
	for(const stow::CellSpec& c : cfg.cells) {
		int idx = grid.cell_index(c.row, c.col);
		if(idx >= 0 && idx < static_cast<int>(cell_at.size())) cell_at[idx] = &c;
	}

	// Create windows
	std::vector<CellState> cells;
	cells.reserve(geoms.size());
	std::vector<bool> hud_cells(geoms.size(), false);

	ShXWindowPr shared;
	if(cfg.single_window) {
		stow::WindowConfig shared_cfg;
		shared_cfg.title = "shud";
		shared_cfg.overlay = true;
		shared_cfg.use_fixed_geometry = true;
		shared_cfg.fixed_x = monitor_x;
		shared_cfg.fixed_y = monitor_y;
		shared_cfg.fixed_w = cfg.fit_to_cells ? grid_w : screen_w;
		shared_cfg.fixed_h = cfg.fit_to_cells ? grid_h : screen_h;

		shared = XWindow::create(shared_cfg);
		shared->setup();
		std::cout << "overlay: " << (shared->_overlay ? "yes" : "no") << "\n";
		std::cout << "window id: 0x" << std::hex << shared->_win << std::dec << "\n";
		std::cout << "override_redirect: " << (shared->_override_redirect ? "yes" : "no") << "\n";
		std::cout << "depth: " << shared->_depth << "\n";
	}

	for(size_t i = 0; i < geoms.size(); i++) {
		ShXWindowPr xwin;
		if(cfg.single_window) {
			xwin = shared;
		} else {
			stow::WindowConfig cell_cfg;
			cell_cfg.title = "shud";
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
		if(cell_at[i] && cell_at[i]->is_hud) {
			hud_cells[i] = true;
		}
	}

	// Grab toggle key on root window for interactive mode switch
	bool interactive = false;
	Display* dpy = shared ? shared->_dpy : cells[0].win->_dpy;
	Window root = shared ? shared->_root : cells[0].win->_root;

	unsigned int toggle_mod = 0;
	KeySym toggle_sym = NoSymbol;
	KeyCode toggle_keycode = 0;

	if(parse_keybind(cfg.toggle_key, toggle_mod, toggle_sym)) {
		toggle_keycode = XKeysymToKeycode(dpy, toggle_sym);
		if(toggle_keycode) {
			XGrabKey(dpy, toggle_keycode, toggle_mod, root,
				False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, toggle_keycode, toggle_mod | Mod2Mask, root,
				False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, toggle_keycode, toggle_mod | LockMask, root,
				False, GrabModeAsync, GrabModeAsync);
			XGrabKey(dpy, toggle_keycode, toggle_mod | Mod2Mask | LockMask, root,
				False, GrabModeAsync, GrabModeAsync);
			std::cout << "toggle interactive: " << cfg.toggle_key << "\n";
		} else {
			std::cerr << "shud: cannot resolve toggle key: " << cfg.toggle_key << "\n";
		}
	} else {
		std::cerr << "shud: invalid toggle_key: " << cfg.toggle_key << "\n";
	}

	// Flush any X errors from grab attempts before entering main loop
	XSync(dpy, False);

	HudState hud;

	while(true) {
		for(size_t i = 0; i < cells.size(); i++) {
			if(i < hud_cells.size() && hud_cells[i]) continue;
			if(!cells[i].cmd.empty()) continue;
			if(!cell_at[i] || cell_at[i]->cmd.empty()) continue;
			std::vector<std::string> parts = split_cmd(cell_at[i]->cmd);
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
		{
			Window qroot = 0, qchild = 0;
			int win_x = 0, win_y = 0;
			unsigned int mask = 0;
			if(XQueryPointer(dpy, root, &qroot, &qchild, &mouse_x, &mouse_y,
				   &win_x, &win_y, &mask) == False) {
				mouse_x = 0;
				mouse_y = 0;
			}
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
							geoms[i].width,
							geoms[i].height);
					} else {
						cells[i].proc->pump(cells[i].win);
					}
				}
				if(cells[i].proc->is_done()) {
					cells[i].done = true;
					cells[i].restart_at = now + cfg.period;
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
			if(interactive) hud_text << "[INTERACTIVE]\n";
			if(cfg.single_window) {
				shared->draw_region(hud_text.str(), geoms[i].x, geoms[i].y, geoms[i].width, geoms[i].height);
			} else {
				cells[i].win->draw(hud_text.str());
				cells[i].win->run();
			}
			drew_hud = true;
		}
		if(cfg.single_window && (drew_any || drew_hud) && shared) {
			if(cfg.grid_lines) {
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
			shared->run();
		}

		// Process X events - handle toggle key (grabbed on root so it works in overlay mode)
		while(XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if(ev.type == KeyPress && toggle_keycode &&
				ev.xkey.keycode == toggle_keycode) {
				interactive = !interactive;
				if(cfg.single_window && shared) {
					//shared->set_clickthrough(!interactive);
					shared->set_clickthrough(true);
				} else {
					for(auto& c : cells) {
						//if(c.win) c.win->set_clickthrough(!interactive);
						if(c.win) c.win->set_clickthrough(true);
					}
				}
				std::cout << (interactive ? "interactive mode" : "overlay mode") << "\n";
			}
		}
	}
}
