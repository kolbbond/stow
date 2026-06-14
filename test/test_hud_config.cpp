// headless unit tests for the sectioned-INI config parser
#include "stow/hud_config.hpp"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

// write `content` to a temp file, return its path
static std::string write_tmp(const std::string& name, const std::string& content) {
	std::string path = "/tmp/stow_test_" + name;
	std::ofstream out(path);
	out << content;
	out.close();
	return path;
}

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
	std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++g_failures; } } while(0)

static void test_missing_file() {
	stow::HudConfig cfg = stow::HudConfig::load("/tmp/stow_does_not_exist.ini");
	CHECK(!cfg.valid());
	CHECK(!cfg.error.empty());
}

int main() {
	test_missing_file();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all hud_config tests passed\n";
	return 0;
}
