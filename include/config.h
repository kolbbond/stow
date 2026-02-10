// DEPRECATED: Legacy configuration - use stow/config.hpp instead
// This file is kept for backward compatibility during transition
#pragma once

#include <array>
#include <string>

// Legacy global struct - maps to new Position type
struct gs {
	int value = 0;
	char prefix = 0;
	char suffix = 0;
};

// Legacy GlobalConfig - provided for backward compatibility
// New code should use stow::WindowConfig and stow::ProcessConfig instead
struct GlobalConfig {
	gs px;
	gs py;
	gs tx;
	gs ty;
	int borderpx = 2;
	double alpha = 0.8;
	char align = 'l';
	std::string font = "monospace:size=10";
	std::array<std::string, 2> colors = {"#00a080", "#0000ff"};
	int period = 1;
	char delimeter = '\4';
	bool window_on_top = true;
	bool borderless = false;
};

// Legacy global config instance
// New code should use stow::Config instead
static struct GlobalConfig gconf = {};
