/* See LICENSE file for copyright and license details. */
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>

#include "arg.h"

struct g {
	int value;
	char prefix;
	char suffix;
};

#include "config.h"

#define LENGTH(X) (sizeof X / sizeof X[0])
#define INITIAL_CAPACITY 2

static char *argv0;
static unsigned int sw, sh;
static unsigned int mw, mh;
static char **cmd;
static pid_t cmdpid;
static FILE *inputf;
static char *text;
static size_t len;
static size_t cap;
static int spipe[2];

// xlib and xft
static Display *dpy;
static int xfd;
static int screen;
static int depth = 32;
static Window win, root;
static Drawable drawable;
static GC xgc;
static XftDraw *xdraw;
static XftColor xforeground, xbackground;
static XftFont *xfont;

static void
die(const char *fmt, ...)
{
	int tmp = errno;
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fputc(' ', stderr);
		errno = tmp;
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

static void
usage()
{
	die(
"usage: %s\n\
	[-x x position]\n\
	[-y y position]\n\
	[-X x translate]\n\
	[-Y y translate]\n\
	[-a alignment]\n\
	[-f foreground]\n\
	[-b background]\n\
	[-F font]\n\
	[-B borderpx]\n\
	[-p period]\n\
	[-A alpha]\n\
	command [args ...]",
argv0);
}

static void
signal_handler(int s)
{
	if (-1 == write(spipe[1], s == SIGCHLD ? "c" : "a", 1))
		abort();
}

static void
start_cmd()
{
	int fds[2];
	if (-1 == pipe(fds))
		die("pipe:");

	inputf = fdopen(fds[0], "r");
	if (inputf == NULL)
		die("fdopen:");

	cmdpid = fork();
	switch (cmdpid) {
	case -1:
		die("fork:");
	case 0:
		close(spipe[0]);
		close(spipe[1]);
		close(xfd);
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		execvp(cmd[0], cmd);
		exit(1);
	}

	close(fds[1]);
}

static void
read_text()
{
	int dlen = strlen(delimeter);

	len = 0;
	for (;;) {
		if (len + dlen + 2 > cap) {
			// buffer must have sufficient capacity to
			// store delimeter string, \n and \0 in one read
			cap = cap ? cap * 2 : INITIAL_CAPACITY;
			text = realloc(text, cap);
			if (text == NULL)
				die("realloc:");
		}

		char *line = &text[len];
		if (NULL == fgets(line, cap - len, inputf)) {
			if (feof(inputf)) {
				break;
			} else {
				die("error reading stdin");
			}
		}

		int llen = strlen(line);

		if (line[llen - 1] == '\n') {
			line[--llen] = '\0';
			len += llen + 1;
		} else {
			len += llen;
		}

		if (llen == dlen && strcmp(line, delimeter) == 0) {
			len -= dlen + 2;
			break;
		}
	}
}

static void
draw()
{
	unsigned int prev_mw = mw;
	unsigned int prev_mh = mh;

	// find maximum text line width and height
	mw = 0;
	mh = 0;
	for (char *line = text; line < text + len; line += strlen(line) + 1) {
		XGlyphInfo ex;
		XftTextExtentsUtf8(dpy, xfont, (unsigned char *)line, strlen(line), &ex);
		if (ex.xOff > mw)
			mw = ex.xOff;
		mh += xfont->ascent + xfont->descent;
	}

	mw += borderpx * 2;
	mh += borderpx * 2;

	if (mw != prev_mw || mh != prev_mh) {
		// todo: for some reason old GC value still works after XFreePixmap call
		XFreePixmap(dpy, drawable);
		drawable = XCreatePixmap(dpy, root, mw, mh, depth);
		if (!drawable)
			die("cannot allocate drawable");
		XftDrawChange(xdraw, drawable);
	}
	XSetForeground(dpy, xgc, xbackground.pixel);
	XFillRectangle(dpy, drawable, xgc, 0, 0, mw,mh);

	// render text lines
	unsigned int y = borderpx;
	for (char *line = text; line < text + len; line += strlen(line) + 1) {
		XGlyphInfo ex;
		XftTextExtentsUtf8(dpy, xfont, (unsigned char *)line, strlen(line), &ex);

		// text alignment
		unsigned int x = borderpx;
		if (align == 'r') {
			x = mw - ex.xOff;
		} else if (align == 'c') {
			x = (mw - ex.xOff) / 2;
		}

		XftDrawStringUtf8(
			xdraw, &xforeground, xfont, x, y + xfont->ascent,
			(unsigned char *)line, strlen(line)
		);
		y += xfont->ascent + xfont->descent;
	}
}

static void
reap()
{
	for (;;) {
		int wstatus;
		pid_t p = waitpid(-1, &wstatus, cmdpid == 0 ? WNOHANG : 0);
		if (p == -1) {
			if (cmdpid == 0 && errno == ECHILD) {
				errno = 0;
				break;
			}
			die("waitpid:");
		}
		if (p == 0)
			break;
		if (p == cmdpid && (WIFEXITED(wstatus) || WIFSIGNALED(wstatus))) {
			cmdpid = 0;
		}
	}
	if (fclose(inputf) == -1)
		die("close:");
}

static void
run()
{
	struct pollfd fds[] = {
		{.fd = spipe[0], .events = POLLIN}, // signals
		{.fd = xfd,      .events = POLLIN}, // xlib
		{.fd = 0,        .events = POLLIN}  // cmd stdout (set later)
	};

	int restart_now = 1;
	for (;;) {
		if (restart_now && cmdpid == 0) {
			restart_now = 0;
			start_cmd();
		}

		int dirty = 0;
		fds[2].fd = cmdpid != 0 ? fileno(inputf) : 0;
		for (int i = 0; i < LENGTH(fds); i++)
			fds[i].revents = 0;

		if (-1 == poll(fds, LENGTH(fds) - (cmdpid == 0), -1)) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			die("poll:");
		}


		if (fds[0].revents & POLLIN) {
			// signals
			char s;
			if (-1 == read(spipe[0], &s, 1))
				die("sigpipe read:");

			if (s == 'c') {
				// sigchld received
				reap();
				if (!restart_now) {
					alarm(period);
				}
			} else if (s == 'a' && cmdpid == 0) {
				// sigalrm received
				start_cmd();
			}
		}

		if (fds[1].revents & POLLIN) {
			// xlib
			while (XPending(dpy)) {
				XEvent ev;
				XNextEvent(dpy, &ev);

				if (ev.type == Expose) {
					if (ev.xexpose.count == 0) {
						dirty = 1;
					}
				} else if (ev.type == ButtonPress) {
					if (cmdpid && kill(cmdpid, SIGTERM) == -1)
						die("kill:");
					alarm(0);
					restart_now = 1;
				}
			}
		}

		if (fds[2].revents & POLLIN) {
			// cmd stdout
			read_text();
			draw();
			dirty = 1;
		}

		if (dirty && mw > 0 && mh > 0) {
			// window redraw
			int x = px.suffix == '%'
				? (px.value / 100.0) * sw
				: px.value;

			if (px.prefix == '-') {
				x = sw - x - mw;
			}

			if (tx.value != '0') {
				int v = tx.value;
				if (tx.suffix == '%')
					v = (v / 100.0) * mw;
				if (tx.prefix == '-')
					v *= -1;
				x += v;
			}

			int y = py.suffix == '%'
				? (py.value / 100.0) * sh
				: py.value;

			if (py.prefix == '-') {
				y = sh - y - mh;
			}

			if (ty.value != '0') {
				int v = ty.value;
				if (ty.suffix == '%')
					v = (v / 100.0) * mh;
				if (ty.prefix == '-')
					v *= -1;
				y += v;
			}

			XMoveResizeWindow(dpy, win, x, y,mw, mh);
			XCopyArea(dpy, drawable, win, xgc, 0, 0, mw, mh, 0, 0);
			XSync(dpy, False);
		}
	}
}

