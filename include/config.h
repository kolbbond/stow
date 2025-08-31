/* See LICENSE file for copyright and license details. */
#pragma once
#include <string>
#include <array>


// global struct
struct gs {
	int value;
	char prefix;
	char suffix;
};
/* X window geometry */
// assume 1920x1080?
struct GlobalConfig {
	gs px;
	gs py;
	gs tx;
	gs ty;
	int borderpx;
	double alpha; // background opacity
	char align;
	std::string font;
	std::array<std::string, 2> colors;
	int period;
	char delimeter;
	bool window_on_top;
};


// set desired values here
static struct GlobalConfig gconf = //
	{.px = {-1600},
		.py = {20},
		.tx = {600},
		.ty = {10},
		.borderpx = 2,
		.alpha = 0.8, /* background opacity */
		.align = 'l', /* text alignment: l, r and c for left, right and centered respectively */
		.font = "monospace:size=10",
		.colors = {"#00fbbb", "#0000ff"}, /* foreground and background colors */
		.period = 1, /* time in seconds between subcommand runs. */
		.delimeter = '\4',
		.window_on_top = 1};

/* delimeter string, encountered as a separate line in subcommand output
signals stw to render buffered text and continue with next frame;
it is the only valid use of non-printable characters in subcommand output */
