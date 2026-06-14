// Win32 Window implementation for Windows
#pragma once

#include "platform.hpp"

#if STOW_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wingdi.h>

#include <memory>
#include <iostream>
#include <cstring>
#include <sstream>
#include <vector>
#include <unordered_map>

#include "config.h"
#include "color_span.hpp"
#include "window.hpp"

typedef std::shared_ptr<class Win32Window> ShWin32WindowPr;

class Win32Window : public StowWindow {
public:
	HWND _hwnd = nullptr;
	HDC _hdc = nullptr;
	HDC _memdc = nullptr;
	HBITMAP _bitmap = nullptr;
	HBITMAP _oldbitmap = nullptr;
	HFONT _font = nullptr;
	HFONT _oldfont = nullptr;
	COLORREF _foreground_color;
	COLORREF _background_color;
	std::unordered_map<unsigned int, COLORREF> _color_cache;
	unsigned int _bitmap_width = 0;
	unsigned int _bitmap_height = 0;

	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch(msg) {
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		case WM_ERASEBKGND:
			return 1;  // We handle background ourselves
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc = BeginPaint(hwnd, &ps);
			Win32Window* self = reinterpret_cast<Win32Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
			if(self && self->_memdc && self->_bitmap) {
				BitBlt(hdc, 0, 0, self->_window_width, self->_window_height, self->_memdc, 0, 0, SRCCOPY);
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}

	static ShWin32WindowPr create() {
		return std::make_shared<Win32Window>();
	}

	~Win32Window() {
		cleanup();
	}

	void cleanup() {
		if(_oldfont && _memdc) {
			SelectObject(_memdc, _oldfont);
			_oldfont = nullptr;
		}
		if(_font) {
			DeleteObject(_font);
			_font = nullptr;
		}
		if(_oldbitmap && _memdc) {
			SelectObject(_memdc, _oldbitmap);
			_oldbitmap = nullptr;
		}
		if(_bitmap) {
			DeleteObject(_bitmap);
			_bitmap = nullptr;
		}
		if(_memdc) {
			DeleteDC(_memdc);
			_memdc = nullptr;
		}
		if(_hdc) {
			ReleaseDC(_hwnd, _hdc);
			_hdc = nullptr;
		}
		if(_hwnd) {
			DestroyWindow(_hwnd);
			_hwnd = nullptr;
		}
	}

	void setup() override {
		std::printf("--- setup Win32Window ---\n");

		// Register window class
		static bool class_registered = false;
		static const char* CLASS_NAME = "StowWindowClass";

		if(!class_registered) {
			WNDCLASSEXA wc = {};
			wc.cbSize = sizeof(WNDCLASSEXA);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WndProc;
			wc.hInstance = GetModuleHandle(nullptr);
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = nullptr;  // No default background - we handle it
			wc.lpszClassName = CLASS_NAME;

			if(!RegisterClassExA(&wc)) {
				std::cerr << "Failed to register window class\n";
				return;
			}
			class_registered = true;
		}

		// Get screen dimensions
		_screen_width = GetSystemMetrics(SM_CXSCREEN);
		_screen_height = GetSystemMetrics(SM_CYSCREEN);

		// Window style flags for layered transparent window
		DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST;
		if(_overlay) {
			exStyle |= WS_EX_TRANSPARENT; // click-through
		}

		DWORD style = WS_POPUP;
		if(!_override_redirect && !_borderless && !gconf.borderless) {
			style = WS_OVERLAPPEDWINDOW;
		}

		// Create window (start with 1x1, will resize on draw)
		_hwnd = CreateWindowExA(
			exStyle,
			CLASS_NAME,
			"Stow",
			style,
			0, 0, 1, 1,
			nullptr,
			nullptr,
			GetModuleHandle(nullptr),
			nullptr
		);

		if(!_hwnd) {
			std::cerr << "Failed to create window\n";
			return;
		}

		// Store pointer to this object
		SetWindowLongPtr(_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

		// Get device context
		_hdc = GetDC(_hwnd);
		if(!_hdc) {
			std::cerr << "Failed to get device context\n";
			return;
		}

		// Create memory DC for double buffering
		_memdc = CreateCompatibleDC(_hdc);

		// Parse font from config (format: "fontname:size=N")
		std::string fontname = "Consolas";
		int fontsize = 10;
		size_t colonpos = gconf.font.find(':');
		if(colonpos != std::string::npos) {
			fontname = gconf.font.substr(0, colonpos);
			size_t sizepos = gconf.font.find("size=");
			if(sizepos != std::string::npos) {
				fontsize = std::stoi(gconf.font.substr(sizepos + 5));
			}
		}

		// Create font
		_font = CreateFontA(
			-MulDiv(fontsize, GetDeviceCaps(_hdc, LOGPIXELSY), 72),
			0, 0, 0,
			FW_NORMAL,
			FALSE, FALSE, FALSE,
			DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS,
			CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY,
			FIXED_PITCH | FF_MODERN,
			fontname.c_str()
		);

		// Parse colors
		_foreground_color = parse_color(gconf.colors[0]);
		// Use magenta as transparent color key (won't appear in normal content)
		_background_color = RGB(255, 0, 255);

		// Setup transparency using color key (magenta becomes transparent)
		if(_transparent_background) {
			SetLayeredWindowAttributes(_hwnd, _background_color, 255, LWA_COLORKEY | LWA_ALPHA);
		}
	}

	static COLORREF parse_color(const std::string& s) {
		if(s.size() == 7 && s[0] == '#') {
			unsigned int r = std::strtoul(s.substr(1, 2).c_str(), nullptr, 16);
			unsigned int g = std::strtoul(s.substr(3, 2).c_str(), nullptr, 16);
			unsigned int b = std::strtoul(s.substr(5, 2).c_str(), nullptr, 16);
			return RGB(r, g, b);
		}
		return RGB(255, 255, 255);
	}

	static COLORREF rgb_to_colorref(unsigned int rgb) {
		return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
	}

	void ensure_bitmap(unsigned int w, unsigned int h) {
		if(_bitmap && _bitmap_width == w && _bitmap_height == h) {
			return;
		}

		if(_oldbitmap && _memdc) {
			SelectObject(_memdc, _oldbitmap);
			_oldbitmap = nullptr;
		}
		if(_bitmap) {
			DeleteObject(_bitmap);
			_bitmap = nullptr;
		}

		_bitmap = CreateCompatibleBitmap(_hdc, w, h);
		_oldbitmap = (HBITMAP)SelectObject(_memdc, _bitmap);
		_bitmap_width = w;
		_bitmap_height = h;

	}

	void draw(const std::string& text) override {
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		// Select font and calculate text dimensions
		HFONT prevfont = (HFONT)SelectObject(_memdc, _font);

		TEXTMETRICA tm;
		GetTextMetricsA(_memdc, &tm);
		int line_height = tm.tmHeight;

		// Calculate required window size
		_window_width = 0;
		_window_height = 0;

		std::istringstream stream(text);
		std::string line;
		while(std::getline(stream, line)) {
			SIZE sz;
			GetTextExtentPoint32A(_memdc, line.c_str(), (int)line.size(), &sz);
			if((unsigned int)sz.cx > _window_width) _window_width = sz.cx;
			_window_height += line_height;
		}

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = _window_width == 0 || _window_height == 0;
			if(_hidden) {
				SelectObject(_memdc, prevfont);
				return;
			}
			_window_width += borderpx * 2;
			_window_height += borderpx * 2;
		}

		ensure_bitmap(_window_width, _window_height);

		// Clear background
		RECT rect = {0, 0, (LONG)_window_width, (LONG)_window_height};
		HBRUSH bg_brush = CreateSolidBrush(_background_color);
		FillRect(_memdc, &rect, bg_brush);
		DeleteObject(bg_brush);

		// Draw text
		SetBkMode(_memdc, TRANSPARENT);
		SetTextColor(_memdc, _foreground_color);

		stream.clear();
		stream.seekg(0, std::ios::beg);
		int y = borderpx;
		while(std::getline(stream, line)) {
			SIZE sz;
			GetTextExtentPoint32A(_memdc, line.c_str(), (int)line.size(), &sz);

			int x = borderpx;
			if(gconf.align == 'r') {
				x = _window_width - sz.cx;
			} else if(gconf.align == 'c') {
				x = (_window_width - sz.cx) / 2;
			}

			TextOutA(_memdc, x, y, line.c_str(), (int)line.size());
			y += line_height;
		}

		SelectObject(_memdc, prevfont);
		_dirty = true;
	}

	void draw_region(const std::string& text, int rx, int ry, unsigned int rw, unsigned int rh) override {
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = false;
			_window_width = rw;
			_window_height = rh;
		}

		ensure_bitmap(_window_width, _window_height);

		HFONT prevfont = (HFONT)SelectObject(_memdc, _font);
		TEXTMETRICA tm;
		GetTextMetricsA(_memdc, &tm);
		int line_height = tm.tmHeight;

		// Clear region
		RECT rect = {rx, ry, rx + (LONG)rw, ry + (LONG)rh};
		HBRUSH bg_brush = CreateSolidBrush(_background_color);
		FillRect(_memdc, &rect, bg_brush);
		DeleteObject(bg_brush);

		// Set clipping region
		HRGN hrgn = CreateRectRgn(rx, ry, rx + rw, ry + rh);
		SelectClipRgn(_memdc, hrgn);

		SetBkMode(_memdc, TRANSPARENT);
		SetTextColor(_memdc, _foreground_color);

		std::istringstream stream(text);
		std::string line;
		int y = ry + borderpx;
		while(std::getline(stream, line)) {
			SIZE sz;
			GetTextExtentPoint32A(_memdc, line.c_str(), (int)line.size(), &sz);

			int x = rx + borderpx;
			if(gconf.align == 'r') {
				if(sz.cx < (int)rw) x = rx + (rw - sz.cx);
			} else if(gconf.align == 'c') {
				if(sz.cx < (int)rw) x = rx + (rw - sz.cx) / 2;
			}

			if(y + line_height > ry + (int)rh) break;
			TextOutA(_memdc, x, y, line.c_str(), (int)line.size());
			y += line_height;
		}

		SelectClipRgn(_memdc, nullptr);
		DeleteObject(hrgn);
		SelectObject(_memdc, prevfont);
		_dirty = true;
	}

