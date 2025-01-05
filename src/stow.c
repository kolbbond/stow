/* See LICENSE file for copyright and license details. */

#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "arg.h"

#include "error.h"

#include "config.h"

#define LENGTH(X) (sizeof(X) / sizeof((X)[0]))
#define INITIAL_CAPACITY 2

//  globals!
static char* argv0; // default version argument
static char** cmd; // command from argv
static pid_t cmdpid; // cmd process id
static FILE* inputf;
static char* text; // buffer to store text
static size_t len;
static size_t cap;
static int spipe[2]; // pipe file descriptors [0],[1] for reading,writing


static void signal_handler(int s) {
	// dunno what this does
	if(-1 == write(spipe[1], s == SIGCHLD ? "c" : "a", 1)) abort();
}

static void start_cmd() {
	printf("start command\n");

	// this code uses a lot of globals...
	// what is fds?
	int fds[2];

	// check pipe exists and is good
	if(-1 == pipe(fds)) die("pipe:");

	// read input and store in inputf
	inputf = fdopen(fds[0], "r");
	if(inputf == NULL) die("fdopen:");

	// create a fork
	cmdpid = fork();
	switch(cmdpid) {
	// bad fork?
	case -1:
		printf("bad fork\n");
		die("fork:");
	case 0:
		// new process (0 means success)
		printf("open process\n");

		// close old file descriptors
		close(spipe[0]);
		close(spipe[1]);

		// close x server connection number
		close(xfd);

		// close newest (read pipe)
		close(fds[0]);

		// copy write pipe to stdout_fileno
		dup2(fds[1], STDOUT_FILENO);

		// set process group ID of current process to its process ID hence: (0,0)
		setpgid(0, 0);

		// replace current process with cmd[0] with arguments cmd
		// doesn't error check?
		execvp(cmd[0], cmd);

		// exits current process
		exit(1);

	default:
		// weird output
		break;
	}

	// close file descriptor (write)
	close(fds[1]);
}

static void read_text() {
	printf("---read text\n");

	// length of chosen delimeter
	int dlen = strlen(delimeter);

	// for select
	fd_set readfds;
	struct timeval timeout;

	// while stuff to read
	len = 0;
	for(;;) {

		// if read length greater than buffer, add memory
		if(len + dlen + 2 > cap) {
			// buffer must have sufficient capacity to
			// store delimeter string, \n and \0 in one read
			cap = cap ? cap * 2 : INITIAL_CAPACITY; // doubles cap if we have any cap

			// reallocate memory
			text = realloc(text, cap);

			// error handle
			if(text == NULL) die("realloc:");
		}

		// select setup
		FD_ZERO(&readfds);
		FD_SET(spipe[0], &readfds);
		timeout.tv_sec = 1; // wait 1s
		timeout.tv_usec = 0;

		// check if there is data to be read
		int result = select(spipe[0] + 1, &readfds, NULL, NULL, &timeout);
		bool use_select = false;
		if(result > 0 || !use_select) {
			//printf("result: %i\n", result);
			// read lines from inputf filestream (with NULL check)
			char* line = &text[len];
			if(NULL == fgets(line, cap - len, inputf)) {
				// check end of stream
				if(feof(inputf)) {
					// close with error handle
					if(fclose(inputf) == -1) die("fclose subcommand output:");

					// reset file and break out
					inputf = NULL;
					break;
				} else {
					die("fgets subcommand output:");
				}
			}

			printf("input: %s\n", line);

			// check for end characters in read line
			int llen = strlen(line);
			if(line[llen - 1] == '\n') {
				line[--llen] = '\0';
				len += llen + 1;
			} else {
				len += llen;
			}

			// check for lines that are delimeter
			if(llen == dlen && strcmp(line, delimeter) == 0) {
				len -= dlen + 2;
				break;
			}

			// set max length ...
			int max_length = 20;
			if(len > max_length) {
				feof(inputf);
				break;
			}
		} else if(result == 0) {
			printf("timeout reached, no data\n");
		} else {
			perror("select failed");
		}
	}

	printf("--- done with text\n");
}



static void run() {
	// run event loop
	bool restart_now = true;
	for(;;) {

		// check if we should restart
		// if restart_now and the cmd process and input files are empty/done/NULL
		if(restart_now && cmdpid == 0 && inputf == NULL) {
			restart_now = false;

			// start cmd generates a new inputf and cmdpid (fork)
			// inputf = fdopen(fds[0], "r");
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

	} // forever loop
} // run

static void setup(char* font) {
	// self pipe and signal handler

	// bad pipe
	if(pipe(spipe) == -1) die("pipe:");

	struct sigaction sa = {0};
	sa.sa_handler = signal_handler;
	sa.sa_flags = SA_RESTART;

	// check signals for bad values
	if(sigaction(SIGCHLD, &sa, NULL) == -1 || sigaction(SIGALRM, &sa, NULL) == -1) die("sigaction:");
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
	// change to tclap
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
