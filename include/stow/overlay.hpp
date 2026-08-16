// stow::Overlay - a lightweight click-through overlay window.
//
//   #include <stow/overlay.hpp>
//   stow::Overlay ov({.anchor = stow::Anchor::TopRight});
//   ov.show();
//   while (ov.pump()) ov.set_text(status());
//
// The platform (X11) is entirely behind the pimpl; this header names no X11
// type, so a consumer needs no X11 include path and links only stow::stow.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "stow/color.hpp"
#include "stow/config.hpp"  // Anchor, Position
#include "stow/error.hpp"
#include "stow/layout.hpp"  // Rect
#include "stow/text.hpp"    // ColorSpan, Lines

namespace stow {

struct Size {
	unsigned int w = 0, h = 0;
	constexpr bool operator==(const Size&) const = default;
	constexpr bool empty() const { return w == 0 || h == 0; }
};

// What the compositor actually granted. Never a reason to fail construction:
// a WSLg or XWayland session may deny any of these and must still render.
struct Caps {
	bool clickthrough = false;
	bool transparency = false;
	bool override_redirect = false;
};

struct OverlayConfig {
	Anchor anchor = Anchor::Custom;
	Position x, y;     // used when anchor == Custom
	Size size;         // empty => size to content
	int monitor = -1;  // -1 => primary
	std::string font = "monospace:size=10";
	Color fg = Color::parse("#00a080");
	Color bg = Color::parse("#000000");
	double alpha = 0.8;
	bool clickthrough = true;
	bool on_top = true;
	bool borderless = true;
	int border_px = 2;  // ignored when borderless
	char align = 'l';   // 'l' | 'r' | 'c'
	std::string title = "stow";
};

class Overlay {
public:
	// Returns nullopt only when there is no usable display; *err then holds the
	// reason. Losing click-through or transparency is reported through caps(),
	// not through failure.
	static std::optional<Overlay> create(const OverlayConfig& cfg = {}, Error* err = nullptr);

	// Same, but throws stow::Error instead of returning nullopt.
	explicit Overlay(const OverlayConfig& cfg = {});

	Overlay(Overlay&&) noexcept;
	Overlay& operator=(Overlay&&) noexcept;
	Overlay(const Overlay&) = delete;
	Overlay& operator=(const Overlay&) = delete;
	~Overlay();

	// Lifecycle
	void show();
	void hide();
	void close();
	bool open() const;  // shown and not closed
	Caps caps() const;

	// Toggle input passthrough at runtime. No-op when the compositor never
	// granted it (caps().clickthrough == false).
	void set_clickthrough(bool enabled);

	// --- geometry (runtime) ---
	void move_to(int x, int y);
	void resize(Size s);
	Rect geometry() const;

	// --- immediate-mode drawing ---
	// begin() clears the frame, end() presents it. Calls in between paint in
	// issue order. This is what per-frame content (a cursor follower, a gauge)
	// wants; set_text/set_spans are the retained-style convenience over it.
	void begin();
	void rect(Rect r, Color c, int thickness = 0);  // thickness 0 => filled
	void text(int x, int y, std::string_view s, Color c = Color::white());
	void spans(const Lines& lines);
	void end();

	// Region-scoped, alignment-aware, clipped drawing - what a grid cell needs.
	void text_in(Rect region, std::string_view s);
	void spans_in(Rect region, const Lines& lines);

	// Content (convenience: a whole frame in one call)
	void set_text(std::string_view text);
	void set_spans(const Lines& lines);

	// --- loop ---
	// Test hook: the pixel value this overlay would hand to core-X11 drawing
	// for `c`. Exposed so a test can catch the alpha-stripping failure mode
	// (see test_overlay), which is invisible to any crash-only check.
	unsigned long debug_solid_pixel(Color c) const;

	// Services X events and presents. Returns false once closed.
	bool pump();                                   // non-blocking
	bool pump(std::chrono::milliseconds timeout);  // waits up to timeout on X events

	// Convenience loop for pure-HUD programs: while(pump(period)) cb(*this).
	void run(std::chrono::milliseconds period, std::function<void(Overlay&)> cb);

private:
	struct Impl;
	std::unique_ptr<Impl> _p;

	explicit Overlay(std::unique_ptr<Impl> p);
};

}  // namespace stow