static void
setup(char *font)
{
	// self pipe and signal handler

	if (pipe(spipe) == -1)
		die("pipe:");

	struct sigaction sa = {0};
	sa.sa_handler = signal_handler;
	sa.sa_flags = SA_RESTART;

	if (sigaction(SIGCHLD, &sa, NULL) == -1
	|| sigaction(SIGALRM, &sa, NULL) == -1)
		die("sigaction:");

	// xlib and xft

	dpy = XOpenDisplay(NULL);
	if (!dpy)
		die("cannot open display");

	xfd = ConnectionNumber(dpy);

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);

	XVisualInfo vi = {
		.screen = screen,
		.depth = depth,
		.class = TrueColor
	};
	XMatchVisualInfo(dpy, screen, vi.depth, TrueColor, &vi);
	Visual *visual = vi.visual;

	Colormap colormap = XCreateColormap(dpy, root, visual, None);
	// dumb 1x1 drawable only to initialize xdraw
	drawable = XCreatePixmap(dpy, root, 1, 1, vi.depth);
	xdraw = XftDrawCreate(dpy, drawable, visual, colormap);
	xfont = XftFontOpenName(dpy, screen, font);
	if (!xfont)
		die("cannot load font");

	// todo: use dedicated color variables instead of array
	if (!XftColorAllocName(dpy, visual, colormap, colors[0], &xforeground))
		die("cannot allocate foreground color");
	if (!XftColorAllocName(dpy, visual, colormap, colors[1], &xbackground))
		die("cannot allocate background color");

	// alpha blending
	xbackground.pixel &= 0x00FFFFFF;
	unsigned char r = ((xbackground.pixel >> 16) & 0xff) * alpha;
	unsigned char g = ((xbackground.pixel >> 8) & 0xff) * alpha;
	unsigned char b = (xbackground.pixel & 0xff) * alpha;
	xbackground.pixel = (r << 16) + (g << 8) + b;
	xbackground.pixel |= (unsigned char)(0xff * alpha) << 24;

	XSetWindowAttributes swa;
	swa.override_redirect = True;
	swa.background_pixel = xbackground.pixel;
	swa.border_pixel = xbackground.pixel;
	swa.colormap = colormap;
	swa.event_mask = ExposureMask | ButtonPressMask;
	win = XCreateWindow(
		dpy, root,
		-1, -1, 1, 1, 0,
		vi.depth, InputOutput, visual,
		CWOverrideRedirect | CWBackPixel | CWBorderPixel | CWEventMask | CWColormap,
		&swa
	);

	XGCValues gcvalues = {0};
	gcvalues.graphics_exposures = False;
	xgc = XCreateGC(dpy, drawable, GCGraphicsExposures, &gcvalues);

	XLowerWindow(dpy, win);
	XMapWindow(dpy, win);
	XSelectInput(dpy, win, swa.event_mask);
}

