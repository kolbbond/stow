#pragma once
// error

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>


class Error {

public:
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

	static void reap(int cmdpid) {
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

	static void usage() {
		// displays usage options and quits through die()
		die("usage: stow [-Vt] [-x pos] [-y pos] [-X pos] [-Y pos] [-a align]\n"
			"           [-f foreground] [-b background] [-F font] [-B borderpx]\n"
			"           [-p period] [-A alpha] command [arg ...]");
	}
};
