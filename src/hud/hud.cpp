#include "stow/hud.hpp"

#include "hud/dashboard.hpp"

#include <utility>

namespace stow {

struct Hud::Impl {
	Dashboard dash;
	explicit Impl(Dashboard d) : dash(std::move(d)) {}
};

std::optional<Hud> Hud::from_config(const HudConfig& cfg, Error* err) {
	if(err) *err = Error{};
	if(!cfg.valid()) {
		if(err) *err = Error{Error::Code::BadConfig, cfg.error.empty() ? "invalid config" : cfg.error};
		return std::nullopt;
	}
	return Hud(std::make_unique<Impl>(Dashboard::from_config(cfg)));
}

std::optional<Hud> Hud::from_file(const std::string& path, Error* err) {
	return from_config(HudConfig::load(path), err);
}

Hud::Hud(std::unique_ptr<Impl> p) : _p(std::move(p)) {}
Hud::Hud(Hud&&) noexcept = default;
Hud& Hud::operator=(Hud&&) noexcept = default;
Hud::~Hud() = default;

void Hud::set_timeout(double seconds) { _p->dash.set_timeout(seconds); }
void Hud::run() { _p->dash.run(); }

}  // namespace stow
