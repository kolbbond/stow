// test layout system
#include "stow/config.hpp"
#include "stow/layout.hpp"
#include "stow/grid.hpp"
#include "stow/monitor.hpp"

#include <iostream>
#include <iomanip>

void print_rect(const char* name, const stow::Rect& r) {
	std::cout << name << ": x=" << r.x << " y=" << r.y
	          << " w=" << r.width << " h=" << r.height << "\n";
}

int main() {
	// Create a fake monitor for testing
	stow::Monitor mon;
	mon.x = 0;
	mon.y = 0;
	mon.width = 1920;
	mon.height = 1080;

	unsigned int content_w = 400;
	unsigned int content_h = 300;

	std::cout << "=== Testing AnchorLayout ===\n";
	std::cout << "Monitor: " << mon.width << "x" << mon.height << "\n";
	std::cout << "Content: " << content_w << "x" << content_h << "\n\n";

	// Test each anchor position
	struct {
		stow::Anchor anchor;
		const char* name;
	} anchors[] = {
		{stow::Anchor::TopLeft, "TopLeft"},
		{stow::Anchor::TopRight, "TopRight"},
		{stow::Anchor::BottomLeft, "BottomLeft"},
		{stow::Anchor::BottomRight, "BottomRight"},
		{stow::Anchor::Top, "Top"},
		{stow::Anchor::Bottom, "Bottom"},
		{stow::Anchor::Left, "Left"},
		{stow::Anchor::Right, "Right"},
		{stow::Anchor::Center, "Center"},
	};

	for (const auto& a : anchors) {
		stow::AnchorLayout layout(a.anchor, 10, 10);
		stow::Rect r = layout.calculate(mon, content_w, content_h);
		print_rect(a.name, r);
	}

	std::cout << "\n=== Testing Position parsing ===\n";

	struct {
		const char* input;
		int screen_size;
		int window_size;
	} positions[] = {
		{"10", 1920, 400},
		{"-10", 1920, 400},
		{"50%", 1920, 400},
		{"+10%", 1920, 400},
		{"-10%", 1920, 400},
	};

	for (const auto& p : positions) {
		stow::Position pos = stow::Position::parse(p.input);
		int result = pos.resolve(p.screen_size, p.window_size);
		std::cout << "\"" << p.input << "\" on " << p.screen_size
		          << " screen, " << p.window_size << " window = " << result << "\n";
	}

	std::cout << "\n=== Testing GridLayout ===\n";

	// Test uniform grid
	stow::GridLayout grid(2, 3);  // 2 rows, 3 columns
	std::cout << "Uniform 2x3 grid on " << mon.width << "x" << mon.height << ":\n";

	std::vector<stow::Rect> cells = grid.calculate(mon.width, mon.height);
	for (size_t i = 0; i < cells.size(); i++) {
		int row, col;
		grid.cell_position(i, row, col);
		std::cout << "  Cell[" << row << "," << col << "]: ";
		print_rect("", cells[i]);
	}

	// Test custom grid percentages
	stow::GridConfig custom_cfg;
	custom_cfg.rows = 2;
	custom_cfg.cols = 2;
	custom_cfg.row_heights = {30, 70};  // 30% top, 70% bottom
	custom_cfg.col_widths = {60, 40};   // 60% left, 40% right

	stow::GridLayout custom_grid(custom_cfg);
	std::cout << "\nCustom 2x2 grid (30/70 rows, 60/40 cols):\n";

	cells = custom_grid.calculate(mon.width, mon.height);
	for (size_t i = 0; i < cells.size(); i++) {
		int row, col;
		custom_grid.cell_position(i, row, col);
		std::cout << "  Cell[" << row << "," << col << "]: ";
		print_rect("", cells[i]);
	}

	// Test grid lines
	auto lines = custom_grid.get_grid_lines(mon.width, mon.height);
	std::cout << "\nGrid lines:\n";
	std::cout << "  Horizontal Y positions: ";
	for (int y : lines.horizontal_y) std::cout << y << " ";
	std::cout << "\n  Vertical X positions: ";
	for (int x : lines.vertical_x) std::cout << x << " ";
	std::cout << "\n";

	std::cout << "\nLayout test completed successfully.\n";
	return 0;
}
