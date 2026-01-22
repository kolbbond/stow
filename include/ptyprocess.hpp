// pseudo terminal process

#pragma once

//#include <iostream>
#include <csignal>
#include <cstdio>
#include <memory>
//#include <errno.h>
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

//#include "config.h"
#include "process.hpp"
#include "xwindow.hpp"

typedef std::shared_ptr<class PTYProcess> ShPTYProcessPr;
class PTYProcess: public Process {
public:
	// parent/child file descriptors
	int _parentfd;
	int _childfd;
	char _childname[100];

	struct ScreenBuffer {
		struct Cell {
			char ch;
			unsigned int fg;
		};
		std::vector<std::vector<Cell>> lines;
		size_t cursor_row = 0;
		size_t cursor_col = 0;
		bool in_esc = false;
		bool in_csi = false;
		bool in_osc = false;
		std::string csi_params;
		unsigned int current_fg = 0xffffff;
		bool bright = false;
	};
	ScreenBuffer _screen;
	bool _child_exited = false;
	bool _eof = false;
	bool _have_status = false;
	int _child_status = 0;
	bool _nonblock = false;

	static std::string sanitize_chunk(const char* data, size_t len) {
		std::string out;
		out.reserve(len);
		for(size_t i = 0; i < len; i++) {
			unsigned char c = static_cast<unsigned char>(data[i]);
			if(c == '\x1b') { // ANSI escape
				if(i + 1 < len && data[i + 1] == '[') {
					i += 2;
					while(i < len) {
						unsigned char cc = static_cast<unsigned char>(data[i]);
						if(cc >= 0x40 && cc <= 0x7e) break;
						i++;
					}
				} else if(i + 1 < len && data[i + 1] == ']') {
					i += 2;
					while(i < len) {
						if(data[i] == '\a') break;
						if(data[i] == '\x1b' && i + 1 < len && data[i + 1] == '\\') {
							i++;
							break;
						}
						i++;
					}
				}
				continue;
			}
			if(c == '\r') {
				out.push_back('\n');
				continue;
			}
			if(c == '\n' || c == '\t' || (c >= 0x20 && c < 0x7f)) {
				out.push_back(static_cast<char>(c));
			}
		}
		return out;
	}

	static unsigned int parse_hex_color(const std::string& s) {
		if(s.size() == 7 && s[0] == '#') {
			unsigned int r = std::strtoul(s.substr(1, 2).c_str(), nullptr, 16);
			unsigned int g = std::strtoul(s.substr(3, 2).c_str(), nullptr, 16);
			unsigned int b = std::strtoul(s.substr(5, 2).c_str(), nullptr, 16);
			return (r << 16) | (g << 8) | b;
		}
		return 0xffffff;
	}

	static unsigned int default_fg() {
		return parse_hex_color(gconf.colors[0]);
	}

	static unsigned int ansi_color_rgb(int idx, bool bright) {
		static const unsigned int base[8] = {
			0x000000, 0x800000, 0x008000, 0x808000,
			0x000080, 0x800080, 0x008080, 0xc0c0c0
		};
		static const unsigned int bright_base[8] = {
			0x808080, 0xff0000, 0x00ff00, 0xffff00,
			0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
		};
		if(idx < 0 || idx > 7) return 0xffffff;
		return bright ? bright_base[idx] : base[idx];
	}

	static unsigned int ansi_256_color(int idx) {
		if(idx < 16) {
			bool bright = idx >= 8;
			return ansi_color_rgb(idx % 8, bright);
		}
		if(idx >= 16 && idx <= 231) {
			int n = idx - 16;
			int r = n / 36;
			int g = (n / 6) % 6;
			int b = n % 6;
			int levels[6] = {0, 95, 135, 175, 215, 255};
			return (levels[r] << 16) | (levels[g] << 8) | levels[b];
		}
		if(idx >= 232 && idx <= 255) {
			int gray = 8 + (idx - 232) * 10;
			return (gray << 16) | (gray << 8) | gray;
		}
		return 0xffffff;
	}

