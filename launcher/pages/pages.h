// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "state.h"
#include <array>

namespace Launcher {
enum class Page { Play, Install, Display, Audio, Controls, Saves, Diagnostics, Count };
inline constexpr std::array PageNames{"Play", "Install", "Display", "Audio", "Controls", "Saves", "Diagnostics"};
static_assert(PageNames.size() == static_cast<size_t>(Page::Count));

Action DrawLauncher(State& state, Page& selected);
Action DrawPlay(State& state);
Action DrawInstall(State& state);
Action DrawDisplay(State& state);
Action DrawAudio(State& state);
Action DrawControls(State& state);
Action DrawSaves(State& state);
Action DrawDiagnostics(State& state);
} // namespace Launcher
