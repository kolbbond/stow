// headless unit tests for the sectioned-INI config parser
#include "stow/hud_config.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

// write `content` to a temp file, return its path
static std::string write_tmp(const std::string& name, const std::string& content) {
	std::string path = "/tmp/stow_test_" + name;
	std::ofstream out(path);
	out << content;
	out.close();
	return path;
}

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
	std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++g_failures; } } while(0)

static void test_missing_file() {
	stow::HudConfig cfg = stow::HudConfig::load("/tmp/stow_does_not_exist.ini");
	CHECK(!cfg.valid());
	CHECK(!cfg.error.empty());
}

static void test_grid_section() {
	std::string path = write_tmp("grid.ini",
		"# comment line\n"
		"[grid]\n"
		"rows = 2\n"
		"cols = 3\n"
		"row_heights = 40,60\n"
		"col_widths = 30,30,40\n"
		"monitor = 1\n"
		"single_window = 1\n"
		"grid_lines = 0\n"
		"fit_to_cells = 1\n"
		"period = 5\n"
		"toggle_key = ctrl+space\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.grid.rows == 2);
	CHECK(cfg.grid.cols == 3);
	CHECK(cfg.grid.row_heights.size() == 2 && cfg.grid.row_heights[1] == 60);
	CHECK(cfg.grid.col_widths.size() == 3 && cfg.grid.col_widths[2] == 40);
	CHECK(cfg.monitor == 1);
	CHECK(cfg.single_window == true);
	CHECK(cfg.grid_lines == false);
	CHECK(cfg.period == 5);
	CHECK(cfg.toggle_key == "ctrl+space");
}

int main() {
	test_missing_file();
	test_grid_section();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all hud_config tests passed\n";
	return 0;
}
