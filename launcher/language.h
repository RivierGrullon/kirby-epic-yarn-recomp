// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <imgui.h>

namespace Launcher {
enum class Language { Chinese, English, Spanish };
inline constexpr std::array LanguageNames{"简体中文", "English", "Español"};
Language CurrentLanguage();
void SetLanguage(Language language);
Language ReadLanguage(const std::filesystem::path& directory);
bool WriteLanguage(const std::filesystem::path& directory, Language language);
const char* Tr(const char* english);
std::string StatusText(const std::string& message);
const ImWchar* LauncherGlyphs();
}
