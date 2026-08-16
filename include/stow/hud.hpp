// stow::Hud - Tier 3: a grid dashboard described by an ini file.
//
//   stow::Error err;
//   if (auto hud = stow::Hud::from_file("hud.ini", &err)) hud->run();
//
// No X11 in this header; Dashboard is private under src/hud/.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "stow/error.hpp"
#include "stow/hud_config.hpp"

namespace stow {

class Hud {
public:
	static std::optional<Hud> from_file(const std::string& path, Error* err = nullptr);
	static std::optional<Hud> from_config(const HudConfig& cfg, Error* err = nullptr);

	Hud(Hud&&) noexcept;
	Hud& operator=(Hud&&) noexcept;
	Hud(const Hud&) = delete;
	Hud& operator=(const Hud&) = delete;
	~Hud();

	// Stop automatically after `seconds`. <= 0 means run until closed.
	void set_timeout(double seconds);

	// Blocks: renders and polls until closed or the timeout expires.
	void run();

private:
	struct Impl;
	std::unique_ptr<Impl> _p;
	explicit Hud(std::unique_ptr<Impl> p);
};

}  // namespace stow
