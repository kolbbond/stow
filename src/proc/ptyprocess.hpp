// pseudo terminal process - POSIX implementation
#pragma once

#include "platform.hpp"

#if STOW_POSIX

#include <csignal>
#include <cstdio>
#include <memory>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/wait.h>
#include <string>
#include <vector>
#include <utmp.h>
#include <algorithm>
#include <iostream>

#include "proc/process.hpp"
#include "proc/screen_buffer.hpp"
#include "x11/window.hpp"
#include "stow/overlay.hpp"

typedef std::shared_ptr<class PTYProcess> ShPTYProcessPr;
class PTYProcess: public Process {
public:
	// parent/child file descriptors
	int _parentfd;
	int _childfd;
	char _childname[100];

	ScreenBuffer _screen;
	bool _child_exited = false;
	bool _eof = false;
	bool _have_status = false;
	int _child_status = 0;
	bool _nonblock = false;

	// methods
	PTYProcess() {};
	~PTYProcess() {};
	static ShPTYProcessPr create() {
		return std::make_shared<class PTYProcess>();
	}

	static ShPTYProcessPr create(const stow::ProcessConfig& config) {
		auto proc = std::make_shared<class PTYProcess>();
		proc->set_config(config);
		return proc;
	}

	int fd() const {
		return _parentfd;
	}

	void set_default_fg(const std::string& color) {
		gconf.colors[0] = color;
		_screen.current_fg = ScreenBufferUtils::parse_hex_color(color);
	}

	void setup() override {
		// start pty
		if(openpty(&_parentfd, &_childfd, _childname, nullptr, nullptr) == -1) {
			die("openpty");
		}
	}

	// start command
	void start_cmd(std::string cmd) override {
		start_cmd(cmd, {});
	};

	void start_cmd(std::string cmd, std::vector<std::string> args) override {
		_screen = ScreenBuffer{};
		_screen.current_fg = ScreenBufferUtils::default_fg();
		_child_exited = false;
		_eof = false;
		_have_status = false;
		_child_status = 0;
		_nonblock = false;

		// create a fork
		_cmdpid = fork();
		switch(_cmdpid) {
		// bad fork?
		case -1: {
			printf("bad fork\n");
			die("fork:");
		}
		case 0: { // child process
			// close old file descriptors
			close(_parentfd);

			// attach to input/output/error
			if(login_tty(_childfd) == -1) {
				die("login_tty");
			}

			// replace current process with cmd[0] with arguments cmd
			std::string pname_str = "stow pty: " + cmd;
			char* pname = strdup(pname_str.c_str());

			// parse args and copy into cstr
			std::vector<char*> cargs = {pname};
			std::transform(args.begin(), args.end(), std::back_inserter(cargs), [&](const std::string& s) {
				return strdup(s.c_str());
			});
			cargs.push_back(nullptr);
			execvp(cmd.c_str(), cargs.data());

			// delete args here?
			for(size_t i = 0; i < cargs.size(); i++) {
				if(cargs[i]) free(cargs[i]);
			}

			// exits current process
			perror("execvp");
			_exit(1);
		}
		default: {
			break;
		}
		}

		// parent process
		close(_childfd);

	}

	void set_nonblocking() {
		if(_nonblock) return;
		int flags = fcntl(_parentfd, F_GETFL, 0);
		if(flags != -1) {
			fcntl(_parentfd, F_SETFL, flags | O_NONBLOCK);
			_nonblock = true;
		}
	}

	bool is_done() const {
		return _child_exited && _eof;
	}

	bool pump(ShXWindowPr xwin = nullptr) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		set_nonblocking();

		while(true) {
			char text[256];
			ssize_t bytes_read = read(_parentfd, text, sizeof(text));
			if(bytes_read > 0) {
				if(xwin != nullptr) {
					ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
					drew = true;
				} else {
					std::string clean = ScreenBufferUtils::sanitize_chunk(text, static_cast<size_t>(bytes_read));
					std::printf("%s", clean.c_str());
					std::fflush(stdout);
				}
			} else if(bytes_read == 0) {
				_eof = true;
				break;
			} else {
				if(errno == EAGAIN || errno == EWOULDBLOCK) break;
				if(errno == EINTR) continue;
				if(errno == EIO) {
					_eof = true;
					break;
				}
				die("read");
			}
		}

		if(drew && xwin != nullptr) {
			std::vector<std::vector<ColorSpan>> spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			xwin->draw_spans(spans);
			xwin->run();
		}

