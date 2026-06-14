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

static void test_cell_and_hud() {
	std::string path = write_tmp("cells.ini",
		"[grid]\n"
		"rows = 2\n"
		"cols = 2\n"
		"row_heights = 50,50\n"
		"col_widths = 50,50\n"
		"[cell]\n"
		"at = 0,0\n"
		"cmd = btop\n"
		"fg = #00ff00\n"
		"period = 2\n"
		"[cell]\n"
		"at = 0,1\n"
		"widget = number\n"
		"label = cores\n"
		"cmd = nproc\n"
		"[hud]\n"
		"at = 1,0\n"
		"fields = time, fps, mouse\n"
		"fg = #ffaa00\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.cells.size() == 3);
	// cell 0: command (default widget)
	CHECK(cfg.cells[0].row == 0 && cfg.cells[0].col == 0);
	CHECK(cfg.cells[0].cmd == "btop");
	CHECK(cfg.cells[0].widget == "command");
	CHECK(cfg.cells[0].fg == "#00ff00");
	CHECK(cfg.cells[0].period == 2);
	// cell 1: explicit number widget
	CHECK(cfg.cells[1].widget == "number");
	CHECK(cfg.cells[1].label == "cores");
	CHECK(cfg.cells[1].cmd == "nproc");
	// cell 2: hud
	CHECK(cfg.cells[2].is_hud == true);
	CHECK(cfg.cells[2].widget == "hud");
	CHECK(cfg.cells[2].fields.size() == 3 && cfg.cells[2].fields[1] == "fps");
}

static void test_finalize() {
	// no row_heights/col_widths => uniform; cells without 'at' => fill order
	std::string path = write_tmp("finalize.ini",
		"[grid]\n"
		"rows = 1\n"
		"cols = 2\n"
		"[cell]\n"
		"cmd = date\n"
		"[cell]\n"
		"cmd = uptime\n");
	stow::HudConfig cfg = stow::HudConfig::load(path);
	CHECK(cfg.error.empty());
	CHECK(cfg.valid());
	CHECK(cfg.grid.row_heights.size() == 1 && cfg.grid.row_heights[0] == 100);
	CHECK(cfg.grid.col_widths.size() == 2);
	CHECK(cfg.cells[0].row == 0 && cfg.cells[0].col == 0);  // fill order
	CHECK(cfg.cells[1].row == 0 && cfg.cells[1].col == 1);
}

static void test_validation_errors() {
	// rows/cols zero => invalid
	std::string p1 = write_tmp("bad_grid.ini", "[grid]\nrows = 0\ncols = 2\n");
	stow::HudConfig c1 = stow::HudConfig::load(p1);
	CHECK(!c1.valid());

	// more cells than grid slots => invalid
	std::string p2 = write_tmp("toomany.ini",
		"[grid]\nrows = 1\ncols = 1\n[cell]\ncmd=a\n[cell]\ncmd=b\n");
	stow::HudConfig c2 = stow::HudConfig::load(p2);
	CHECK(!c2.valid());

	// explicit 'at' out of range => error recorded
	std::string p3 = write_tmp("oob.ini",
		"[grid]\nrows = 1\ncols = 1\n[cell]\nat = 5,5\ncmd=a\n");
	stow::HudConfig c3 = stow::HudConfig::load(p3);
	CHECK(!c3.valid());
}

static void test_robustness() {
	// negative 'at' is a typo, not a fill-order request => error
	std::string p1 = write_tmp("neg_at.ini",
		"[grid]\nrows = 1\ncols = 2\n[cell]\nat = -1,0\ncmd=a\n");
	stow::HudConfig c1 = stow::HudConfig::load(p1);
	CHECK(!c1.valid());

	// key before any section => error (catches stale flat-format configs)
	std::string p2 = write_tmp("no_section.ini", "rows = 2\n[grid]\nrows = 1\ncols = 1\n");
	stow::HudConfig c2 = stow::HudConfig::load(p2);
	CHECK(!c2.valid());

	// two cells placed at the same coordinate => error (not a silent overwrite)
	std::string p3 = write_tmp("dup_at.ini",
		"[grid]\nrows = 2\ncols = 2\n[cell]\nat = 0,0\ncmd=a\n[cell]\nat = 0,0\ncmd=b\n");
	stow::HudConfig c3 = stow::HudConfig::load(p3);
	CHECK(!c3.valid());
}

#ifdef HUD_TEST_INI
static void test_real_fixture() {
	stow::HudConfig cfg = stow::HudConfig::load(HUD_TEST_INI);
	CHECK(cfg.error.empty());
	CHECK(cfg.valid());
	CHECK(cfg.grid.rows >= 1 && cfg.grid.cols >= 1);
	bool has_hud = false;
	for(const auto& c : cfg.cells) if(c.is_hud) has_hud = true;
	CHECK(has_hud);
}
#endif

int main() {
	test_missing_file();
	test_grid_section();
	test_cell_and_hud();
	test_finalize();
	test_validation_errors();
	test_robustness();
#ifdef HUD_TEST_INI
	test_real_fixture();
#endif
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all hud_config tests passed\n";
	return 0;
}
