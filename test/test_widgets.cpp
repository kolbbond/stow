// headless unit tests for widget value logic
#include "stow/widgets.hpp"
#include "stow/widget.hpp"
#include "stow/capture.hpp"

#include <iostream>
#include <string>

static int g_failures = 0;
#define CHECK(cond) do { if(!(cond)) { \
	std::cerr << "FAIL: " << #cond << " (line " << __LINE__ << ")\n"; ++g_failures; } } while(0)

// a minimal concrete widget proves the interface is usable without X
namespace {
struct NullWidget : stow::Widget {
	int updates = 0;
	void update() override { updates++; }
	void render(stow::RenderCtx&) override {}
};
}

static void test_value_logic() {
	// number_text: label + value on two lines; missing label => value only
	CHECK(stow::number_text("cores", "8") == "cores\n8");
	CHECK(stow::number_text("", "8") == "8");

	// bar_fraction: clamp to [0,1]; non-numeric => 0
	CHECK(stow::bar_fraction("50", 100.0) == 0.5);
	CHECK(stow::bar_fraction("150", 100.0) == 1.0);   // clamp high
	CHECK(stow::bar_fraction("-5", 100.0) == 0.0);    // clamp low
	CHECK(stow::bar_fraction("abc", 100.0) == 0.0);   // non-numeric
	CHECK(stow::bar_fraction("1", 0.0) == 0.0);       // max<=0 guarded
}

static void test_capture() {
	// last non-empty line of stdout, trimmed
	CHECK(stow::capture_command("printf 'a\\nb\\n'") == "b");
	CHECK(stow::capture_command("printf '  42  \\n'") == "42");
	// trailing blank lines ignored
	CHECK(stow::capture_command("printf 'x\\n\\n\\n'") == "x");
	// empty output => empty string
	CHECK(stow::capture_command("true") == "");
	// command that fails to run => empty string (no throw, no crash)
	CHECK(stow::capture_command("this_command_does_not_exist_xyz 2>/dev/null") == "");
}

static void test_interface() {
	NullWidget w;
	CHECK(w.fd() == -1);          // default: not pollable
	CHECK(w.dirty() == true);     // default: always redraw
	w.update();
	CHECK(w.updates == 1);
}

int main() {
	test_value_logic();
	test_capture();
	test_interface();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all widget tests passed\n";
	return 0;
}
