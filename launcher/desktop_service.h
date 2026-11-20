// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "state.h"
#include <functional>

namespace Launcher {
// The host owns game and installation state. The Qt process sends one JSON request
// per line and receives one reply, including after failed commands.
int DesktopService(State& state,
    const std::function<bool(const std::filesystem::path&, std::string&)>& verify,
    const std::function<bool()>& startGame, const std::function<std::optional<int>()>& pollGame);
}
