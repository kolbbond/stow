// process wrapper
#pragma once

#include <iostream>
#include <csignal>
#include <cstdio>
#include <memory>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "config.h"

#define INITIAL_CAPACITY 2
#define dprintf(...) printf(__VA_ARGS__)

typedef std::shared_ptr<class Process> ShProcessPr;
class Process {
public:
	// properties
	// file pointer
	FILE* _inputf;

	// pipe file descriptor
	int _pipefd[2];

	// pipe read/write are [0]/[1]
	int _spipe[2];

	// cmd name and id
	char** _cmd;
	pid_t _cmdpid;

	// methods
	Process() {};
	virtual ~Process() {};
	//	static ShProcessPr create() {
	//		return std::make_shared<class Process>();
	//	}

	__attribute__((noreturn)) static void die(const char* fmt, ...) {
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

	static void signal_handler(int signal) {
		//if(-1 == write(_spipe[1], s == SIGCHLD ? "c" : "a", 1)) abort();
		if(signal == SIGINT) {
			std::cout << "SIGINT received...\n";
		} else if(signal == SIGTERM) {
			std::cout << "SIGINT received...\n";
		} else if(signal == SIGCHLD) {
			std::cout << "SIGINT received...\n";
		}
	}

	static void signal_action(int s, siginfo_t* siptr, void* uct) {}

	virtual void setup() = 0;

	// start command
	virtual void start_cmd(std::string cmd, std::vector<std::string> args) = 0;
	virtual void start_cmd(std::string cmd) = 0;

	// read output from file pipe
	virtual void read_text() = 0;
};
