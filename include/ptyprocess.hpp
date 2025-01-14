// pseudo terminal process

#pragma once

//#include <iostream>
#include <csignal>
#include <cstdio>
#include <memory>
//#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>
#include <cstring>
#include <vector>
#include <utmp.h>

//#include "config.h"
#include "process.hpp"

typedef std::shared_ptr<class PTYProcess> ShPTYProcessPr;
class PTYProcess: public Process {
public:
	// parent/child file descriptors
	int _parentfd;
	int _childfd;
	char _childname[100];

	// methods
	PTYProcess() {};
	~PTYProcess() {};
	static ShPTYProcessPr create() {
		return std::make_shared<class PTYProcess>();
	}

	void setup() override {
		// self pipe and signal handler

		// start pty
		//if(pipe(_spipe) == -1) die("pipe:");
		if(openpty(&_parentfd, &_childfd, _childname, nullptr, nullptr) == -1) {
			die("openpty");
		}
	}

	// start command
	void start_cmd(std::string cmd) override {
		start_cmd(cmd, {});
	};
	void start_cmd(std::string cmd, std::vector<std::string> args) override {
		// this code uses a lot of globals...
		// what is pipefd?

		// create a fork
		_cmdpid = fork();
		switch(_cmdpid) {
		// bad fork?
		case -1: {
			printf("bad fork\n");
			die("fork:");
		}
		case 0: { // child process
			// new process (0 means success)
			printf("open process\n");

			// close old file descriptors
			close(_parentfd);

			// attach to input/output/error
			if(login_tty(_childfd) == -1) {
				die("login_tty");
			}

			// replace current process with cmd[0] with arguments cmd
			// doesn't error check?
			const char* pname = "stow pty";
			char* const args[] = { (char*)pname, nullptr, nullptr };
			execvp(cmd.c_str(), args);

			// delete args here?


			// exits current process
			exit(1);
		}
		default: {
			// weird output
			break;
		}
		}

		// parent process
		close(_childfd);

		std::printf("child spawned with cmd pid: %i\n", _cmdpid);
	}

	// read output from file pipe
	void read_text() override {
		static char delimeter[] = "\4";
		//int dlen = strlen(gconf.delimeter);
		int dlen = strlen(delimeter);
		static size_t cap;

		dprintf("read_text\n");

		// read from pty
		static char text[256];
		while(true) {

			// read in bytes
			ssize_t bytes_read = read(_parentfd, text, sizeof(text) - 1);

			// if we have bytes
			if(bytes_read > 0) {
				text[bytes_read] = '\0'; // null terminate
				std::printf("%s", text);
				std::flush(std::cout);
			} else if(bytes_read == 0) {
				break; // eof
			} else if(bytes_read == -1) {
				// error!
				die("read");
			}
		}

		// wait for termination?
		int status;
		waitpid(_cmdpid, &status, 0);
		std::cout << "\nChild process terminated with status: " << WEXITSTATUS(status) << std::endl;
	}
};