	void draw_spans(const std::vector<std::vector<ColorSpan>>& lines) override {
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		HFONT prevfont = (HFONT)SelectObject(_memdc, _font);
		TEXTMETRICA tm;
		GetTextMetricsA(_memdc, &tm);
		int line_height = tm.tmHeight;

		unsigned int rw = 0;
		unsigned int rh = 0;
		for(size_t i = 0; i < lines.size(); i++) {
			int line_width = 0;
			for(const ColorSpan& sp : lines[i]) {
				SIZE sz;
				GetTextExtentPoint32A(_memdc, sp.text.c_str(), (int)sp.text.size(), &sz);
				line_width += sz.cx;
			}
			if((unsigned int)line_width > rw) rw = line_width;
			rh += line_height;
		}
		rw += borderpx * 2;
		rh += borderpx * 2;

		SelectObject(_memdc, prevfont);
		draw_region_spans(lines, 0, 0, rw, rh);
	}

	void draw_region_spans(const std::vector<std::vector<ColorSpan>>& lines, int rx, int ry,
		unsigned int rw, unsigned int rh) override {
		int borderpx = (_borderless || gconf.borderless) ? 0 : gconf.borderpx;

		if(_use_fixed_geometry) {
			_hidden = false;
			_window_width = _fixed_w;
			_window_height = _fixed_h;
		} else if(_fullscreen) {
			_hidden = false;
			_window_width = _screen_width;
			_window_height = _screen_height;
		} else {
			_hidden = false;
			_window_width = rw;
			_window_height = rh;
		}

		ensure_bitmap(_window_width, _window_height);

		HFONT prevfont = (HFONT)SelectObject(_memdc, _font);
		TEXTMETRICA tm;
		GetTextMetricsA(_memdc, &tm);
		int line_height = tm.tmHeight;

		// Clear region
		RECT rect = {rx, ry, rx + (LONG)rw, ry + (LONG)rh};
		HBRUSH bg_brush = CreateSolidBrush(_background_color);
		FillRect(_memdc, &rect, bg_brush);
		DeleteObject(bg_brush);

		// Set clipping region
		HRGN hrgn = CreateRectRgn(rx, ry, rx + rw, ry + rh);
		SelectClipRgn(_memdc, hrgn);

		SetBkMode(_memdc, TRANSPARENT);

		int y = ry + borderpx;
		for(size_t i = 0; i < lines.size(); i++) {
			const std::vector<ColorSpan>& spans = lines[i];
			int line_width = 0;
			for(const ColorSpan& sp : spans) {
				SIZE sz;
				GetTextExtentPoint32A(_memdc, sp.text.c_str(), (int)sp.text.size(), &sz);
				line_width += sz.cx;
			}

			int x = rx + borderpx;
			if(gconf.align == 'r') {
				if(line_width < (int)rw) x = rx + (rw - line_width);
			} else if(gconf.align == 'c') {
				if(line_width < (int)rw) x = rx + (rw - line_width) / 2;
			}

			for(const ColorSpan& sp : spans) {
				if(sp.text.empty()) continue;
				SetTextColor(_memdc, rgb_to_colorref(sp.rgb));
				SIZE sz;
				GetTextExtentPoint32A(_memdc, sp.text.c_str(), (int)sp.text.size(), &sz);
				TextOutA(_memdc, x, y, sp.text.c_str(), (int)sp.text.size());
				x += sz.cx;
			}

			y += line_height;
			if(y > ry + (int)rh) break;
		}

		SelectClipRgn(_memdc, nullptr);
		DeleteObject(hrgn);
		SelectObject(_memdc, prevfont);
		_dirty = true;
	}

