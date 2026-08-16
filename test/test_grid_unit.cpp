// Assertions over stow::GridConfig / GridLayout cell arithmetic.
// Headless: no X11, no $DISPLAY.
#include "stow/grid.hpp"
#include "check.hpp"

int main() {
	// --- uniform() splits evenly and absorbs rounding into the last row/col ---
	stow::GridConfig g = stow::GridConfig::uniform(3, 3);
	CHECK_EQ(g.rows, 3);
	CHECK_EQ(g.cols, 3);
	CHECK(g.valid());
	int row_sum = 0, col_sum = 0;
	for(int h : g.row_heights) row_sum += h;
	for(int w : g.col_widths) col_sum += w;
	CHECK_EQ(row_sum, 100);
	CHECK_EQ(col_sum, 100);
	// 100/3 == 33, so the last one carries the remainder
	CHECK_EQ(g.row_heights[2], 34);

	// --- validation rejects malformed configs ---
	stow::GridConfig bad;
	bad.rows = 0;
	bad.cols = 1;
	CHECK(!bad.valid());

	stow::GridConfig mismatched = stow::GridConfig::uniform(2, 2);
	mismatched.row_heights.pop_back();
	CHECK(!mismatched.valid());

	// --- calculate() produces one rect per cell, in row-major order ---
	stow::GridLayout layout(stow::GridConfig::uniform(2, 2));
	std::vector<stow::Rect> cells = layout.calculate(1000, 800);
	CHECK_EQ(cells.size(), size_t(4));
	CHECK_EQ(cells[0].x, 0);
	CHECK_EQ(cells[0].y, 0);
	CHECK_EQ(cells[0].width, 500u);
	CHECK_EQ(cells[0].height, 400u);
	CHECK_EQ(cells[1].x, 500);
	CHECK_EQ(cells[1].y, 0);
	CHECK_EQ(cells[2].x, 0);
	CHECK_EQ(cells[2].y, 400);

	// --- cells tile the area without gaps or overlap ---
	unsigned int area = 0;
	for(const stow::Rect& r : cells) area += r.width * r.height;
	CHECK_EQ(area, 1000u * 800u);

	// --- a 1x1 grid is the whole area ---
	stow::GridLayout single(1, 1);
	std::vector<stow::Rect> one = single.calculate(640, 480);
	CHECK_EQ(one.size(), size_t(1));
	CHECK_EQ(one[0].width, 640u);
	CHECK_EQ(one[0].height, 480u);

	CHECK_REPORT();
}
