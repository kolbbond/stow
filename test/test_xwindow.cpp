// test xwindow
#include "xwindow.hpp"

int main() {

	// create
	ShXWindowPr xwin = XWindow::create();

	// setup window -> what do we need to pass into it
	xwin->setup();

	// draw
	while(true) {
		xwin->draw("hello\0");
		xwin->run();
	}
}
