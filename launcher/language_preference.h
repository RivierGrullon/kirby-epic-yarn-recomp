// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace Launcher {
// These indices are also used by the UI service protocol.
enum class Language { Chinese, English, Spanish };
inline constexpr Language DefaultLanguage = Language::English;
inline constexpr std::array LanguageCodes{"zh-CN", "en", "es"};
inline constexpr char LanguageFile[] = "launcher-language.txt";

inline Language ReadLanguage(const std::filesystem::path& directory) {
    std::ifstream file(directory / LanguageFile);
    char buffer[16];
    file.read(buffer, sizeof(buffer));
    if (file.eof() && !file.bad()) {
        std::string_view code(buffer, static_cast<size_t>(file.gcount()));
        constexpr auto whitespace = " \t\r\n";
        const auto start = code.find_first_not_of(whitespace);
        if (start != std::string_view::npos) {
            code = code.substr(start, code.find_last_not_of(whitespace) - start + 1);
            for (size_t i = 0; i < LanguageCodes.size(); ++i)
                if (code == LanguageCodes[i]) return static_cast<Language>(i);
        }
    }
    return DefaultLanguage;
}
}
