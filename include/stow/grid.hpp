// Grid layout for splitting screen into cells
#pragma once

#include <vector>
#include "layout.hpp"

namespace stow {

// Grid layout configuration
struct GridConfig {
    int rows = 1;
    int cols = 1;
    std::vector<int> row_heights;  // Percentages (should sum to 100)
    std::vector<int> col_widths;   // Percentages (should sum to 100)
    bool fit_to_cells = true;      // If false, stretch last row/col to fill screen

    // Create uniform grid
    static GridConfig uniform(int rows, int cols) {
        GridConfig cfg;
        cfg.rows = rows;
        cfg.cols = cols;

        int row_pct = 100 / rows;
        int col_pct = 100 / cols;

        cfg.row_heights.resize(rows, row_pct);
        cfg.col_widths.resize(cols, col_pct);

        // Adjust last row/col to account for rounding
        if (rows > 0) {
            cfg.row_heights[rows - 1] = 100 - (row_pct * (rows - 1));
        }
        if (cols > 0) {
            cfg.col_widths[cols - 1] = 100 - (col_pct * (cols - 1));
        }

        return cfg;
    }

    // Validate configuration
    bool valid() const {
        if (rows <= 0 || cols <= 0) return false;
        if (static_cast<int>(row_heights.size()) != rows) return false;
        if (static_cast<int>(col_widths.size()) != cols) return false;
        return true;
    }
};

// Grid layout calculator
class GridLayout {
public:
    GridConfig config;

    GridLayout() = default;
    explicit GridLayout(const GridConfig& cfg) : config(cfg) {}
    GridLayout(int rows, int cols) : config(GridConfig::uniform(rows, cols)) {}

    // Calculate cell rectangles within a screen area
    std::vector<Rect> calculate(unsigned int screen_w, unsigned int screen_h) const {
        std::vector<Rect> cells;
        if (!config.valid()) return cells;

        // Convert percentages to pixels
        std::vector<unsigned int> row_px(config.rows, 0);
        std::vector<unsigned int> col_px(config.cols, 0);

        unsigned int acc_h = 0;
        for (int i = 0; i < config.rows; i++) {
            row_px[i] = (screen_h * config.row_heights[i]) / 100;
            acc_h += row_px[i];
        }
        // Stretch last row if not fitting to cells
        if (!config.fit_to_cells && acc_h < screen_h && config.rows > 0) {
            row_px[config.rows - 1] += (screen_h - acc_h);
            acc_h = screen_h;
        }

        unsigned int acc_w = 0;
        for (int i = 0; i < config.cols; i++) {
            col_px[i] = (screen_w * config.col_widths[i]) / 100;
            acc_w += col_px[i];
        }
        // Stretch last column if not fitting to cells
        if (!config.fit_to_cells && acc_w < screen_w && config.cols > 0) {
            col_px[config.cols - 1] += (screen_w - acc_w);
            acc_w = screen_w;
        }

        // Build grid cells
        int y = 0;
        for (int r = 0; r < config.rows; r++) {
            int x = 0;
            for (int c = 0; c < config.cols; c++) {
                Rect cell;
                cell.x = x;
                cell.y = y;
                cell.width = col_px[c];
                cell.height = row_px[r];
                cells.push_back(cell);
                x += static_cast<int>(col_px[c]);
            }
            y += static_cast<int>(row_px[r]);
        }

        return cells;
    }

    // Calculate cell rectangles within a monitor
    std::vector<Rect> calculate(const Monitor& mon) const {
        std::vector<Rect> cells = calculate(mon.width, mon.height);
        // Offset cells by monitor position
        for (auto& cell : cells) {
            cell.x += mon.x;
            cell.y += mon.y;
        }
        return cells;
    }

    // Get the total grid size
    void total_size(unsigned int screen_w, unsigned int screen_h,
                    unsigned int& out_w, unsigned int& out_h) const {
        if (!config.valid()) {
            out_w = 0;
            out_h = 0;
            return;
        }

        out_h = 0;
        for (int i = 0; i < config.rows; i++) {
            out_h += (screen_h * config.row_heights[i]) / 100;
        }

        out_w = 0;
        for (int i = 0; i < config.cols; i++) {
            out_w += (screen_w * config.col_widths[i]) / 100;
        }
    }

    // Get cell at row,col (0-indexed)
    int cell_index(int row, int col) const {
        if (row < 0 || row >= config.rows) return -1;
        if (col < 0 || col >= config.cols) return -1;
        return row * config.cols + col;
    }

    // Get row,col from cell index
    void cell_position(int index, int& row, int& col) const {
        if (index < 0 || index >= config.rows * config.cols) {
            row = -1;
            col = -1;
            return;
        }
        row = index / config.cols;
        col = index % config.cols;
    }

    // Get number of cells
    int num_cells() const {
        return config.rows * config.cols;
    }

    // Get grid lines for drawing (returns line start/end points)
    struct GridLines {
        std::vector<int> horizontal_y;  // Y positions of horizontal lines
        std::vector<int> vertical_x;    // X positions of vertical lines
    };

    GridLines get_grid_lines(unsigned int screen_w, unsigned int screen_h) const {
        GridLines lines;
        if (!config.valid()) return lines;

        // Horizontal lines (between rows)
        int y = 0;
        for (int r = 0; r < config.rows - 1; r++) {
            y += (screen_h * config.row_heights[r]) / 100;
            lines.horizontal_y.push_back(y);
        }

        // Vertical lines (between columns)
        int x = 0;
        for (int c = 0; c < config.cols - 1; c++) {
            x += (screen_w * config.col_widths[c]) / 100;
            lines.vertical_x.push_back(x);
        }

        return lines;
    }
};

}  // namespace stow
