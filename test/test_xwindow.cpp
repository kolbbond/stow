// test xwindow
#include "xwindow.hpp"

std::string teststr =
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vivamus luctus urna sed urna ultricies ac tempor dui \n"
	"sagittis. In condimentum facilisis porta. Sed nec diam eu diam mattis viverra. Nulla fringilla, orci ac euismod\n"
	"semper, massa velit luctus felis, a aliquet magna ante ac quam. Maecenas fermentum consequat mi. Donec "
	"fermentum.\n"
	"Pellentesque tincidunt, sem in lacinia aliquet, mi nisl adipiscing odio, eu gravida dolor metus non quam. \n"
	"Curabitur ultrices ligula ut augue laoreet, nec malesuada enim condimentum. Pellentesque et nulla vel nisi \n"
	"pretium tincidunt ac sit amet eros. Sed malesuada odio augue, id suscipit felis pharetra eget. Mauris ac dui sit\n"
	"amet augue lacinia luctus. Fusce congue nunc sit amet nisl mollis pharetra. Etiam venenatis, lorem sit amet \n"
	"auctor varius, ante nisi laoreet risus, vel fringilla mauris enim sed nulla. Suspendisse potenti.	Nam aliquam \n"
	"hendrerit enim,			sed feugiat sapien posuere non.Ut fermentum dui lectus,			at viverra urna \n"
	"iaculis a.Integer non faucibus justo,			a faucibus nisl.Nam tristique nibh id libero malesuada \n"
	"tempor.Nulla non libero at purus cursus volutpat				.Mauris eleifend mi a lorem dictum lacinia.Etiam \n"
	"ut felis risus.Nullam euismod est in ipsum cursus,			a sodales sapien sodales.Ut sed arcu a justo "
	"faucibus\n"
	"cursus et vel nulla.Phasellus				placerat laoreet mollis.Aenean viverra auctor orci,			in mollis\n"
	"lorem hendrerit a.Suspendisse cursus dolor ligula,			sed lacinia arcu pellentesque et.Duis vitae leo\n"
	"mollis, feugiat turpis a,			ullamcorper arcu.Aliquam erat volutpat.Nam congue, nisi ut sodales suscipit, \n"
	"eros odio tristique leo,AAtae condimentum risus arcu at enim.Nulla facilisi.\0";

int main() {
	// create
	ShXWindowPr xwin = XWindow::create();

	// setup window -> what do we need to pass into it
	xwin->setup();

	// draw
	bool done = false;
	int cnt = 0;
	while(!done) {
		// test dynamic string
		teststr = "counter: " + std::to_string(cnt++) + "\n";

		xwin->draw(teststr);
		xwin->run();
	}
}
