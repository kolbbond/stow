// Run a shell command, return the last non-empty stdout line (trimmed).
// Used by value widgets (Number, Bar). Never throws.
#pragma once

#include <array>
#include <cstdio>
#include <string>

namespace stow {

inline std::string trim_ws(const std::string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if(start == std::string::npos) return "";
	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

// Runs `cmd` via /bin/sh -c, captures stdout, returns the last non-empty line trimmed.
inline std::string capture_command(const std::string& cmd) {
	std::FILE* pipe = ::popen(cmd.c_str(), "r");
	if(!pipe) return "";
	std::string out;
	std::array<char, 4096> buf;
	while(std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
		out += buf.data();
	}
	::pclose(pipe);

	// last non-empty line
	std::string last;
	size_t pos = 0;
	while(pos <= out.size()) {
		size_t nl = out.find('\n', pos);
		std::string line = (nl == std::string::npos) ? out.substr(pos) : out.substr(pos, nl - pos);
		std::string t = trim_ws(line);
		if(!t.empty()) last = t;
		if(nl == std::string::npos) break;
		pos = nl + 1;
	}
	return last;
}

}  // namespace stow
