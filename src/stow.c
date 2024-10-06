/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include "arg.h"

// global struct
struct g {
	int value;
	char prefix;
	char suffix;
};

#include "config.h"

#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))
#define INITIAL_CAPACITY 2

//  globals!
static char* argv0;
static char** cmd;
static pid_t cmdpid;
static FILE* inputf;
static char* text;
static size_t len;
static size_t cap;
static int spipe[2];

// xlib and xft
static Display* dpy;
static int xfd;
static int screen;
static int depth = 32;
static Window win, root;
static Drawable drawable;
static GC xgc;
static XftDraw* xdraw;
static XftColor xforeground, xbackground;
static XftFont* xfont;
static unsigned int screen_width, screen_height;
static unsigned int window_width, window_height;
static bool hidden = true;
static bool overlay = true;

__attribute__((noreturn)) static void die(const char* fmt, ...) {
	// returns error message and quits
	int tmp = errno;
	va_list ap;

	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);

	if(fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		(void)fputc(' ', stderr);
		errno = tmp;
		perror(NULL);
	} else {
		(void)fputc('\n', stderr);
	}

	exit(1);
}

static void usage() {
	// displays usage options and quits through die()
	die("usage: stow [-Vt] [-x pos] [-y pos] [-X pos] [-Y pos] [-a align]\n"
		"           [-f foreground] [-b background] [-F font] [-B borderpx]\n"
		"           [-p period] [-A alpha] command [arg ...]");
}

static void signal_handler(int s) {
	// dunno what this does
	if(-1 == write(spipe[1], s == SIGCHLD ? "c" : "a", 1)) abort();
}

static void start_cmd() {
	// this code uses a lot of globals...
	// what is fds?
	int fds[2];

	// check pipe exists and is good
	if(-1 == pipe(fds)) die("pipe:");

	// read input
	inputf = fdopen(fds[0], "r");
	if(inputf == NULL) die("fdopen:");

	// create a fork
	cmdpid = fork();
	switch(cmdpid) {
	// bad fork?
	case -1:
		die("fork:");
	case 0:
		// close everything
		close(spipe[0]);
		close(spipe[1]);
		close(xfd);
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		setpgid(0, 0);
		execvp(cmd[0], cmd);
		exit(1);
	default:
		// resume normal op
		break;
	}

	// what is fds[1]?
	close(fds[1]);
}

static void read_text() {
	// self-explanatory
	int dlen = strlen(delimeter);

	len = 0;
	for(;;) {
		if(len + dlen + 2 > cap) {
			// buffer must have sufficient capacity to
			// store delimeter string, \n and \0 in one read
			cap = cap ? cap * 2 : INITIAL_CAPACITY;
			text = realloc(text, cap);
			if(text == NULL) die("realloc:");
		}

		char* line = &text[len];
		if(NULL == fgets(line, cap - len, inputf)) {
			if(feof(inputf)) {
				if(fclose(inputf) == -1) die("fclose subcommand output:");
				inputf = NULL;
				break;
			} else {
				die("fgets subcommand output:");
			}
		}

		int llen = strlen(line);

		if(line[llen - 1] == '\n') {
			line[--llen] = '\0';
			len += llen + 1;
		} else {
			len += llen;
		}

		if(llen == dlen && strcmp(line, delimeter) == 0) {
			len -= dlen + 2;
			break;
		}
	}
}

static void draw() {
	// draw window
	unsigned int prev_mw = window_width;
	unsigned int prev_mh = window_height;

	// find maximum text line width and height
	window_width = 0;
	window_height = 0;
	for(char* line = text; line < text + len; line += strlen(line) + 1) {
		// X library glypth for each character?
		XGlyphInfo ex;
		XftTextExtentsUtf8(dpy, xfont, (unsigned char*)line, strlen(line), &ex);

		// check we're onscreen
		if(ex.xOff > window_width) window_width = ex.xOff;
		window_height += xfont->ascent + xfont->descent;
	}

	// hidden is a zero size window ...
	hidden = window_width == 0 || window_height == 0;
	if(hidden) {
		printf("0 size window = hidden\n");
		return;
	}

	// add border to window sizes
	window_width += borderpx * 2;
	window_height += borderpx * 2;

	// why would the window sizes change here? @hey
	if(window_width != prev_mw || window_height != prev_mh) {
		// TODO: for some reason old GC value still works after XFreePixmap call
		// creates a new pixmap
		//printf("window size changes, redraw pixmap\n");
		XFreePixmap(dpy, drawable);
		drawable = XCreatePixmap(dpy, root, window_width, window_height, depth);
		if(!drawable) die("cannot allocate drawable");
		XftDrawChange(xdraw, drawable);
	}

	//printf("setting stow foreground\n");
	XSetForeground(dpy, xgc, xbackground.pixel);
	XFillRectangle(dpy, drawable, xgc, 0, 0, window_width, window_height);

	// render text lines
	unsigned int y = borderpx;
	for(char* line = text; line < text + len; line += strlen(line) + 1) {
		// more glyphs ... ?
		XGlyphInfo ex;
		XftTextExtentsUtf8(dpy, xfont, (unsigned char*)line, strlen(line), &ex);

		// text alignment
		unsigned int x = borderpx;
		if(align == 'r') {
			x = window_width - ex.xOff;
		} else if(align == 'c') {
			x = (window_width - ex.xOff) / 2;
		}

		// X library draw function
		XftDrawStringUtf8(xdraw, &xforeground, xfont, x, y + xfont->ascent, (unsigned char*)line, strlen(line));
		y += xfont->ascent + xfont->descent;
	}
}

