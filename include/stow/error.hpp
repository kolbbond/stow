// stow::Error - failure reason from library construction. No X11.
#pragma once

#include <string>

namespace stow {

struct Error {
	// NB: not "None" - X11 defines `None` as a macro, so a consumer that
	// includes X11 before this header would fail to compile.
	enum class Code {
		Ok = 0,
		NoDisplay,   // could not open an X display; the one true failure
		NoFont,      // requested font could not be loaded
		NoColor,     // a color could not be allocated
		BadConfig,   // a config file failed to parse or validate
	};

	Code code = Code::Ok;
	std::string message;

	explicit operator bool() const { return code != Code::Ok; }
};

}  // namespace stow