	static std::vector<int> parse_params(const std::string& s) {
		std::vector<int> params;
		int value = 0;
		bool have = false;
		for(char ch : s) {
			if(ch == '?') continue;
			if(ch == ';') {
				params.push_back(have ? value : 0);
				value = 0;
				have = false;
			} else if(ch >= '0' && ch <= '9') {
				value = value * 10 + (ch - '0');
				have = true;
			}
		}
		if(have || s.find(';') != std::string::npos) {
			params.push_back(have ? value : 0);
		}
		return params;
	}

	static void clear_from_cursor(ScreenBuffer& sb) {
		if(sb.cursor_row >= sb.lines.size()) return;
		std::vector<ScreenBuffer::Cell>& line = sb.lines[sb.cursor_row];
		if(sb.cursor_col < line.size()) {
			line.erase(line.begin() + sb.cursor_col, line.end());
		}
		for(size_t i = sb.cursor_row + 1; i < sb.lines.size(); i++) {
			sb.lines[i].clear();
		}
	}

	static void clear_line(ScreenBuffer& sb, int mode) {
		if(sb.cursor_row >= sb.lines.size()) return;
		std::vector<ScreenBuffer::Cell>& line = sb.lines[sb.cursor_row];
		if(mode == 2) {
			line.clear();
			return;
		}
		if(mode == 1) {
			if(sb.cursor_col >= line.size()) return;
			line.erase(line.begin(), line.begin() + sb.cursor_col);
			return;
		}
		if(sb.cursor_col < line.size()) {
			line.erase(line.begin() + sb.cursor_col, line.end());
		}
	}

	static void ensure_row(ScreenBuffer& sb, size_t row, size_t max_lines) {
		if(row < sb.lines.size()) return;
		if(sb.lines.size() < max_lines) {
			sb.lines.resize(row + 1);
			return;
		}
		while(row >= max_lines) {
			sb.lines.erase(sb.lines.begin());
			if(sb.cursor_row > 0) sb.cursor_row--;
			row--;
		}
		if(row >= sb.lines.size()) {
			sb.lines.resize(row + 1);
		}
	}

	static void ensure_col(ScreenBuffer& sb, size_t max_lines) {
		ensure_row(sb, sb.cursor_row, max_lines);
		std::vector<ScreenBuffer::Cell>& line = sb.lines[sb.cursor_row];
		if(sb.cursor_col > line.size()) {
			line.resize(sb.cursor_col, ScreenBuffer::Cell{' ', sb.current_fg});
		}
	}

