// Stow HUD dashboard config: sectioned-INI parser ([grid]/[cell]/[hud])
#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>

#include "grid.hpp"

namespace stow {

// One cell's parsed config. Sentinels (empty string / -1 / 0) mean "inherit".
struct CellSpec {
	int row = -1, col = -1;            // from "at = r,c"; -1 => assigned by fill order
	std::string widget;                // "command"|"hud"|"number"|"bar"; resolved in finalize
	std::string cmd;                   // data source / command to run
	std::string label;                 // number/bar label
	std::vector<std::string> fields;   // hud fields (e.g. time, fps, mouse)
	double max = 100.0;                // bar scale
	int period = -1;                   // -1 => inherit HudConfig::period
	std::string fg, bg, font;          // appearance overrides
	double alpha = -1.0;
	char align = 0;
	bool is_hud = false;
};

struct HudConfig {
	GridConfig grid;
	int monitor = -1;
	int period = 1;
	bool single_window = false;
	bool grid_lines = true;
	bool fit_to_cells = true;
	std::string toggle_key = "super+h";
	std::vector<CellSpec> cells;
	std::string error;                 // non-empty => parse/validation failure

	bool valid() const {
		return error.empty() && grid.valid() &&
		       static_cast<int>(cells.size()) <= grid.rows * grid.cols;
	}

