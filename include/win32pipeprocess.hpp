// pipe process - Windows implementation
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
#include "process.hpp"

typedef std::shared_ptr<class Win32PipeProcess> ShWin32PipeProcessPr;
class Win32PipeProcess : public Process {
public:
	HANDLE _hChildStdoutRd = nullptr;
	HANDLE _hChildStdoutWr = nullptr;
	HANDLE _hProcess = nullptr;
	HANDLE _hThread = nullptr;
	DWORD _processId = 0;
	bool _process_exited = false;
	DWORD _exit_code = 0;

	Win32PipeProcess() {}
	~Win32PipeProcess() {
		cleanup();
	}

	void cleanup() {
		if(_hChildStdoutRd) {
			CloseHandle(_hChildStdoutRd);
			_hChildStdoutRd = nullptr;
		}
		if(_hChildStdoutWr) {
			CloseHandle(_hChildStdoutWr);
			_hChildStdoutWr = nullptr;
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

	static ShWin32PipeProcessPr create() {
		return std::make_shared<Win32PipeProcess>();
	}

	void setup() override {
		// Create pipe for stdout
		SECURITY_ATTRIBUTES sa;
		sa.nLength = sizeof(SECURITY_ATTRIBUTES);
		sa.bInheritHandle = TRUE;
		sa.lpSecurityDescriptor = nullptr;

		if(!CreatePipe(&_hChildStdoutRd, &_hChildStdoutWr, &sa, 0)) {
			std::cerr << "CreatePipe failed\n";
			return;
		}

		// Ensure the read handle to the pipe for STDOUT is not inherited
		if(!SetHandleInformation(_hChildStdoutRd, HANDLE_FLAG_INHERIT, 0)) {
			std::cerr << "SetHandleInformation failed\n";
			return;
		}
	}

	void start_cmd(std::string cmd) override {
		start_cmd(cmd, {});
	}

	void start_cmd(std::string cmd, std::vector<std::string> args) override {
		_process_exited = false;
		_exit_code = 0;

		// Build command line
		std::string cmdline = cmd;
		for(const auto& arg : args) {
			cmdline += " ";
			// Simple quoting for args with spaces
			if(arg.find(' ') != std::string::npos) {
				cmdline += "\"" + arg + "\"";
			} else {
				cmdline += arg;
			}
		}

		STARTUPINFOA si;
		PROCESS_INFORMATION pi;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		si.hStdError = _hChildStdoutWr;
		si.hStdOutput = _hChildStdoutWr;
		si.dwFlags |= STARTF_USESTDHANDLES;
		ZeroMemory(&pi, sizeof(pi));

		// Create child process
		if(!CreateProcessA(
			nullptr,
			const_cast<char*>(cmdline.c_str()),
			nullptr,
			nullptr,
			TRUE,
			0,
			nullptr,
			nullptr,
			&si,
			&pi)) {
			std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
			return;
		}

		_hProcess = pi.hProcess;
		_hThread = pi.hThread;
		_processId = pi.dwProcessId;

		// Close write end in parent
		CloseHandle(_hChildStdoutWr);
		_hChildStdoutWr = nullptr;

		std::printf("child spawned with process id: %lu\n", _processId);
	}

	bool is_done() const {
		return _process_exited;
	}

	void check_process_status() {
		if(_process_exited || !_hProcess) return;

		DWORD result = WaitForSingleObject(_hProcess, 0);
		if(result == WAIT_OBJECT_0) {
			GetExitCodeProcess(_hProcess, &_exit_code);
			_process_exited = true;
		}
	}

	void read_text(ShWindowPtr win = nullptr) override {
		(void)win; // unused in pipe process

		dprintf("read_text\n");

		char buffer[4096];
		DWORD bytesRead;

		while(true) {
			// Check if process has exited
			check_process_status();

			// Check if data is available
			DWORD bytesAvailable = 0;
			if(!PeekNamedPipe(_hChildStdoutRd, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
				break;
			}

			if(bytesAvailable == 0) {
				if(_process_exited) break;
				Sleep(100);
				continue;
			}

			if(!ReadFile(_hChildStdoutRd, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) || bytesRead == 0) {
				break;
			}

			buffer[bytesRead] = '\0';
			std::printf("%s", buffer);
			std::fflush(stdout);
		}

		if(_process_exited) {
			std::cout << "\nChild process terminated with exit code: " << _exit_code << std::endl;
		}
	}
};

#endif // STOW_WINDOWS
