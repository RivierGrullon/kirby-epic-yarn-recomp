// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#undef CreateFile
#undef CreateDirectory
namespace Kirby {
inline void ValidateWindowsInput(const std::filesystem::path& root) {
    std::array<char, 8> boot{};
    std::ifstream header(root / "sys/boot.bin", std::ios::binary);
    if (!header.read(boot.data(), boot.size()) || std::string_view(boot.data(),6) != "RK5E01" || boot[7] != 0)
        throw std::runtime_error("Only RK5E01 Rev 0 is supported");
    const auto path = root / "sys/main.dol";
    if (std::filesystem::file_size(path) != 9340736)
        throw std::runtime_error("Supported DOL size mismatch");
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes(9340736);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
        throw std::runtime_error("Cannot read complete game DOL");
    BCRYPT_ALG_HANDLE algorithm{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("Cannot initialize SHA-256 provider");
    std::array<unsigned char,32> digest{};
    BCRYPT_HASH_HANDLE hash{};
    auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0) status = BCryptHashData(hash, bytes.data(), ULONG(bytes.size()), 0);
    if (status >= 0) status = BCryptFinishHash(hash, digest.data(), ULONG(digest.size()), 0);
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("DOL SHA-256 calculation failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string actual;
    for (const auto byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
    if (actual != "a73cd2f5b1768268451aa28efa649bcf95461b25a481fc4b6cd053c941cf9079")
        throw std::runtime_error("Unsupported DOL SHA-256; unmodified RK5E01 Rev 0 required");
}
}
#endif
