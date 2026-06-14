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

inline HudConfig HudConfig::load(const std::string& path) {
	HudConfig cfg;
	std::ifstream in(path);
	if(!in) {
		cfg.error = "cannot open config: " + path;
		return cfg;
	}
	return cfg;  // parsing added in later tasks
}

}  // namespace stow
