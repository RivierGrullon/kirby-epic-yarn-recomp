// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdexcept>
#include <vector>
#endif

namespace Kirby {
inline std::filesystem::path CommandLinePath(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}

// The Windows CRT's narrow argv uses the active code page, which can destroy
// Unicode paths before parsing starts. Own a UTF-8 copy for the entire main()
// invocation. POSIX argv already carries filesystem bytes and remains untouched.
class CommandLineArguments {
public:
    void Normalize(int& argc,char**& argv) {
#ifdef _WIN32
        int count=0;
        auto* wide=CommandLineToArgvW(GetCommandLineW(),&count);
        if (!wide) throw std::runtime_error("Cannot read the Windows command line");
        struct FreeArguments { wchar_t** value;~FreeArguments() { LocalFree(value); } } cleanup{wide};
        text_.clear();text_.reserve(count);
        for (int i=0;i<count;++i) {
            const int length=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,nullptr,0,nullptr,nullptr);
            if (!length) throw std::runtime_error("Command line contains invalid Unicode");
            std::string text(length,'\0');
            if (WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide[i],-1,text.data(),length,nullptr,nullptr)!=length)
                throw std::runtime_error("Cannot convert the Windows command line to UTF-8");
            text.pop_back();text_.push_back(std::move(text));
        }
        pointers_.clear();pointers_.reserve(text_.size()+1);
        for (auto& text : text_) pointers_.push_back(text.data());
        pointers_.push_back(nullptr);argc=count;argv=pointers_.data();
#else
        (void)argc;(void)argv;
#endif
    }
private:
#ifdef _WIN32
    std::vector<std::string> text_;
    std::vector<char*> pointers_;
#endif
};
} // namespace Kirby
