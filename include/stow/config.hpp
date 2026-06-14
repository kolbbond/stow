// Stow configuration system
#pragma once

#include <string>
#include <vector>

namespace stow {

// Position specification with optional prefix (+/-) and suffix (%)
struct Position {
    int value = 0;
    char prefix = 0;  // '+', '-', or 0 (no prefix means absolute)
    char suffix = 0;  // '%' or 0

    Position() = default;
    Position(int v) : value(v), prefix(0), suffix(0) {}
    Position(int v, char p) : value(v), prefix(p), suffix(0) {}
    Position(int v, char p, char s) : value(v), prefix(p), suffix(s) {}

    // Resolve position to absolute pixels given screen/window size
    int resolve(int screen_size, int window_size = 0) const {
        int base = value;
        if (suffix == '%') {
            base = (value * screen_size) / 100;
        }

        if (prefix == '-') {
            // Negative prefix means from right/bottom edge
            return screen_size - base - window_size;
        } else if (prefix == '+') {
            // Explicit positive - same as no prefix
            return base;
        }
        return base;
    }

    // Parse from string like "10", "-10", "50%", "+10%"
    static Position parse(const std::string& s) {
        Position p;
        if (s.empty()) return p;

        size_t start = 0;
        if (s[0] == '+' || s[0] == '-') {
            p.prefix = s[0];
            start = 1;
        }

        size_t end = s.size();
        if (s.back() == '%') {
            p.suffix = '%';
            end--;
        }

        if (start < end) {
            p.value = std::stoi(s.substr(start, end - start));
        }
        return p;
    }
};

// Anchor positions for window placement
enum class Anchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Top,
    Bottom,
    Left,
    Right,
    Center,
    Custom  // Use px/py directly
};

// Convert string to Anchor
inline Anchor anchor_from_string(const std::string& s) {
    if (s == "top-left" || s == "tl") return Anchor::TopLeft;
    if (s == "top-right" || s == "tr") return Anchor::TopRight;
    if (s == "bottom-left" || s == "bl") return Anchor::BottomLeft;
    if (s == "bottom-right" || s == "br") return Anchor::BottomRight;
    if (s == "top" || s == "t") return Anchor::Top;
    if (s == "bottom" || s == "b") return Anchor::Bottom;
    if (s == "left" || s == "l") return Anchor::Left;
    if (s == "right" || s == "r") return Anchor::Right;
    if (s == "center" || s == "c") return Anchor::Center;
    return Anchor::Custom;
}

// Convert Anchor to string
inline const char* anchor_to_string(Anchor a) {
    switch (a) {
        case Anchor::TopLeft: return "top-left";
        case Anchor::TopRight: return "top-right";
        case Anchor::BottomLeft: return "bottom-left";
        case Anchor::BottomRight: return "bottom-right";
        case Anchor::Top: return "top";
        case Anchor::Bottom: return "bottom";
        case Anchor::Left: return "left";
        case Anchor::Right: return "right";
        case Anchor::Center: return "center";
        case Anchor::Custom: return "custom";
    }
    return "custom";
}

// Window configuration
struct WindowConfig {
    // Position settings
    Position px;           // X position
    Position py;           // Y position
    Position tx;           // X offset (relative to content)
    Position ty;           // Y offset (relative to content)
    Anchor anchor = Anchor::Custom;
    int margin_x = 10;     // Margin from edge when using anchors
    int margin_y = 10;

    // Appearance
    int border_px = 2;
    double alpha = 0.8;
    char align = 'l';      // 'l', 'r', 'c' for left, right, center
    std::string font = "monospace:size=10";
    std::string fg = "#00a080";
    std::string bg = "#0000ff";

    // Identity
    std::string title = "stow";    // Window title and class name

    // Behavior
    bool overlay = true;           // Click-through overlay mode
    bool borderless = false;       // No window border
    bool on_top = true;            // Keep window on top

    // Multi-monitor
    int monitor = -1;              // -1 = primary, 0+ = specific monitor index

    // Fixed geometry mode (overrides position calculations)
    bool use_fixed_geometry = false;
    int fixed_x = 0;
    int fixed_y = 0;
    unsigned int fixed_w = 0;
    unsigned int fixed_h = 0;

    // Fullscreen mode
    bool fullscreen = false;

    // Resolve anchor to absolute position
    void resolve_anchor(int screen_w, int screen_h, int window_w, int window_h,
                        int& out_x, int& out_y) const {
        if (anchor == Anchor::Custom) {
            // Use px/py positions
            out_x = px.resolve(screen_w, window_w);
            out_y = py.resolve(screen_h, window_h);

            // Apply offsets
            out_x += tx.resolve(window_w);
            out_y += ty.resolve(window_h);
            return;
        }

        // Calculate anchor-based position
        switch (anchor) {
            case Anchor::TopLeft:
                out_x = margin_x;
                out_y = margin_y;
                break;
            case Anchor::TopRight:
                out_x = screen_w - window_w - margin_x;
                out_y = margin_y;
                break;
            case Anchor::BottomLeft:
                out_x = margin_x;
                out_y = screen_h - window_h - margin_y;
                break;
            case Anchor::BottomRight:
                out_x = screen_w - window_w - margin_x;
                out_y = screen_h - window_h - margin_y;
                break;
            case Anchor::Top:
                out_x = (screen_w - window_w) / 2;
                out_y = margin_y;
                break;
            case Anchor::Bottom:
                out_x = (screen_w - window_w) / 2;
                out_y = screen_h - window_h - margin_y;
                break;
            case Anchor::Left:
                out_x = margin_x;
                out_y = (screen_h - window_h) / 2;
                break;
            case Anchor::Right:
                out_x = screen_w - window_w - margin_x;
                out_y = (screen_h - window_h) / 2;
                break;
            case Anchor::Center:
                out_x = (screen_w - window_w) / 2;
                out_y = (screen_h - window_h) / 2;
                break;
            default:
                out_x = margin_x;
                out_y = margin_y;
                break;
        }
    }
};

// Process configuration
struct ProcessConfig {
    std::string command;
    std::vector<std::string> args;
    int period = 1;         // Time between runs in seconds
    bool use_pty = true;    // Use PTY for ANSI support
    char delimiter = '\4';  // Frame delimiter character
};

// Combined configuration
class Config {
public:
    WindowConfig window;
    ProcessConfig process;

    Config() = default;

    // Create with defaults
    static Config defaults() {
        Config cfg;
        cfg.window.px = Position(10);
        cfg.window.py = Position(10);
        cfg.window.tx = Position(0);
        cfg.window.ty = Position(0);
        cfg.window.border_px = 2;
        cfg.window.alpha = 0.8;
        cfg.window.align = 'l';
        cfg.window.font = "monospace:size=10";
        cfg.window.fg = "#00a080";
        cfg.window.bg = "#0000ff";
        cfg.window.overlay = true;
        cfg.window.on_top = true;
        cfg.process.period = 1;
        cfg.process.use_pty = true;
        return cfg;
    }
};

}  // namespace stow
