// process wrapper
#pragma once

#include "platform.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if STOW_POSIX
#include <csignal>
#include <cstdio>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#endif

#include "config.h"

// Forward declare window types
class StowWindow;
using ShWindowPtr = std::shared_ptr<StowWindow>;

// Legacy typedef for backwards compatibility
class XWindow;
using ShXWindowPr = std::shared_ptr<XWindow>;

#define INITIAL_CAPACITY 2
#define dprintf(...) printf(__VA_ARGS__)

typedef std::shared_ptr<class Process> ShProcessPr;
class Process {
public:
	// properties
	// file pointer
	FILE* _inputf = nullptr;

	// pipe file descriptor
	int _pipefd[2] = {0, 0};

	// pipe read/write are [0]/[1]
	int _spipe[2] = {0, 0};

	// cmd name and id
	char** _cmd = nullptr;
#if STOW_POSIX
	pid_t _cmdpid = 0;
#else
	int _cmdpid = 0;
#endif

	// methods
	Process() {};
	virtual ~Process() {};

	[[noreturn]] static void die(const char* fmt, ...) {
#if STOW_POSIX
		int tmp = errno;
#else
		int tmp = 0;
#endif
		va_list ap;

		va_start(ap, fmt);
		(void)vfprintf(stderr, fmt, ap);
		va_end(ap);

		if(fmt[0] && fmt[strlen(fmt) - 1] == ':') {
			(void)fputc(' ', stderr);
#if STOW_POSIX
			errno = tmp;
			perror(NULL);
#endif
		} else {
			(void)fputc('\n', stderr);
		}

		exit(1);
	}

#if STOW_POSIX
	static void signal_handler(int signal) {
		if(signal == SIGINT) {
			std::cout << "SIGINT received...\n";
		} else if(signal == SIGTERM) {
			std::cout << "SIGTERM received...\n";
		} else if(signal == SIGCHLD) {
			std::cout << "SIGCHLD received...\n";
		}
	}

	static void signal_action(int s, siginfo_t* siptr, void* uct) {
		(void)s;
		(void)siptr;
		(void)uct;
	}
#endif

	virtual void setup() = 0;

	// start command
	virtual void start_cmd(std::string cmd, std::vector<std::string> args) = 0;
	virtual void start_cmd(std::string cmd) = 0;

	// read output from file pipe - accepts abstract window pointer
	virtual void read_text(ShWindowPtr win = nullptr) = 0;
};