	static void handle_csi(ScreenBuffer& sb, char final, const std::vector<int>& params, size_t max_lines) {
		if(final == 'm') {
			if(params.empty()) {
				sb.current_fg = default_fg();
				sb.bright = false;
				return;
			}
			for(size_t i = 0; i < params.size(); i++) {
				int p = params[i];
				if(p == 0) {
					sb.current_fg = default_fg();
					sb.bright = false;
				} else if(p == 1) {
					sb.bright = true;
				} else if(p == 22) {
					sb.bright = false;
				} else if(p == 39) {
					sb.current_fg = default_fg();
				} else if(p >= 30 && p <= 37) {
					sb.current_fg = ansi_color_rgb(p - 30, sb.bright);
				} else if(p >= 90 && p <= 97) {
					sb.current_fg = ansi_color_rgb(p - 90, true);
				} else if(p == 38) {
					if(i + 1 < params.size() && params[i + 1] == 2 && i + 4 < params.size()) {
						int r = params[i + 2];
						int g = params[i + 3];
						int b = params[i + 4];
						sb.current_fg = ((r & 0xff) << 16) | ((g & 0xff) << 8) | (b & 0xff);
						i += 4;
					} else if(i + 1 < params.size() && params[i + 1] == 5 && i + 2 < params.size()) {
						int idx = params[i + 2];
						sb.current_fg = ansi_256_color(idx);
						i += 2;
					}
				}
			}
			return;
		}
		if(final == 'H' || final == 'f') {
			int row = params.size() > 0 && params[0] > 0 ? params[0] - 1 : 0;
			int col = params.size() > 1 && params[1] > 0 ? params[1] - 1 : 0;
			sb.cursor_row = row < 0 ? 0 : static_cast<size_t>(row);
			sb.cursor_col = col < 0 ? 0 : static_cast<size_t>(col);
			ensure_row(sb, sb.cursor_row, max_lines);
			return;
		}
		if(final == 'A' || final == 'B' || final == 'C' || final == 'D') {
			int n = params.size() > 0 && params[0] > 0 ? params[0] : 1;
			if(final == 'A') {
				sb.cursor_row = sb.cursor_row > static_cast<size_t>(n) ? sb.cursor_row - n : 0;
			} else if(final == 'B') {
				sb.cursor_row += n;
			} else if(final == 'C') {
				sb.cursor_col += n;
			} else if(final == 'D') {
				sb.cursor_col = sb.cursor_col > static_cast<size_t>(n) ? sb.cursor_col - n : 0;
			}
			ensure_row(sb, sb.cursor_row, max_lines);
			return;
		}
		if(final == 'J') {
			int mode = params.size() > 0 ? params[0] : 0;
			if(mode == 2) {
				sb.lines.clear();
				sb.cursor_row = 0;
				sb.cursor_col = 0;
			} else {
				clear_from_cursor(sb);
			}
			return;
		}
		if(final == 'K') {
			int mode = params.size() > 0 ? params[0] : 0;
			clear_line(sb, mode);
			return;
		}
		if(final == 'h' || final == 'l') {
			for(int p : params) {
				if(p == 1049) {
					sb.lines.clear();
					sb.cursor_row = 0;
					sb.cursor_col = 0;
				}
			}
		}
	}

	static void append_screen(ScreenBuffer& sb, const char* data, size_t len, size_t max_lines) {
		for(size_t i = 0; i < len; i++) {
			unsigned char c = static_cast<unsigned char>(data[i]);
			if(sb.in_osc) {
				if(c == '\a') {
					sb.in_osc = false;
				} else if(c == '\x1b') {
					sb.in_osc = false;
					sb.in_esc = true;
				}
				continue;
			}
			if(sb.in_csi) {
				if(c >= 0x40 && c <= 0x7e) {
					std::vector<int> params = parse_params(sb.csi_params);
					handle_csi(sb, static_cast<char>(c), params, max_lines);
					sb.in_csi = false;
					sb.csi_params.clear();
				} else {
					sb.csi_params.push_back(static_cast<char>(c));
				}
				continue;
			}
			if(sb.in_esc) {
				if(c == '[') {
					sb.in_csi = true;
					sb.csi_params.clear();
				} else if(c == ']') {
					sb.in_osc = true;
				}
				sb.in_esc = false;
				continue;
			}

			if(c == '\x1b') {
				sb.in_esc = true;
				continue;
			}
			if(c == '\r') {
				sb.cursor_col = 0;
				continue;
			}
			if(c == '\n') {
				sb.cursor_row++;
				sb.cursor_col = 0;
				ensure_row(sb, sb.cursor_row, max_lines);
				continue;
			}
			if(c == '\b') {
				if(sb.cursor_col > 0) sb.cursor_col--;
				continue;
			}
			if(c == '\t') {
				size_t next = ((sb.cursor_col / 4) + 1) * 4;
				while(sb.cursor_col < next) {
					ensure_col(sb, max_lines);
					std::vector<ScreenBuffer::Cell>& line = sb.lines[sb.cursor_row];
					if(sb.cursor_col < line.size()) {
						line[sb.cursor_col] = ScreenBuffer::Cell{' ', sb.current_fg};
					} else {
						line.push_back(ScreenBuffer::Cell{' ', sb.current_fg});
					}
					sb.cursor_col++;
				}
				continue;
			}
			if(c >= 0x20 && c < 0x7f) {
				ensure_col(sb, max_lines);
				std::vector<ScreenBuffer::Cell>& line = sb.lines[sb.cursor_row];
				if(sb.cursor_col < line.size()) {
					line[sb.cursor_col] = ScreenBuffer::Cell{static_cast<char>(c), sb.current_fg};
				} else {
					line.push_back(ScreenBuffer::Cell{static_cast<char>(c), sb.current_fg});
				}
				sb.cursor_col++;
			}
		}
	}

