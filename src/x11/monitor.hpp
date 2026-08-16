// Multi-monitor detection and management
#pragma once

#include <X11/Xlib.h>

#include "stow/monitor.hpp"
#include <memory>
#include <string>
#include <vector>

// Try XRandR first, then Xinerama
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif

#ifdef HAVE_XINERAMA
#include <X11/extensions/Xinerama.h>
#endif

namespace stow {


class MonitorManager {
public:
    MonitorManager() = default;
    ~MonitorManager() = default;

    // Create a MonitorManager for the given display
    static std::shared_ptr<MonitorManager> create(Display* dpy) {
        auto mgr = std::make_shared<MonitorManager>();
        mgr->_dpy = dpy;
        mgr->refresh();
        return mgr;
    }

    // Refresh monitor information
    void refresh() {
        _monitors.clear();

        if (!_dpy) return;

        // Try XRandR first (modern, handles hotplug)
#ifdef HAVE_XRANDR
        if (try_xrandr()) return;
#endif

        // Fall back to Xinerama
#ifdef HAVE_XINERAMA
        if (try_xinerama()) return;
#endif

        // Final fallback: single monitor from root window
        fallback_single_monitor();
    }

    // Get all monitors
    const std::vector<Monitor>& monitors() const {
        return _monitors;
    }

    // Get primary monitor (or first monitor if no primary)
    const Monitor* primary() const {
        for (const auto& mon : _monitors) {
            if (mon.primary) return &mon;
        }
        return _monitors.empty() ? nullptr : &_monitors[0];
    }

    // Get monitor by index
    const Monitor* at(int index) const {
        if (index < 0) return primary();
        if (index >= static_cast<int>(_monitors.size())) return nullptr;
        return &_monitors[index];
    }

    // Get monitor containing a point
    const Monitor* at_point(int x, int y) const {
        for (const auto& mon : _monitors) {
            if (mon.contains(x, y)) return &mon;
        }
        return primary();
    }

    // Get total screen dimensions (bounding box of all monitors)
    void total_size(unsigned int& width, unsigned int& height) const {
        int max_x = 0, max_y = 0;
        for (const auto& mon : _monitors) {
            int right = mon.x + static_cast<int>(mon.width);
            int bottom = mon.y + static_cast<int>(mon.height);
            if (right > max_x) max_x = right;
            if (bottom > max_y) max_y = bottom;
        }
        width = static_cast<unsigned int>(max_x);
        height = static_cast<unsigned int>(max_y);
    }

    // Check if multi-monitor is available
    bool has_multiple_monitors() const {
        return _monitors.size() > 1;
    }

    // Get detection method used
    const char* detection_method() const {
        return _method;
    }

private:
    Display* _dpy = nullptr;
    std::vector<Monitor> _monitors;
    const char* _method = "none";

#ifdef HAVE_XRANDR
    bool try_xrandr() {
        int event_base, error_base;
        if (!XRRQueryExtension(_dpy, &event_base, &error_base)) {
            return false;
        }

        int screen = DefaultScreen(_dpy);
        Window root = RootWindow(_dpy, screen);

        XRRScreenResources* res = XRRGetScreenResources(_dpy, root);
        if (!res) return false;

        // Get primary output
        RROutput primary_output = XRRGetOutputPrimary(_dpy, root);

        int monitor_index = 0;
        for (int i = 0; i < res->noutput; i++) {
            XRROutputInfo* output = XRRGetOutputInfo(_dpy, res, res->outputs[i]);
            if (!output) continue;

            // Skip disconnected outputs
            if (output->connection != RR_Connected || output->crtc == None) {
                XRRFreeOutputInfo(output);
                continue;
            }

            XRRCrtcInfo* crtc = XRRGetCrtcInfo(_dpy, res, output->crtc);
            if (!crtc) {
                XRRFreeOutputInfo(output);
                continue;
            }

            Monitor mon;
            mon.index = monitor_index++;
            mon.name = output->name ? output->name : "";
            mon.x = crtc->x;
            mon.y = crtc->y;
            mon.width = crtc->width;
            mon.height = crtc->height;
            mon.primary = (res->outputs[i] == primary_output);

            _monitors.push_back(mon);

            XRRFreeCrtcInfo(crtc);
            XRRFreeOutputInfo(output);
        }

        XRRFreeScreenResources(res);

        if (!_monitors.empty()) {
            _method = "xrandr";
            return true;
        }
        return false;
    }
#endif

#ifdef HAVE_XINERAMA
    bool try_xinerama() {
        int event_base, error_base;
        if (!XineramaQueryExtension(_dpy, &event_base, &error_base)) {
            return false;
        }

        if (!XineramaIsActive(_dpy)) {
            return false;
        }

        int num_screens = 0;
        XineramaScreenInfo* screens = XineramaQueryScreens(_dpy, &num_screens);
        if (!screens || num_screens <= 0) {
            return false;
        }

        for (int i = 0; i < num_screens; i++) {
            Monitor mon;
            mon.index = i;
            mon.name = "screen" + std::to_string(screens[i].screen_number);
            mon.x = screens[i].x_org;
            mon.y = screens[i].y_org;
            mon.width = static_cast<unsigned int>(screens[i].width);
            mon.height = static_cast<unsigned int>(screens[i].height);
            mon.primary = (i == 0);  // First screen is typically primary

            _monitors.push_back(mon);
        }

        XFree(screens);

        if (!_monitors.empty()) {
            _method = "xinerama";
            return true;
        }
        return false;
    }
#endif

    void fallback_single_monitor() {
        int screen = DefaultScreen(_dpy);

        Monitor mon;
        mon.index = 0;
        mon.name = "default";
        mon.x = 0;
        mon.y = 0;
        mon.width = static_cast<unsigned int>(DisplayWidth(_dpy, screen));
        mon.height = static_cast<unsigned int>(DisplayHeight(_dpy, screen));
        mon.primary = true;

        _monitors.push_back(mon);
        _method = "fallback";
    }
};

}  // namespace stow