static int
parsegeom(char *b, char *prefix, char *suffix, struct g *g)
{
	g->prefix = 0;
	g->suffix = 0;

	if (b[0] < '0' || b[0] > '9') {
		for (char *t = prefix; *t; t++) {
			if (*t == b[0]) {
				g->prefix = b[0];
				break;
			}
		}
		if (g->prefix == 0) {
			return -1;
		}
		b++;
	}

	if (b[0] < '0' || b[0] > '9')
		return -1;

	char *e;
	long int li = strtol(b, &e, 10);

	if (b == e || li < INT_MIN || li > INT_MAX)
		return -1;

	g->value = (int)li;
	b = e;

	if (b[0] != '\0') {
		for (char *t = suffix; *t; t++) {
			if (*t == b[0]) {
				g->suffix = b[0];
				break;
			}
		}
		if (g->suffix == 0 || b[1] != '\0') {
			return -1;
		}
	}

	return 0;
}

static int
stoi(char *s, int *r) {
	char *e;
	long int li = strtol(s, &e, 10);
	*r = (int)li;
	return s[0] < '0' || s[0] > '9' \
		|| li < INT_MIN || li > INT_MAX \
		|| *e != '\0';
}

int
main(int argc, char *argv[])
{
	char *xfont = font;

	ARGBEGIN {
	case 'x':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &px))
			usage();
		break;
	case 'y':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &py))
			usage();
		break;
	case 'X':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &tx))
			usage();
		break;
	case 'Y':
		if (-1 == parsegeom(EARGF(usage()), "+-", "%", &ty))
			usage();
		break;
	case 'f':
		colors[0] = EARGF(usage());
		break;
	case 'b':
		colors[1] = EARGF(usage());
		break;
	case 'F':
		xfont = EARGF(usage());
		break;
	case 'B':
		if (stoi(EARGF(usage()), &borderpx))
			usage();
		break;
	case 'a': {
		const char *a = EARGF(usage());
		align = a[0];
		if (strlen(a) != 1 \
		|| (align != 'l' && align != 'r' && align != 'c'))
			usage();
	} break;
	case 'p':
		if (stoi(EARGF(usage()), &period))
			usage();
		break;
	case 'A': {
		char *s = EARGF(usage());
		char *end;
		alpha = strtod(s, &end);
		if (*s == '\0' || *end != '\0' || alpha < 0 || alpha > 1)
			usage();
	} break;
	default:
		usage();
	} ARGEND

	if (argc == 0)
		usage();

	cmd = argv;

	setup(xfont);
	run();
}