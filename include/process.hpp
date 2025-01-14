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

	// cmd name and id
	char** _cmd;
	pid_t _cmdpid;

	// pipe read/write are [0]/[1]
	int _spipe[2];

	// methods
	Process() {};
	~Process() {};
	static ShProcessPr create() {
		return std::make_shared<class Process>();
	}

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

	void setup() {
		// self pipe and signal handler

		// start pipe
		if(pipe(_spipe) == -1) die("pipe:");

		// signal handling
		struct sigaction sa = {0};
		sa.sa_handler = signal_handler;
		sa.sa_flags = SA_RESTART;

		//
		if(sigaction(SIGCHLD, &sa, NULL) == -1 || sigaction(SIGALRM, &sa, NULL) == -1) die("sigaction:");
	}

	// start command
	void start_cmd(std::string cmd) {
		// this code uses a lot of globals...
		// what is pipefd?

		// check pipe exists and is good
		if(-1 == pipe(_pipefd)) die("pipe:");

		// read input and store in inputf
		_inputf = fdopen(_pipefd[0], "r");
		if(_inputf == NULL) die("fdopen:");

		// create a fork
		_cmdpid = fork();
		switch(_cmdpid) {
		// bad fork?
		case -1:
			printf("bad fork\n");
			die("fork:");
		case 0: // child process
			// new process (0 means success)
			printf("open process\n");

			// close old file descriptors
			close(_spipe[0]);
			close(_spipe[1]);

			// close x server connection number @hey: need crosstalk
			//close(xfd);

			// close newest (read pipe)
			close(_pipefd[0]);

			// copy write pipe to stdout_fileno
			dup2(_pipefd[1], STDOUT_FILENO);

			// set process group ID of current process to its process ID hence: (0,0)
			setpgid(0, 0);

			// replace current process with cmd[0] with arguments cmd
			// doesn't error check?
			// @hey: add args
			execvp(cmd.c_str(), _cmd);

			// exits current process
			exit(1);

		default:
			// weird output
			break;
		}

		// close file descriptor (write)
		close(_pipefd[1]);
	}

	// read output from file pipe
	void read_text() {
		static char delimeter[] = "\4";
		//int dlen = strlen(gconf.delimeter);
		int dlen = strlen(delimeter);
		static char* text;
		static size_t cap;

		dprintf("read_text\n");

        // read from pipe
		int len = 0;
		for(;;) {
			if(len + dlen + 2 > cap) {
				// buffer must have sufficient capacity to
				// store delimeter string, \n and \0 in one read
				cap = cap ? cap * 2 : INITIAL_CAPACITY;
				text = static_cast<char*>(realloc(text, cap));
				if(text == NULL) die("realloc:");
			}

			// read line from file
			char* line = &text[len];
			if(NULL == fgets(line, cap - len, _inputf)) {
				if(feof(_inputf)) {
					if(fclose(_inputf) == -1) die("fclose subcommand output:");
					_inputf = NULL;
					break;
				} else {
					die("fgets subcommand output:");
				}
				dprintf("%s", line);
			}


			// recalculate length
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
};
