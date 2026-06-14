// headless unit tests for widget value logic
#include "stow/widget.hpp"

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

static void test_interface() {
	NullWidget w;
	CHECK(w.fd() == -1);          // default: not pollable
	CHECK(w.dirty() == true);     // default: always redraw
	w.update();
	CHECK(w.updates == 1);
}

int main() {
	test_interface();
	if(g_failures) { std::cerr << g_failures << " checks failed\n"; return 1; }
	std::cout << "all widget tests passed\n";
	return 0;
}
