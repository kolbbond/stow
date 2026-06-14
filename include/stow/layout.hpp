// Layout system for window positioning
#pragma once

#include "config.hpp"
#include "monitor.hpp"

namespace stow {

// Rectangle for layout calculations
struct Rect {
    int x = 0;
    int y = 0;
    unsigned int width = 0;
    unsigned int height = 0;

    // Check if rect is empty
    bool empty() const {
        return width == 0 || height == 0;
    }

    // Check if a point is within this rect
    bool contains(int px, int py) const {
        return px >= x && px < x + static_cast<int>(width) &&
               py >= y && py < y + static_cast<int>(height);
    }

    // Get right edge
    int right() const { return x + static_cast<int>(width); }

    // Get bottom edge
    int bottom() const { return y + static_cast<int>(height); }
};

// Anchor-based layout calculator
class AnchorLayout {
public:
    Anchor anchor = Anchor::TopLeft;
    int margin_x = 10;
    int margin_y = 10;

    AnchorLayout() = default;
    AnchorLayout(Anchor a, int mx = 10, int my = 10)
        : anchor(a), margin_x(mx), margin_y(my) {}

    // Calculate window position based on anchor within a monitor
    Rect calculate(const Monitor& mon, unsigned int content_w, unsigned int content_h) const {
        Rect r;
        r.width = content_w;
        r.height = content_h;

        switch (anchor) {
            case Anchor::TopLeft:
                r.x = mon.x + margin_x;
                r.y = mon.y + margin_y;
                break;
            case Anchor::TopRight:
                r.x = mon.x + static_cast<int>(mon.width) - static_cast<int>(content_w) - margin_x;
                r.y = mon.y + margin_y;
                break;
            case Anchor::BottomLeft:
                r.x = mon.x + margin_x;
                r.y = mon.y + static_cast<int>(mon.height) - static_cast<int>(content_h) - margin_y;
                break;
            case Anchor::BottomRight:
                r.x = mon.x + static_cast<int>(mon.width) - static_cast<int>(content_w) - margin_x;
                r.y = mon.y + static_cast<int>(mon.height) - static_cast<int>(content_h) - margin_y;
                break;
            case Anchor::Top:
                r.x = mon.x + (static_cast<int>(mon.width) - static_cast<int>(content_w)) / 2;
                r.y = mon.y + margin_y;
                break;
            case Anchor::Bottom:
                r.x = mon.x + (static_cast<int>(mon.width) - static_cast<int>(content_w)) / 2;
                r.y = mon.y + static_cast<int>(mon.height) - static_cast<int>(content_h) - margin_y;
                break;
            case Anchor::Left:
                r.x = mon.x + margin_x;
                r.y = mon.y + (static_cast<int>(mon.height) - static_cast<int>(content_h)) / 2;
                break;
            case Anchor::Right:
                r.x = mon.x + static_cast<int>(mon.width) - static_cast<int>(content_w) - margin_x;
                r.y = mon.y + (static_cast<int>(mon.height) - static_cast<int>(content_h)) / 2;
                break;
            case Anchor::Center:
                r.x = mon.x + (static_cast<int>(mon.width) - static_cast<int>(content_w)) / 2;
                r.y = mon.y + (static_cast<int>(mon.height) - static_cast<int>(content_h)) / 2;
                break;
            case Anchor::Custom:
            default:
                r.x = mon.x + margin_x;
                r.y = mon.y + margin_y;
                break;
        }

        return r;
    }

    // Calculate using raw screen dimensions (for when no monitor info available)
    Rect calculate(unsigned int screen_w, unsigned int screen_h,
                   unsigned int content_w, unsigned int content_h) const {
        Monitor fake_mon;
        fake_mon.x = 0;
        fake_mon.y = 0;
        fake_mon.width = screen_w;
        fake_mon.height = screen_h;
        return calculate(fake_mon, content_w, content_h);
    }
};

// Position-based layout using Position specifications
class PositionLayout {
public:
    Position px;
    Position py;
    Position tx;  // Additional offset
    Position ty;

    PositionLayout() = default;
    PositionLayout(const Position& x, const Position& y)
        : px(x), py(y) {}

    // Calculate window position within a monitor
    Rect calculate(const Monitor& mon, unsigned int content_w, unsigned int content_h) const {
        Rect r;
        r.width = content_w;
        r.height = content_h;

        // Resolve positions relative to monitor size
        r.x = mon.x + px.resolve(static_cast<int>(mon.width), static_cast<int>(content_w));
        r.y = mon.y + py.resolve(static_cast<int>(mon.height), static_cast<int>(content_h));

        // Apply additional offsets
        r.x += tx.resolve(static_cast<int>(content_w));
        r.y += ty.resolve(static_cast<int>(content_h));

        return r;
    }
};

}  // namespace stow
