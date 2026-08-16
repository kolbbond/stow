// stow::Error - failure reason from library construction. No X11.
#pragma once

#include <string>

namespace stow {

struct Error {
	enum class Code {
		None = 0,
		NoDisplay,   // could not open an X display; the one true failure
		NoFont,      // requested font could not be loaded
		NoColor,     // a color could not be allocated
		BadConfig,   // a config file failed to parse or validate
	};

	Code code = Code::None;
	std::string message;

	explicit operator bool() const { return code != Code::None; }
};

}  // namespace stow
