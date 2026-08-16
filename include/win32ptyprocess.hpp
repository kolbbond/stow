// pseudo terminal process - Windows ConPTY implementation
// Requires Windows 10 version 1809 or later
#pragma once

#include "platform.hpp"

#if STOW_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <iostream>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "proc/process.hpp"
#include "proc/screen_buffer.hpp"
#include "win32window.hpp"

typedef std::shared_ptr<class Win32PTYProcess> ShWin32PTYProcessPr;
class Win32PTYProcess : public Process {
public:
	HPCON _hPC = nullptr;
	HANDLE _hPipeIn = nullptr;   // Pipe for reading from PTY
	HANDLE _hPipeOut = nullptr;  // Pipe for writing to PTY
	HANDLE _hProcess = nullptr;
	HANDLE _hThread = nullptr;
	DWORD _processId = 0;

	ScreenBuffer _screen;
	bool _child_exited = false;
	bool _eof = false;
	bool _have_status = false;
	DWORD _exit_code = 0;

	Win32PTYProcess() {}
	~Win32PTYProcess() {
		cleanup();
	}

	void cleanup() {
		if(_hPC) {
			ClosePseudoConsole(_hPC);
			_hPC = nullptr;
		}
		if(_hPipeIn) {
			CloseHandle(_hPipeIn);
			_hPipeIn = nullptr;
		}
		if(_hPipeOut) {
			CloseHandle(_hPipeOut);
			_hPipeOut = nullptr;
		}
		if(_hThread) {
			CloseHandle(_hThread);
			_hThread = nullptr;
		}
		if(_hProcess) {
			CloseHandle(_hProcess);
			_hProcess = nullptr;
		}
	}

	static ShWin32PTYProcessPr create() {
		return std::make_shared<Win32PTYProcess>();
	}

	int fd() const {
		return (int)(intptr_t)_hPipeIn;
	}

	void setup() override {
		// Create pipes for PTY
		HANDLE hPipePTYIn = nullptr;
		HANDLE hPipePTYOut = nullptr;

		// Pipe for PTY input (our output -> PTY input)
		if(!CreatePipe(&hPipePTYIn, &_hPipeOut, nullptr, 0)) {
			std::cerr << "CreatePipe for PTY input failed\n";
			return;
		}

		// Pipe for PTY output (PTY output -> our input)
		if(!CreatePipe(&_hPipeIn, &hPipePTYOut, nullptr, 0)) {
			std::cerr << "CreatePipe for PTY output failed\n";
			CloseHandle(hPipePTYIn);
			CloseHandle(_hPipeOut);
			_hPipeOut = nullptr;
			return;
		}

		// Create pseudo console
		COORD size = {80, 25};
		HRESULT hr = CreatePseudoConsole(size, hPipePTYIn, hPipePTYOut, 0, &_hPC);
		if(FAILED(hr)) {
			std::cerr << "CreatePseudoConsole failed: " << hr << "\n";
			CloseHandle(hPipePTYIn);
			CloseHandle(hPipePTYOut);
			CloseHandle(_hPipeIn);
			CloseHandle(_hPipeOut);
			_hPipeIn = nullptr;
			_hPipeOut = nullptr;
			return;
		}

		// Close handles that are now owned by the pseudo console
		CloseHandle(hPipePTYIn);
		CloseHandle(hPipePTYOut);
	}

	void start_cmd(std::string cmd) override {
		start_cmd(cmd, {});
	}

	void start_cmd(std::string cmd, std::vector<std::string> args) override {
		_screen = ScreenBuffer{};
		_screen.current_fg = ScreenBufferUtils::default_fg();
		_child_exited = false;
		_eof = false;
		_have_status = false;
		_exit_code = 0;

		if(!_hPC) {
			std::cerr << "Pseudo console not initialized\n";
			return;
		}

		// Build command line
		std::string cmdline = cmd;
		for(const auto& arg : args) {
			cmdline += " ";
			if(arg.find(' ') != std::string::npos) {
				cmdline += "\"" + arg + "\"";
			} else {
				cmdline += arg;
			}
		}

		// Initialize thread attribute list for pseudo console
		SIZE_T attrListSize = 0;
		InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

		std::vector<BYTE> attrListBuffer(attrListSize);
		LPPROC_THREAD_ATTRIBUTE_LIST attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());