static void reap() {
	// does this kill the process?
	for(;;) {
		int wstatus;
		pid_t p = waitpid(-1, &wstatus, cmdpid == 0 ? WNOHANG : 0);
		if(p == -1) {
			if(cmdpid == 0 && errno == ECHILD) {
				errno = 0;
				break;
			}
			die("waitpid:");
		}
		if(p == 0) break;
		if(p == cmdpid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
			cmdpid = 0;
		}
	}
}

static int pos(struct g g, int size) {
	//
	int sign = g.prefix == '-' ? -1 : 1;
	switch(g.suffix) {
	case '%':
		return sign * (g.value / 100.0) * size;
	default:
		return sign * g.value;
	}
}

static void run() {
	// run event loop
	bool restart_now = true;
	for(;;) {
		// check if we should restart
		if(restart_now && cmdpid == 0 && inputf == NULL) {
			restart_now = false;
			start_cmd();
		}

		// what is this flag
		bool dirty = false;

		// dunno what this is
		int inputfd = 0;
		if(inputf != NULL) {
			inputfd = fileno(inputf);
			// TODO: Handle fileno error
		}

		// different inputs?
		struct pollfd fds[] = {
			{.fd = spipe[0], .events = POLLIN}, // Signals
			{.fd = xfd, .events = POLLIN}, // X events
			{.fd = inputfd, .events = POLLIN} // Subcommand output
		};

		int fds_len = LENGTH(fds);
		if(inputfd == 0) {
			fds_len--;
		}

		// check if poll is bad?
		if(-1 == poll(fds, fds_len, -1)) {
			if(errno == EINTR) {
				errno = 0;
				continue;
			}
			die("poll:");
		}

		// Read subcommand output
		// this is where we determines the command passed into our
		// stow program
		if(inputf && (fds[2].revents & POLLIN || fds[2].revents & POLLHUP)) {
			read_text();
			draw();
			dirty = true;
		}

		// Handle signals
		if(fds[0].revents & POLLIN) {
			char s;
			if(-1 == read(spipe[0], &s, 1)) die("sigpipe read:");

			if(s == 'c') {
				// SIGCHLD received
				reap();
				if(period < 0) {
					restart_now = true;
				} else if(!restart_now) {
					alarm(period);
				}

			} else if(s == 'a' && cmdpid == 0) {
				// SIGALRM received
				restart_now = true;
			}
		}

		// Process X events
		if(fds[1].revents & POLLIN) {
			while(XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);

				if(ev.type == Expose && ev.xexpose.count == 0) {
					// Last expose event processed, redraw once
					dirty = true;

				} else if(ev.type == ButtonPress) {
					// X Window was clicked, restart subcommand
					// here's where we see a button press
					// this is where an overlay window should send
					// the signal to any underlying windows ...
					// so we need some window index and position and
					// order from foreground to background
					//printf("ButtonPress registered\n");
					int root_x, root_y;
					unsigned int mask;
					XQueryPointer(dpy,
						DefaultRootWindow(dpy),
						&root,
						&root,
						&root_x,
						&root_y,
						&root_x,
						&root_y,
						&mask); //<--four

					//printf("Mouse coordinates (X: %d, Y: %d)\n", root_x, root_y);

					// get tree of windows
					/*
					Window root_win, par_win;
					Window* chd_win;
					unsigned int num_win;
					XQueryTree(dpy, XRootWindow(dpy, 0), &root_win, &par_win, &chd_win, &num_win);

					printf("num wins: %lu\n", num_win);

                    // create new pointer event
                    XEvent pev;
                    

					// walk over windows
					// send event to every window? lol
                    // appears XSendEvent is blocked by some applications
                    // so we use a Xfixes set region instead
					for(int i = 0; i < num_win; i++) {
						// send coordinates to underlying window
                        //printf("send event: %i\n",i);
                        ev.xkey.window = chd_win[i];
						if(!XSendEvent(dpy, chd_win[i], 0, KeyPressMask, &ev)) {
							printf("click error\n");
						}
					}
                    */


					if(cmdpid && kill(-cmdpid, SIGTERM) == -1) {
						die("kill:");
					}
					alarm(0);
					restart_now = true;
				}
			}
		}

		// hidden window
		if(hidden) {
			XUnmapWindow(dpy, win);
			XSync(dpy, False);

		} else if(dirty) {
			// set window position within server
			if(window_on_top) {
				XRaiseWindow(dpy, win);
			} else {
				XLowerWindow(dpy, win);
			}

			XMapWindow(dpy, win);

			// set window position
			int x = pos(px, screen_width);
			if(px.prefix == '-') {
				x = screen_width + x - window_width;
			}
			x += pos(tx, window_width);

			int y = pos(py, screen_height);
			if(py.prefix == '-') {
				y = screen_height + y - window_height;
			}
			y += pos(ty, window_height);

			// final move and sync
			XMoveResizeWindow(dpy, win, x, y, window_width, window_height);
			XCopyArea(dpy, drawable, win, xgc, 0, 0, window_width, window_height, 0, 0);
			XSync(dpy, False);
		}
	}
}