	static std::string compose_screen(const ScreenBuffer& sb, size_t max_chars) {
		std::string out;
		for(size_t i = 0; i < sb.lines.size(); i++) {
			for(size_t j = 0; j < sb.lines[i].size(); j++) {
				out.push_back(sb.lines[i][j].ch);
				if(out.size() > max_chars) break;
			}
			out.push_back('\n');
			if(out.size() > max_chars) break;
		}
		if(out.size() > max_chars) {
			out.erase(0, out.size() - max_chars);
		}
		return out;
	}

	static std::vector<std::vector<ColorSpan>> compose_screen_spans(const ScreenBuffer& sb, size_t max_chars) {
		std::vector<std::vector<ColorSpan>> out;
		size_t total = 0;
		for(size_t i = 0; i < sb.lines.size(); i++) {
			const std::vector<ScreenBuffer::Cell>& line = sb.lines[i];
			std::vector<ColorSpan> spans;
			ColorSpan cur;
			cur.rgb = 0;
			for(size_t j = 0; j < line.size(); j++) {
				if(total >= max_chars) break;
				const ScreenBuffer::Cell& cell = line[j];
				if(cur.text.empty()) {
					cur.rgb = cell.fg;
					cur.text.push_back(cell.ch);
				} else if(cur.rgb == cell.fg) {
					cur.text.push_back(cell.ch);
				} else {
					spans.push_back(cur);
					cur.text.clear();
					cur.rgb = cell.fg;
					cur.text.push_back(cell.ch);
				}
				total++;
			}
			if(!cur.text.empty()) spans.push_back(cur);
			out.push_back(spans);
			total++; // newline
			if(total >= max_chars) break;
		}
		return out;
	}

	// methods
	PTYProcess() {};
	~PTYProcess() {};
	static ShPTYProcessPr create() {
		return std::make_shared<class PTYProcess>();
	}

	int fd() const {
		return _parentfd;
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
		_screen = ScreenBuffer{};
		_screen.current_fg = default_fg();
		_child_exited = false;
		_eof = false;
		_have_status = false;
		_child_status = 0;
		_nonblock = false;

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
			std::string pname_str = "stow pty: " + cmd;
			char* pname = strdup(pname_str.c_str());

			// parse args and copy into cstr
			//char* const cargs[] = { (char*)pname, nullptr, nullptr };
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
			// weird output
			break;
		}
		}

		// parent process
		close(_childfd);

		std::printf("child spawned with cmd pid: %i\n", _cmdpid);
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

	bool pump(ShXWindowPr xwin = NULL) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		set_nonblocking();

		while(true) {
			char text[256];
			ssize_t bytes_read = read(_parentfd, text, sizeof(text));
			if(bytes_read > 0) {
				if(xwin != NULL) {
					append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
					drew = true;
				} else {
					std::string clean = sanitize_chunk(text, static_cast<size_t>(bytes_read));
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

		if(drew && xwin != NULL) {
			std::vector<std::vector<ColorSpan>> spans = compose_screen_spans(_screen, kMaxBuffer);
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
				append_screen(_screen, text, static_cast<size_t>(bytes_read), kMaxLines);
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

		if(drew && xwin != NULL) {
			std::vector<std::vector<ColorSpan>> spans = compose_screen_spans(_screen, kMaxBuffer);
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

	// read output from file pipe
	void read_text(ShXWindowPr xwin = NULL) override {
		dprintf("read_text\n");

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
