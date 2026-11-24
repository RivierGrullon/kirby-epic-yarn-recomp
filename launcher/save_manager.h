// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Launcher {
// One save backup under local/save-backups/RK5E01/.
struct BackupEntry {
    std::string name;       // Directory ID (epoch microseconds, sortable).
    std::string timestamp;  // Human-readable local time for display.
    std::string label;      // User label; empty for automatic/legacy backups.
};

// The live NAND save directory and the separate backup root.
std::filesystem::path SaveDataDirectory(const std::filesystem::path& root);
std::filesystem::path BackupDirectory(const std::filesystem::path& root);

// Import a Dolphin save (a Wii folder or a data.bin export). The existing save
// is backed up before replacement.
std::string ImportSave(const std::filesystem::path& root, const std::filesystem::path& source);
std::string ImportDolphinSave(const std::filesystem::path& root,
                              const std::filesystem::path& dolphinSource);
std::string ImportDolphinSaveBin(const std::filesystem::path& root,
                                 const std::filesystem::path& binSource);

// Export the current save as a folder of raw files or as a Dolphin data.bin.
std::string ExportSaveFolder(const std::filesystem::path& root,
                             const std::filesystem::path& destination);
std::string ExportSaveBin(const std::filesystem::path& root,
                          const std::filesystem::path& destination);

// Copy the on-disk save without replacing it. Named backups are kept until deleted.
// The caller must ensure the game is stopped, as with import/export/restore.
std::string CreateBackup(const std::filesystem::path& root, const std::string& label);

// List backups newest first, restore one, or delete one.
std::vector<BackupEntry> ListBackups(const std::filesystem::path& root);
std::string RestoreBackup(const std::filesystem::path& root, const std::string& name);
std::string DeleteBackup(const std::filesystem::path& root, const std::string& name);
}