static void setup(char* font) {
	// self pipe and signal handler

	// bad pipe
	if(pipe(spipe) == -1) die("pipe:");

	struct sigaction sa = {0};
	sa.sa_handler = signal_handler;
	sa.sa_flags = SA_RESTART;

	// check signals for bad values
	if(sigaction(SIGCHLD, &sa, NULL) == -1 || sigaction(SIGALRM, &sa, NULL) == -1) die("sigaction:");

	// xlib and xft
	// opens a new connection to the X server
	dpy = XOpenDisplay(NULL);
	if(!dpy) die("cannot open display");

	// get the display connection number to the X server
	xfd = ConnectionNumber(dpy);

	// set screen and window
	// can we get the other windows from this?
	// get the default screen number for our display connection
	// for a single screen app...
	screen = DefaultScreen(dpy);

	// debug get the total number of screens
	int screencount = ScreenCount(dpy);
	//printf("We find %i screens\n", screencount);

	// gets the root window for our display connection (dpy)
	// and the screen (monitor)
	// returns a Window object
	root = RootWindow(dpy, screen);

	// display size
	screen_width = DisplayWidth(dpy, screen);
	screen_height = DisplayHeight(dpy, screen);

	// looks like this sets our new display to the currently
	// set options
	XVisualInfo vi = {.screen = screen, .depth = depth, .class = TrueColor};
	XMatchVisualInfo(dpy, screen, vi.depth, TrueColor, &vi);
	Visual* visual = vi.visual;

	// creates a new colormap
	Colormap colormap = XCreateColormap(dpy, root, visual, None);
	// dumb 1x1 drawable only to initialize xdraw
	// I respect this comment, love a good hack
	drawable = XCreatePixmap(dpy, root, 1, 1, vi.depth);
	xdraw = XftDrawCreate(dpy, drawable, visual, colormap);
	xfont = XftFontOpenName(dpy, screen, font);
	if(!xfont) die("cannot load font");

	// allocate colors for foreground and background
	// TODO: use dedicated color variables instead of array
	if(!XftColorAllocName(dpy, visual, colormap, colors[0], &xforeground)) die("cannot allocate foreground color");
	if(!XftColorAllocName(dpy, visual, colormap, colors[1], &xbackground)) die("cannot allocate background color");

	// alpha blending
	xbackground.pixel &= 0x00FFFFFF;
	unsigned char r = ((xbackground.pixel >> 16) & 0xff) * alpha;
	unsigned char g = ((xbackground.pixel >> 8) & 0xff) * alpha;
	unsigned char b = (xbackground.pixel & 0xff) * alpha;
	xbackground.pixel = (r << 16) + (g << 8) + b;
	xbackground.pixel |= (unsigned char)(0xff * alpha) << 24;

	// window attributes
	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = xbackground.pixel;
	swa.border_pixel = xbackground.pixel;
	swa.colormap = colormap;
	swa.event_mask = ExposureMask | ButtonPressMask;

	// create our window
	win = XCreateWindow(dpy,
		root,
		-1,
		-1,
		1,
		1,
		0,
		vi.depth,
		InputOutput,
		visual,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask | CWColormap,
		&swa);

	// create a fixed region to allow passthrough
	if(overlay) {
		XRectangle rect;
		XserverRegion region = XFixesCreateRegion(dpy, &rect, 1);
		XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, region);
		XFixesDestroyRegion(dpy, region);
	}
    

	// graphics context
	XGCValues gcvalues = {0};
	gcvalues.graphics_exposures = False;
	xgc = XCreateGC(dpy, drawable, GCGraphicsExposures, &gcvalues);

	XSelectInput(dpy, win, swa.event_mask);
}