		if(!InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize)) {
			std::cerr << "InitializeProcThreadAttributeList failed\n";
			return;
		}

		if(!UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
			_hPC, sizeof(HPCON), nullptr, nullptr)) {
			std::cerr << "UpdateProcThreadAttribute failed\n";
			DeleteProcThreadAttributeList(attrList);
			return;
		}

		STARTUPINFOEXA si;
		ZeroMemory(&si, sizeof(si));
		si.StartupInfo.cb = sizeof(STARTUPINFOEXA);
		si.lpAttributeList = attrList;

		PROCESS_INFORMATION pi;
		ZeroMemory(&pi, sizeof(pi));

		if(!CreateProcessA(
			nullptr,
			const_cast<char*>(cmdline.c_str()),
			nullptr,
			nullptr,
			FALSE,
			EXTENDED_STARTUPINFO_PRESENT,
			nullptr,
			nullptr,
			&si.StartupInfo,
			&pi)) {
			std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
			DeleteProcThreadAttributeList(attrList);
			return;
		}

		DeleteProcThreadAttributeList(attrList);

		_hProcess = pi.hProcess;
		_hThread = pi.hThread;
		_processId = pi.dwProcessId;

		std::printf("child spawned with process id: %lu\n", _processId);
	}

	void set_nonblocking() {
		// Windows pipes don't have a direct non-blocking mode like POSIX
		// We use PeekNamedPipe to check for data availability
	}

	bool is_done() const {
		return _child_exited && _eof;
	}

	void check_process_status() {
		if(_child_exited || !_hProcess) return;

		DWORD result = WaitForSingleObject(_hProcess, 0);
		if(result == WAIT_OBJECT_0) {
			GetExitCodeProcess(_hProcess, &_exit_code);
			_child_exited = true;
			_have_status = true;
		}
	}

	bool pump(ShWindowPtr win = nullptr) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		while(true) {
			// Check if data is available
			DWORD bytesAvailable = 0;
			if(!PeekNamedPipe(_hPipeIn, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
				_eof = true;
				break;
			}

			if(bytesAvailable == 0) {
				break;
			}

			char text[256];
			DWORD bytesRead;
			if(!ReadFile(_hPipeIn, text, sizeof(text), &bytesRead, nullptr) || bytesRead == 0) {
				_eof = true;
				break;
			}

			if(win != nullptr) {
				ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytesRead), kMaxLines);
				drew = true;
			} else {
				std::string clean = ScreenBufferUtils::sanitize_chunk(text, static_cast<size_t>(bytesRead));
				std::printf("%s", clean.c_str());
				std::fflush(stdout);
			}
		}

		if(drew && win != nullptr) {
			std::vector<std::vector<ColorSpan>> spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			win->draw_spans(spans);
			win->run();
		}

		check_process_status();

		return !is_done();
	}

	bool pump_region(ShWindowPtr win, int rx, int ry, unsigned int rw, unsigned int rh) {
		const size_t kMaxBuffer = 16384;
		const size_t kMaxLines = 200;
		bool drew = false;

		while(true) {
			DWORD bytesAvailable = 0;
			if(!PeekNamedPipe(_hPipeIn, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
				_eof = true;
				break;
			}

			if(bytesAvailable == 0) {
				break;
			}

			char text[256];
			DWORD bytesRead;
			if(!ReadFile(_hPipeIn, text, sizeof(text), &bytesRead, nullptr) || bytesRead == 0) {
				_eof = true;
				break;
			}

			ScreenBufferUtils::append_screen(_screen, text, static_cast<size_t>(bytesRead), kMaxLines);
			drew = true;
		}

		if(drew && win != nullptr) {
			std::vector<std::vector<ColorSpan>> spans = ScreenBufferUtils::compose_screen_spans(_screen, kMaxBuffer);
			win->draw_region_spans(spans, rx, ry, rw, rh);
		}

		check_process_status();

		return !is_done();
	}

	void read_text(ShWindowPtr win = nullptr) override {
		dprintf("read_text\n");

		while(!is_done()) {
			// Wait for data or process exit with timeout
			HANDLE handles[2] = {_hPipeIn, _hProcess};
			int handleCount = _hProcess ? 2 : 1;

			DWORD result = WaitForMultipleObjects(handleCount, handles, FALSE, 100);
			(void)result;

			pump(win);
		}

		if(_have_status) {
			std::cout << "\nChild process terminated with exit code: " << _exit_code << std::endl;
		}
	}
};

#endif // STOW_WINDOWS