		if(!_child_exited) {
			pid_t wp = waitpid(_cmdpid, &_child_status, WNOHANG);
			if(wp == _cmdpid) {
				_child_exited = true;
				_have_status = true;
			}
		}

		return !is_done();
	}

	bool pump_region(ShXWindowPr xwin, int rx, int ry, unsigned int rw, unsigned int rh) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		set_nonblocking();

		while(true) {
			char text[256];
			ssize_t bytes_read = read(_parentfd, text, sizeof(text));
			if(bytes_read > 0) {
				ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
				drew = true;
			} else if(bytes_read == 0) {
				_eof = true;
				break;
			} else {
				if(errno == EAGAIN || errno == EWOULDBLOCK) break;
				if(errno == EINTR) continue;
				if(errno == EIO) {
					_eof = true;
					break;
				}
				die("read");
			}
		}

		if(drew && xwin != nullptr) {
			std::vector<std::vector<ColorSpan>> spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			xwin->draw_region_spans(spans, rx, ry, rw, rh);
		}

		if(!_child_exited) {
			pid_t wp = waitpid(_cmdpid, &_child_status, WNOHANG);
			if(wp == _cmdpid) {
				_child_exited = true;
				_have_status = true;
			}
		}

		return !is_done();
	}

	// Same as pump_region above, but drawing through the public overlay API.
	// This is what dashboard widgets use; the ShXWindowPr overload remains for
	// the not-yet-ported stow binary.
	bool pump_region(stow::Overlay& ov, stow::Rect region) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		set_nonblocking();

		while(true) {
			char text[256];
			ssize_t bytes_read = read(_parentfd, text, sizeof(text));
			if(bytes_read > 0) {
				ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
				drew = true;
			} else if(bytes_read == 0) {
				_eof = true;
				break;
			} else {
				if(errno == EAGAIN || errno == EWOULDBLOCK) break;
				if(errno == EINTR) continue;
				if(errno == EIO) {
					_eof = true;
					break;
				}
				die("read");
			}
		}

		if(drew) {
			stow::Lines spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			ov.spans_in(region, spans);
		}

		if(!_child_exited) {
			pid_t wp = waitpid(_cmdpid, &_child_status, WNOHANG);
			if(wp == _cmdpid) {
				_child_exited = true;
				_have_status = true;
			}
		}

		return !is_done();
	}

	// Same as pump() above, drawing a whole frame through the public overlay
	// API instead of an XWindow.
	bool pump(stow::Overlay& ov) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		set_nonblocking();

		while(true) {
			char text[256];
			ssize_t bytes_read = read(_parentfd, text, sizeof(text));
			if(bytes_read > 0) {
				ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
				drew = true;
			} else if(bytes_read == 0) {
				_eof = true;
				break;
			} else {
				if(errno == EAGAIN || errno == EWOULDBLOCK) break;
				if(errno == EINTR) continue;
				if(errno == EIO) {
					_eof = true;
					break;
				}
				die("read");
			}
		}

		if(drew) {
			stow::Lines spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			ov.set_spans(spans);
			ov.pump();
		}

		if(!_child_exited) {
			pid_t wp = waitpid(_cmdpid, &_child_status, WNOHANG);
			if(wp == _cmdpid) {
				_child_exited = true;
				_have_status = true;
			}
		}

		return !is_done();
	}

	// Stream this process's output into `ov` until the child exits.
	void read_text(stow::Overlay& ov) {
		while(!is_done()) {
			struct pollfd pfd;
			pfd.fd = _parentfd;
			pfd.events = POLLIN | POLLHUP | POLLERR;
			pfd.revents = 0;

			int pr = poll(&pfd, 1, 100);
			if(pr == -1) {
				if(errno == EINTR) continue;
				die("poll");
			}
			pump(ov);
		}
	}

	// read output from file pipe - accepts abstract window pointer
	void read_text(ShWindowPtr win = nullptr) override {
		dprintf("read_text\n");

		// Cast to XWindow for X11-specific features
		ShXWindowPr xwin = std::dynamic_pointer_cast<XWindow>(win);

		while(!is_done()) {
			struct pollfd pfd;
			pfd.fd = _parentfd;
			pfd.events = POLLIN | POLLHUP | POLLERR;
			pfd.revents = 0;

			int pr = poll(&pfd, 1, 100);
			if(pr == -1) {
				if(errno == EINTR) continue;
				die("poll");
			}
			if(pr > 0 || pr == 0) {
				pump(xwin);
			}
		}

		if(_have_status) {
			std::cout << "\nChild process terminated with status: " << WEXITSTATUS(_child_status) << std::endl;
		}
	}
};

#endif // STOW_POSIX
