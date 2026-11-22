// SPDX-License-Identifier: GPL-3.0-only
#include "save_manager.h"
#include "filesystem_fixture.h"
#include "../windows_fixtures.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#ifndef _WIN32
#include <sys/xattr.h>
#endif

namespace {
namespace fs = std::filesystem;
unsigned checks;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template <typename Fn> void Reject(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected, "Operation must reject invalid input");
}
std::string Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void WriteMetadata(const fs::path& path, const void* bytes, std::size_t size) {
#ifdef _WIN32
    const auto result = WindowsTest::SetAttr(path.c_str(), "user.kirby.isfs", bytes, size, 0);
#else
    const auto result = setxattr(path.c_str(), "user.kirby.isfs", bytes, size, 0);
#endif
    Check(result == 0, "Write save permissions");
}
std::array<unsigned char, 4> ReadMetadata(const fs::path& path) {
    std::array<unsigned char, 4> bytes{};
#ifdef _WIN32
    const auto count = WindowsTest::GetAttr(path.c_str(), "user.kirby.isfs", bytes.data(), bytes.size());
#else
    const auto count = getxattr(path.c_str(), "user.kirby.isfs", bytes.data(), bytes.size());
#endif
    Check(count == 4, "Read save permissions");
    return bytes;
}
}

int main() {
    const auto root = TestDirectory("kirby-save-manager-") / fs::path(u8"partida 中文");
    try {
        Reject([&] { Launcher::CreateBackup(root, "Before the boss"); });
        Check(!fs::exists(Launcher::BackupDirectory(root)), "Missing save creates no backup");
        const auto data = Launcher::SaveDataDirectory(root);
        fs::create_directories(data / "sub");
        std::ofstream(data / "FLF.bin", std::ios::binary) << "saved progress";
        std::ofstream(data / "sub/file.bin", std::ios::binary) << "nested data";
        const std::array<unsigned char, 4> filePermissions{2, 1, 0, 5}, dirPermissions{3, 0, 1, 2};
        WriteMetadata(data / "FLF.bin", filePermissions.data(), 4);
        WriteMetadata(data, dirPermissions.data(), 4);
        WriteMetadata(data / "sub", dirPermissions.data(), 4);
        Reject([&] { Launcher::CreateBackup(root, " \t "); });
        Reject([&] { Launcher::CreateBackup(root, std::string(257, 'x')); });
        Reject([&] { Launcher::CreateBackup(root, "two\nlines"); });
        Check(Launcher::ListBackups(root).empty(), "Rejected names create no entries");
        const std::string label = "Antes del jefe / 中文 <backup>";
        Check(Launcher::CreateBackup(root, "  " + label + "  ") == "Backup created", "Create named backup");
        auto backups = Launcher::ListBackups(root);
        Check(backups.size() == 1 && backups[0].label == label && !backups[0].timestamp.empty(),
              "Unicode label and timestamp survive a fresh listing");
        const auto firstId = backups[0].name;
        const auto first = Launcher::BackupDirectory(root) / firstId;
        Check(firstId.find('/') == std::string::npos && firstId.find("jefe") == std::string::npos,
              "Labels are never filesystem paths");
        Check(Read(data / "FLF.bin") == "saved progress" && Read(first / "data/FLF.bin") == "saved progress",
              "Backup copies saved bytes without moving the live save");
        Check(ReadMetadata(data / "FLF.bin") == filePermissions &&
              ReadMetadata(first / "data/FLF.bin") == filePermissions, "Exact file metadata is preserved");
        Check(ReadMetadata(first / "data") == dirPermissions && ReadMetadata(first / "data/sub") == dirPermissions,
              "Exact directory metadata is preserved");
        Check(Read(first / "data/sub/file.bin") == "nested data", "Nested files are copied");
        Check(ReadMetadata(first / "data/sub/file.bin") == std::array<unsigned char, 4>{3, 1, 0, 0},
              "Legacy files without metadata receive save-compatible defaults");
        Launcher::CreateBackup(root, label);
        backups = Launcher::ListBackups(root);
        Check(backups.size() == 2 && backups[0].name != firstId && backups[0].label == label,
              "Duplicate display names have independent IDs");
        const auto secondId = backups[0].name;
        fs::remove(Launcher::BackupDirectory(root) / secondId / "data/FLF.bin");
        Reject([&] { Launcher::RestoreBackup(root, secondId); });
        Check(Read(data / "FLF.bin") == "saved progress", "Incomplete named backup cannot replace a live save");
        std::ofstream(data / "FLF.bin", std::ios::binary | std::ios::trunc) << "newer progress";
        Launcher::RestoreBackup(root, firstId);
        Check(Read(data / "FLF.bin") == "saved progress", "Named backup restores saved progress");
        Check(ReadMetadata(data / "FLF.bin") == filePermissions && ReadMetadata(data) == dirPermissions,
              "Restore preserves exact NAND permission bytes");
        Check(!fs::exists(data / "backup-name.txt") && !fs::exists(data / "data"),
              "Backup metadata stays outside the NAND");
        backups = Launcher::ListBackups(root);
        Check(backups.size() == 3 && backups[0].label.empty(), "Restore backs up the current save first");
        const auto automaticId = backups[0].name;
        Check(Read(Launcher::BackupDirectory(root) / automaticId / "FLF.bin") == "newer progress",
              "Replacement backup contains previous live progress");
        Launcher::RestoreBackup(root, automaticId);
        Check(Read(data / "FLF.bin") == "newer progress", "Legacy raw backups remain restorable");
        for (int i = 0; i < 12; ++i) Launcher::RestoreBackup(root, firstId);
        backups = Launcher::ListBackups(root);
        std::size_t automatic = 0, named = 0;
        for (const auto& backup : backups) backup.label.empty() ? ++automatic : ++named;
        Check(automatic == 10 && named == 2, "Retention prunes automatic backups only");
        Check(Read(first / "data/FLF.bin") == "saved progress", "Manual backup survives automatic retention");
        Launcher::DeleteBackup(root, secondId);
        Check(fs::exists(first) && !fs::exists(Launcher::BackupDirectory(root) / secondId),
              "Delete removes only the selected named backup");
        const auto countBefore = Launcher::ListBackups(root).size();
        WriteMetadata(data / "FLF.bin", "bad", 3);
        Reject([&] { Launcher::CreateBackup(root, "Incomplete"); });
        Check(Launcher::ListBackups(root).size() == countBefore && Read(data / "FLF.bin") == "saved progress",
              "Failed copy publishes no backup and leaves live data intact");
        for (const auto& entry : fs::directory_iterator(Launcher::BackupDirectory(root)))
            Check(!entry.path().filename().string().starts_with(".creating-"), "Failed staging is cleaned up");
        WriteMetadata(data / "FLF.bin", filePermissions.data(), 4);
        Reject([&] { Launcher::RestoreBackup(root, "../outside"); });
        Reject([&] { Launcher::DeleteBackup(root, "../outside"); });
        Check(Read(data / "FLF.bin") == "saved progress", "Invalid IDs leave progress intact");
        std::cout << checks << " save manager checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << "check " << checks << ": " << error.what() << '\n';
        fs::remove_all(root.parent_path());
        return 1;
    }
    fs::remove_all(root.parent_path());
}
