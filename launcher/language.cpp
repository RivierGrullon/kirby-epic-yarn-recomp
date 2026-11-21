// SPDX-License-Identifier: GPL-3.0-only
#include "language_preference.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <random>

namespace Launcher {
bool WriteLanguage(const std::filesystem::path& directory, Language language) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    const auto path = Utf8(directory / LanguageFile);
    const auto temporary = path + "." + std::to_string(SDL_GetPerformanceCounter()) +
        "." + std::to_string(std::random_device{}()) + ".tmp";
    const auto content = std::string(LanguageCodes[static_cast<size_t>(language)]) + "\n";
    if (SDL_SaveFile(temporary.c_str(), content.data(), content.size()) &&
        SDL_RenamePath(temporary.c_str(), path.c_str())) return true;
    SDL_RemovePath(temporary.c_str());
    return false;
}

} // namespace Launcher