	static HudConfig load(const std::string& path);
};

namespace detail {

inline std::string trim(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

inline std::string lower(std::string s) {
	for(char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

inline bool truthy(const std::string& v) {
	std::string l = lower(v);
	return l == "1" || l == "true" || l == "yes" || l == "on";
}

inline std::vector<int> csv_ints(const std::string& s) {
	std::vector<int> out;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) {
		item = trim(item);
		if(item.empty()) continue;
		try { out.push_back(std::stoi(item)); } catch(...) { /* caller validates */ }
	}
	return out;
}

inline std::vector<std::string> csv_strs(const std::string& s) {
	std::vector<std::string> out;
	std::stringstream ss(s);
	std::string item;
	while(std::getline(ss, item, ',')) {
		item = trim(item);
		if(!item.empty()) out.push_back(item);
	}
	return out;
}

inline int to_int(const std::string& v, int fallback) {
	try { return std::stoi(v); } catch(...) { return fallback; }
}

}  // namespace detail

inline HudConfig HudConfig::load(const std::string& path) {
	HudConfig cfg;
	std::ifstream in(path);
	if(!in) {
		cfg.error = "cannot open config: " + path;
		return cfg;
	}

	auto add_err = [&cfg](const std::string& m) {
		if(!cfg.error.empty()) cfg.error += "; ";
		cfg.error += m;
	};

	std::string line;
	std::string section;  // "", "grid", "cell", "hud"

	while(std::getline(in, line)) {
		line = detail::trim(line);
		if(line.empty() || line[0] == '#' || line[0] == ';') continue;

		if(line.front() == '[' && line.back() == ']') {
			section = detail::lower(detail::trim(line.substr(1, line.size() - 2)));
			if(section == "cell" || section == "hud") {
				cfg.cells.emplace_back();
				CellSpec& c = cfg.cells.back();
				c.is_hud = (section == "hud");
				c.widget = c.is_hud ? "hud" : "";  // command default resolved in finalize
			}
			continue;
		}

		size_t eq = line.find('=');
		if(eq == std::string::npos) { add_err("malformed line: " + line); continue; }
		std::string key = detail::trim(line.substr(0, eq));
		std::string val = detail::trim(line.substr(eq + 1));

		if(section == "grid") {
			if(key == "rows") cfg.grid.rows = detail::to_int(val, 0);
			else if(key == "cols") cfg.grid.cols = detail::to_int(val, 0);
			else if(key == "row_heights") cfg.grid.row_heights = detail::csv_ints(val);
			else if(key == "col_widths") cfg.grid.col_widths = detail::csv_ints(val);
			else if(key == "monitor") cfg.monitor = detail::to_int(val, -1);
			else if(key == "period") cfg.period = detail::to_int(val, 1);
			else if(key == "single_window") cfg.single_window = detail::truthy(val);
			else if(key == "grid_lines") cfg.grid_lines = detail::truthy(val);
			else if(key == "fit_to_cells") cfg.fit_to_cells = detail::truthy(val);
			else if(key == "toggle_key") cfg.toggle_key = val;
			else add_err("unknown grid key: " + key);
		}
		else if(section == "cell" || section == "hud") {
			if(cfg.cells.empty()) { add_err("key outside cell: " + key); continue; }
			CellSpec& c = cfg.cells.back();
			if(key == "at") {
				std::vector<int> rc = detail::csv_ints(val);
				if(rc.size() == 2 && rc[0] >= 0 && rc[1] >= 0) { c.row = rc[0]; c.col = rc[1]; }
				else add_err("bad 'at' (want non-negative r,c): " + val);
			}
			else if(key == "widget") c.widget = detail::lower(val);
			else if(key == "cmd") c.cmd = val;
			else if(key == "label") c.label = val;
			else if(key == "fields") c.fields = detail::csv_strs(val);
			else if(key == "max") { try { c.max = std::stod(val); } catch(...) { add_err("bad max: " + val); } }
			else if(key == "period") c.period = detail::to_int(val, -1);
			else if(key == "fg") c.fg = val;
			else if(key == "bg") c.bg = val;
			else if(key == "font") c.font = val;
			else if(key == "alpha") { try { c.alpha = std::stod(val); } catch(...) { add_err("bad alpha: " + val); } }
			else if(key == "align") c.align = val.empty() ? 0 : val[0];
			else add_err("unknown cell key: " + key);
		}
		else {
			add_err(section.empty() ? ("key outside any section: " + key)
			                        : ("key in unknown section [" + section + "]: " + key));
		}
	}

	// --- finalize ---
	// default to uniform percentages when not given
	if(cfg.grid.rows > 0 && cfg.grid.cols > 0) {
		GridConfig uni = GridConfig::uniform(cfg.grid.rows, cfg.grid.cols);
		if(cfg.grid.row_heights.empty()) cfg.grid.row_heights = uni.row_heights;
		if(cfg.grid.col_widths.empty()) cfg.grid.col_widths = uni.col_widths;
	}
	cfg.grid.fit_to_cells = cfg.fit_to_cells;

	// resolve command default + fill-order placement
	int next = 0;
	for(CellSpec& c : cfg.cells) {
		if(!c.is_hud && c.widget.empty()) c.widget = "command";
		if(c.row < 0 || c.col < 0) {
			if(cfg.grid.cols > 0) { c.row = next / cfg.grid.cols; c.col = next % cfg.grid.cols; }
		}
		next++;
		if(cfg.grid.rows > 0 && cfg.grid.cols > 0 &&
			(c.row >= cfg.grid.rows || c.col >= cfg.grid.cols)) {
			add_err("cell out of grid range: " + std::to_string(c.row) + "," + std::to_string(c.col));
		}
	}

	// detect overlapping placements (duplicate explicit 'at', or fill-order clashing with one)
	if(cfg.grid.rows > 0 && cfg.grid.cols > 0) {
		std::vector<bool> occupied(cfg.grid.rows * cfg.grid.cols, false);
		for(const CellSpec& c : cfg.cells) {
			if(c.row < 0 || c.col < 0 || c.row >= cfg.grid.rows || c.col >= cfg.grid.cols) continue;
			int idx = c.row * cfg.grid.cols + c.col;
			if(occupied[idx]) add_err("duplicate cell at: " + std::to_string(c.row) + "," + std::to_string(c.col));
			else occupied[idx] = true;
		}
	}

	if(cfg.grid.rows <= 0 || cfg.grid.cols <= 0) add_err("grid rows/cols must be > 0");

	return cfg;
}

}  // namespace stow