	void run() override {
		if(_hidden) {
			ShowWindow(_hwnd, SW_HIDE);
			return;
		}

		if(_dirty) {
			int x, y;
			if(_use_fixed_geometry) {
				x = _fixed_x;
				y = _fixed_y;
			} else if(_fullscreen) {
				x = 0;
				y = 0;
			} else {
				x = pos(gconf.px, _screen_width);
				if(gconf.px.prefix == '-') {
					x = _screen_width + x - _window_width;
				}
				x += pos(gconf.tx, _window_width);

				y = pos(gconf.py, _screen_height);
				if(gconf.py.prefix == '-') {
					y = _screen_height + y - _window_height;
				}
				y += pos(gconf.ty, _window_height);
			}

			SetWindowPos(_hwnd, gconf.window_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
				x, y, _window_width, _window_height, SWP_SHOWWINDOW);

			// Blit directly to window
			if(_memdc && _bitmap) {
				HDC windowDC = GetDC(_hwnd);
				BitBlt(windowDC, 0, 0, _window_width, _window_height, _memdc, 0, 0, SRCCOPY);
				ReleaseDC(_hwnd, windowDC);
			}
			_dirty = false;
		}

		// Process pending messages
		MSG msg;
		while(PeekMessage(&msg, _hwnd, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	static int pos(struct gs g, int size) {
		int sign = g.prefix == '-' ? -1 : 1;
		switch(g.suffix) {
		case '%':
			return sign * (int)((g.value / 100.0) * size);
		default:
			return sign * g.value;
		}
	}
};

// Factory implementation for Windows
inline ShWindowPtr StowWindow::create() {
	return std::static_pointer_cast<StowWindow>(Win32Window::create());
}

#endif // STOW_WINDOWS