static int parsegeom(char* b, char* prefix, char* suffix, struct g* g) {
	g->prefix = 0;
	g->suffix = 0;

	if(b[0] < '0' || b[0] > '9') {
		for(char* t = prefix; *t; t++) {
			if(*t == b[0]) {
				g->prefix = b[0];
				break;
			}
		}
		if(g->prefix == 0) {
			return -1;
		}
		b++;
	}

	if(b[0] < '0' || b[0] > '9') return -1;

	char* e;
	long int li = strtol(b, &e, 10);

	if(b == e || li < INT_MIN || li > INT_MAX) return -1;

	g->value = (int)li;
	b = e;

	if(b[0] != '\0') {
		for(char* t = suffix; *t; t++) {
			if(*t == b[0]) {
				g->suffix = b[0];
				break;
			}
		}
		if(g->suffix == 0 || b[1] != '\0') {
			return -1;
		}
	}

	return 0;
}

static int stoi(char* s, int* r) {
	// string to int?
	char* e;
	long int li = strtol(s, &e, 10);
	*r = (int)li;
	return ((s[0] < '0' || s[0] > '9') && s[0] != '-') || li < INT_MIN || li > INT_MAX || *e != '\0';
}

int main(int argc, char* argv[]) {
	// here's main!
	char* xfont = font;

	// check input arguments
	ARGBEGIN {
	case 'V':
		//version
		printf("print version\n");
		printf("%s  VERSION \n", argv0);
		return 0;
		break;
	case 'x':
		// set x pos
		printf("set x pos\n");
		if(-1 == parsegeom(EARGF(usage()), "+-", "%", &px)) usage();
		break;
	case 'y':
		// set y pos
		printf("set y pos\n");
		if(-1 == parsegeom(EARGF(usage()), "+-", "%", &py)) usage();
		break;
	case 'X':
		// is this just capital?
		printf("set x/X pos\n");
		if(-1 == parsegeom(EARGF(usage()), "+-", "%", &tx)) usage();
		break;
	case 'Y':
		printf("set y/Y pos\n");
		if(-1 == parsegeom(EARGF(usage()), "+-", "%", &ty)) usage();
		break;
	case 'f':
		// set foreground color
		printf("setting foreground color\n");
		colors[0] = EARGF(usage());
		break;
	case 'b':
		// set background color
		printf("set background color\n");
		colors[1] = EARGF(usage());
		break;
	case 'F':
		// set font
		printf("setting font\n");
		xfont = EARGF(usage());
		break;
	case 'B':
		// set border pixels
		printf("set border pixel thickness\n");
		if(stoi(EARGF(usage()), &borderpx)) usage();
		break;
	case 'a': {
		// alignment
		printf("set alignment\n");
		const char* a = EARGF(usage());
		align = a[0];
		if(strlen(a) != 1 || (align != 'l' && align != 'r' && align != 'c')) usage();
	} break;
	case 'p':
		// period?
		printf("period set\n");
		if(stoi(EARGF(usage()), &period)) usage();
		break;
	case 'A': {
		// set alpha (transparency)
		printf("set alpha (transparency)\n");
		char* s = EARGF(usage());
		char* end;
		alpha = strtod(s, &end);
		if(*s == '\0' || *end != '\0' || alpha < 0 || alpha > 1) usage();
	} break;
	case 't':
		// set window on top
		printf("set window on top\n");
		window_on_top = true;
		break;
	default:
		// default
		printf("default\n");
		usage();
	}
	ARGEND

	if(argc == 0) {
		// no arguments return list and kills process
		printf("enter some args!\n");
		usage();
	}

	// set the input argument command to our global pointer
	cmd = argv;

	// run the setup
	setup(xfont);

	// run the process
	run();

	return 0;
}
