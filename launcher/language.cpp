// SPDX-License-Identifier: GPL-3.0-only
#include "language.h"
#include "platform.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <random>
#include <string_view>
#include <unordered_map>

namespace Launcher {
namespace {
Language current = Language::Chinese;
constexpr std::array Codes{"zh-CN", "en", "es"};
constexpr char Preference[] = "launcher-language.txt";
struct Message { const char *en, *zh, *es; };
constexpr Message messages[]{
#include "strings.inc"
};
}

Language CurrentLanguage() { return current; }
void SetLanguage(Language language) { current = language; }

Language ReadLanguage(const std::filesystem::path& directory) {
    std::ifstream file(directory / Preference);
    char buffer[16]{};
    if (file.getline(buffer, sizeof(buffer))) {
        for (size_t i = 0; i < Codes.size(); ++i)
            if (std::string_view(buffer) == Codes[i]) return static_cast<Language>(i);
    }
    return Language::Chinese;
}

bool WriteLanguage(const std::filesystem::path& directory, Language language) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    const auto path = Utf8(directory / Preference);
    const auto temporary = path + "." + std::to_string(SDL_GetPerformanceCounter()) +
        "." + std::to_string(std::random_device{}()) + ".tmp";
    const auto content = std::string(Codes[static_cast<size_t>(language)]) + "\n";
    if (SDL_SaveFile(temporary.c_str(), content.data(), content.size()) &&
        SDL_RenamePath(temporary.c_str(), path.c_str())) return true;
    SDL_RemovePath(temporary.c_str());
    return false;
}

const char* Tr(const char* english) {
    if (current == Language::English) return english;
    static const auto index = [] {
        std::unordered_map<std::string_view, const Message*> result;
        for (const auto& message : messages) result.emplace(message.en, &message);
        return result;
    }();
    const auto found = index.find(english);
    if (found == index.end()) return english; // Paths, hardware names and technical details.
    return current == Language::Chinese ? found->second->zh : found->second->es;
}

std::string StatusText(const std::string& message) {
    for (const std::string_view prefix : {"Missing ", "Already assigned to ", "Data verification failed: ", "Could not start the native executable: ",
                                         "Repair the installed build before playing. "}) {
        if (message.starts_with(prefix))
            return std::string(Tr(prefix.data())) + Tr(message.c_str() + prefix.size());
    }
    return Tr(message.c_str());
}

const ImWchar* LauncherGlyphs() {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesDefault());
        for (const auto* name : LanguageNames) builder.AddText(name);
        for (const auto& message : messages) {
            builder.AddText(message.zh);
            builder.AddText(message.es);
        }
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}
}
