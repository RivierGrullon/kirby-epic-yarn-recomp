// SPDX-License-Identifier: GPL-3.0-only
#include "save_manager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define CRYPTOPP_ENABLE_NAMESPACE_WEAK 1
#include <cryptopp/aes.h>
#include <cryptopp/md5.h>
#include <cryptopp/modes.h>

namespace Launcher {
namespace {

// Kirby's Epic Yarn (RK5E01) NAND title identity and save location.
constexpr std::uint32_t kTitleType = 0x00010000;   // Disc-based Wii title.
constexpr std::uint32_t kTitleIdHi = 0x524b3545;   // "RK5E" (title ID, no version).
constexpr std::uint64_t kTitleId = (std::uint64_t(kTitleType) << 32) | kTitleIdHi;
constexpr const char* kSaveFile = "FLF.bin";
constexpr std::size_t kMaxBackups = 10;

// Dolphin "data.bin" Wii save export layout (Core/HW/WiiSaveStructs.h). Only the
// plaintext BkHeader and FileHDR records plus the SD-key-encrypted payloads are
// needed on import; export writes the same records plus an encrypted lead Header.
constexpr std::size_t kHeaderSize = 0xF0C0;
constexpr std::size_t kBkHeaderSize = 0x80;
constexpr std::size_t kFileHeaderSize = 0x80;
constexpr std::uint32_t kBkHeaderMagic = 0x426B0001;
constexpr std::uint32_t kFileHeaderMagic = 0x03ADF17E;
constexpr std::size_t kBlockSize = 0x40;
constexpr std::uint32_t kMaxEntries = 4096;
constexpr std::uint32_t kBannerSize = 0x72A0;   // BNR_SZ + one icon (minimum valid).
constexpr std::uint32_t kFullCertSize = 0x3C0;
constexpr std::uint32_t kNgId = 0x0403AC68;
constexpr std::uint8_t kPermissions = 0x3F;     // owner/group/other read-write.

// The Wii SD-key (AES-128-CBC). Public console constant Dolphin uses to seal
// data.bin payloads.
constexpr std::array<std::uint8_t, 16> kSdKey{
    0xAB, 0x01, 0xB9, 0xD8, 0xE1, 0x62, 0x2B, 0x08,
    0xAF, 0xBA, 0xD8, 0x4D, 0xBF, 0xC2, 0xA5, 0x5D};
// Initial IV Dolphin applies to the data.bin lead Header.
constexpr std::array<std::uint8_t, 16> kInitialIv{
    0x21, 0x67, 0x12, 0xE6, 0xAA, 0x1F, 0x68, 0x9F,
    0x95, 0xC5, 0xA2, 0x23, 0x24, 0xDC, 0x6A, 0x98};
// MD5 blanker used when hashing the data.bin lead Header.
constexpr std::array<std::uint8_t, 16> kMd5Blanker{
    0x0E, 0x65, 0x37, 0x81, 0x99, 0xBE, 0x45, 0x17,
    0xAB, 0x06, 0xEC, 0x22, 0x45, 0x1A, 0x57, 0x93};

std::uint32_t ReadBe32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}
std::uint64_t ReadBe64(const std::uint8_t* p) {
    return (std::uint64_t(ReadBe32(p)) << 32) | ReadBe32(p + 4);
}
void WriteBe32(std::uint8_t* p, std::uint32_t x) {
    p[0] = x >> 24; p[1] = x >> 16; p[2] = x >> 8; p[3] = x;
}
void WriteBe64(std::uint8_t* p, std::uint64_t x) {
    WriteBe32(p, static_cast<std::uint32_t>(x >> 32));
    WriteBe32(p + 4, static_cast<std::uint32_t>(x));
}
std::size_t BoundedStringLength(const char* p, std::size_t max) {
    std::size_t n = 0;
    while (n < max && p[n] != '\0') ++n;
    return n;
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(millis);
}

std::string FormatTimestamp(const std::string& millisText) {
    try {
        const long long millis = std::stoll(millisText);
        const std::time_t when = static_cast<std::time_t>(millis / 1000);
        std::tm local{};
#ifdef _WIN32
        if (localtime_s(&local, &when) != 0) return millisText;
#else
        if (!localtime_r(&when, &local)) return millisText;
#endif
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
        return std::string(buffer);
    } catch (...) {
        return millisText;
    }
}

// Locate the RK5E01 title-data folder inside a selected Dolphin NAND tree.
std::filesystem::path FindDolphinTitleData(const std::filesystem::path& source) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto hasSave = [&](const fs::path& dir) {
        std::error_code e;
        return fs::is_directory(dir, e) && fs::is_regular_file(dir / kSaveFile, e);
    };
    for (const fs::path& candidate : {
             source,
             source / "data",
             source / "524b3545/data",
             source / "00010000/524b3545/data",
             source / "title/00010000/524b3545/data",
             source / "Wii/title/00010000/524b3545/data"}) {
        if (hasSave(candidate)) return candidate;
    }
    if (!fs::is_directory(source, ec))
        throw std::runtime_error("The selected Dolphin save is not a folder");
    for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        if (!it->is_directory(e)) continue;
        const fs::path folder = it->path();
        if (folder.filename() != "data" || !fs::is_regular_file(folder / kSaveFile, e)) continue;
        fs::path parent = folder.parent_path();
        if (parent.filename() != "524b3545") continue;
        parent = parent.parent_path();
        if (parent.filename() != "00010000") continue;
        return folder;
    }
    throw std::runtime_error("No RK5E01 Kirby save (FLF.bin) was found in that Dolphin folder");
}

void CopyTree(const std::filesystem::path& source, const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    fs::create_directories(destination);
    std::error_code ec;
    for (fs::recursive_directory_iterator it(source, ec), end; !ec && it != end; it.increment(ec)) {
        const auto relative = fs::relative(it->path(), source);
        const auto target = destination / relative;
        if (it->is_symlink()) throw std::runtime_error("Save contains unsupported symbolic links");
        if (it->is_directory()) {
            fs::create_directories(target);
        } else if (it->is_regular_file()) {
            fs::create_directories(target.parent_path());
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing);
        } else {
            throw std::runtime_error("Save contains an unsupported file entry");
        }
    }
    if (ec) throw std::runtime_error("Cannot read the save folder");
}

bool ValidComponent(std::string_view name) {
    if (name.empty() || name.size() > 12) return false;  // NAND names are at most 12 chars.
    if (name == "." || name == "..") return false;
    for (const char c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '\0') return false;
    return true;
}

// A data.bin entry name is a NAND path relative to the title data directory and
// may span subdirectories with '/'. Keep it host-safe and NAND-shaped.
std::filesystem::path ParseRelativePath(const std::string& name) {
    if (name.empty()) throw std::runtime_error("The save contains an empty file name");
    std::filesystem::path result;
    std::string component;
    for (const char c : name) {
        if (c == '/') {
            if (!ValidComponent(component)) throw std::runtime_error("The save contains an invalid file name");
            result /= component;
            component.clear();
        } else if (c == '\\' || c == ':' || c == '\0') {
            throw std::runtime_error("The save contains an invalid file name");
        } else {
            component.push_back(c);
        }
    }
    if (!ValidComponent(component)) throw std::runtime_error("The save contains an invalid file name");
    result /= component;
    return result;
}

void DecryptSd(const std::uint8_t* iv, const std::uint8_t* input, std::size_t size,
               std::vector<std::uint8_t>& output) {
    output.resize(size);
    if (size == 0) return;
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption decryption;
    decryption.SetKeyWithIV(kSdKey.data(), kSdKey.size(), iv);
    decryption.ProcessData(output.data(), input, size);
}
void EncryptSd(const std::uint8_t* iv, const std::uint8_t* input, std::size_t size,
               std::vector<std::uint8_t>& output) {
    output.resize(size);
    if (size == 0) return;
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption encryption;
    encryption.SetKeyWithIV(kSdKey.data(), kSdKey.size(), iv);
    encryption.ProcessData(output.data(), input, size);
}

void WriteFile(const std::filesystem::path& target, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write the save file");
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("Cannot finish writing the save file");
}
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read the save file");
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

struct SaveEntry {
    std::filesystem::path path;  // Relative to the title data directory.
    std::vector<std::uint8_t> bytes;
    bool directory = false;
};

void CollectEntries(const std::filesystem::path& dir, const std::filesystem::path& relative,
                    std::vector<SaveEntry>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        const auto status = it->symlink_status(e);
        if (e) throw std::runtime_error("Cannot read the save directory");
        if (fs::is_symlink(status)) throw std::runtime_error("Save contains unsupported symbolic links");
        if (it->is_directory(e)) {
            SaveEntry entry;
            entry.path = relative / it->path().filename();
            entry.directory = true;
            out.push_back(std::move(entry));
            CollectEntries(it->path(), out.back().path, out);
        } else if (it->is_regular_file(e)) {
            SaveEntry entry;
            entry.path = relative / it->path().filename();
            entry.bytes = ReadFile(it->path());
            out.push_back(std::move(entry));
        } else {
            throw std::runtime_error("Save contains an unsupported file entry");
        }
    }
    if (ec) throw std::runtime_error("Cannot read the save directory");
}

void WriteBinIntoStaging(const std::filesystem::path& bin, const std::filesystem::path& staging) {
    std::ifstream in(bin, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open the selected .bin save");
    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    in.seekg(0, std::ios::beg);
    if (length < 0 || static_cast<std::uint64_t>(length) < kHeaderSize + kBkHeaderSize)
        throw std::runtime_error("This .bin is too small to be a Dolphin Wii save");

    in.seekg(static_cast<std::streamoff>(kHeaderSize));
    std::array<std::uint8_t, kBkHeaderSize> bk{};
    in.read(reinterpret_cast<char*>(bk.data()), bk.size());
    if (!in) throw std::runtime_error("Cannot read the .bin save header");
    if (ReadBe32(bk.data() + 4) != kBkHeaderMagic)
        throw std::runtime_error("This .bin is not a Dolphin Wii save export");
    const std::uint32_t count = ReadBe32(bk.data() + 0x0C);
    if (ReadBe64(bk.data() + 0x60) != kTitleId)
        throw std::runtime_error("This .bin save is not for RK5E01 (Kirby's Epic Yarn)");
    if (count > kMaxEntries)
        throw std::runtime_error("The .bin save contains too many entries");

    for (std::uint32_t i = 0; i < count; ++i) {
        std::array<std::uint8_t, kFileHeaderSize> fh{};
        in.read(reinterpret_cast<char*>(fh.data()), fh.size());
        if (!in) throw std::runtime_error("The .bin save is truncated");
        if (ReadBe32(fh.data()) != kFileHeaderMagic)
            throw std::runtime_error("The .bin save has a malformed file entry");
        const std::uint32_t size = ReadBe32(fh.data() + 4);
        const std::uint8_t type = fh[0x0A];
        const char* const rawName = reinterpret_cast<const char*>(fh.data() + 0x0B);
        const std::string name(rawName, BoundedStringLength(rawName, 0x40));
        const auto relative = ParseRelativePath(name);

        if (type == 2) {
            std::filesystem::create_directories(staging / relative);
            continue;
        }
        if (type != 1) throw std::runtime_error("The .bin save has an unknown entry type");

        const std::size_t rounded = ((std::uint64_t(size) + kBlockSize - 1) / kBlockSize) * kBlockSize;
        std::vector<std::uint8_t> encrypted(rounded);
        in.read(reinterpret_cast<char*>(encrypted.data()), static_cast<std::streamsize>(rounded));
        if (!in) throw std::runtime_error("The .bin save file data is truncated");
        std::vector<std::uint8_t> plain;
        DecryptSd(fh.data() + 0x50, encrypted.data(), encrypted.size(), plain);
        plain.resize(size);

        std::filesystem::create_directories((staging / relative).parent_path());
        WriteFile(staging / relative, plain);
    }
}

std::vector<std::string> BackupNames(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(BackupDirectory(root), ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e;
        if (it->is_directory(e)) names.push_back(it->path().filename().string());
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        try {
            return std::stoll(a) < std::stoll(b);
        } catch (...) {
            return a < b;
        }
    });
    return names;
}

bool ValidBackupName(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const char c : name)
        if (c == '/' || c == '\\' || c == ':' || c == '\0') return false;
    return true;
}

void PruneBackups(const std::filesystem::path& root) {
    auto names = BackupNames(root);  // Oldest first.
    while (names.size() > kMaxBackups) {
        std::error_code ec;
        std::filesystem::remove_all(BackupDirectory(root) / names.front(), ec);
        names.erase(names.begin());
    }
}

std::string PublishStaging(const std::filesystem::path& root,
                           const std::filesystem::path& staging, const char* success) {
    namespace fs = std::filesystem;
    const auto destination = SaveDataDirectory(root);
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) throw std::runtime_error("Cannot create the NAND title data directory");
    ec.clear();

    fs::path backup;
    if (fs::exists(destination, ec)) {
        backup = BackupDirectory(root) / Timestamp();
        fs::create_directories(backup.parent_path(), ec);
        if (ec) throw std::runtime_error("Cannot create the save backup directory");
        ec.clear();
        fs::rename(destination, backup, ec);
        if (ec) throw std::runtime_error("Cannot back up the existing save");
    }

    try {
        fs::rename(staging, destination, ec);
        if (ec) throw std::runtime_error("Cannot install the imported save");
    } catch (...) {
        std::error_code restoreEc;
        if (!backup.empty() && fs::exists(backup, restoreEc) && !fs::exists(destination, restoreEc))
            fs::rename(backup, destination, restoreEc);
        throw;
    }
    PruneBackups(root);
    return success;
}

std::filesystem::path MakeStaging(const std::filesystem::path& root) {
    const auto staging = SaveDataDirectory(root);
    const auto stagingPath = staging.parent_path() / "data.importing";
    std::error_code ec;
    std::filesystem::remove_all(stagingPath, ec);
    std::filesystem::create_directories(stagingPath, ec);
    if (ec) throw std::runtime_error("Cannot create the import staging directory");
    return stagingPath;
}

bool HasSave(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(SaveDataDirectory(root) / kSaveFile, ec);
}

}  // namespace

std::filesystem::path SaveDataDirectory(const std::filesystem::path& root) {
    return root / "local/user/nand/title/00010000/524b3545/data";
}
std::filesystem::path BackupDirectory(const std::filesystem::path& root) {
    return root / "local/save-backups/RK5E01";
}

std::string ImportDolphinSave(const std::filesystem::path& root,
                              const std::filesystem::path& dolphinSource) {
    namespace fs = std::filesystem;
    const auto source = FindDolphinTitleData(dolphinSource);
    const auto staging = MakeStaging(root);
    try {
        CopyTree(source, staging);
        return PublishStaging(root, staging, "Dolphin save imported for RK5E01");
    } catch (...) {
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        throw;
    }
}

std::string ImportDolphinSaveBin(const std::filesystem::path& root,
                                 const std::filesystem::path& binSource) {
    namespace fs = std::filesystem;
    const auto staging = MakeStaging(root);
    try {
        WriteBinIntoStaging(binSource, staging);
        return PublishStaging(root, staging, "Dolphin save imported for RK5E01");
    } catch (...) {
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        throw;
    }
}

std::string ImportSave(const std::filesystem::path& root, const std::filesystem::path& source) {
    std::error_code ec;
    if (std::filesystem::is_directory(source, ec)) return ImportDolphinSave(root, source);
    ec.clear();
    if (std::filesystem::is_regular_file(source, ec)) return ImportDolphinSaveBin(root, source);
    throw std::runtime_error("Choose a Dolphin save folder or a .bin save file");
}

std::string ExportSaveFolder(const std::filesystem::path& root,
                             const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    const auto source = SaveDataDirectory(root);
    if (!HasSave(root)) throw std::runtime_error("There is no save to export yet");
    std::error_code ec;
    if (fs::equivalent(source, destination, ec))
        throw std::runtime_error("Choose a different export folder");
    CopyTree(source, destination);
    return "Save exported to folder";
}

std::string ExportSaveBin(const std::filesystem::path& root,
                          const std::filesystem::path& destination) {
    namespace fs = std::filesystem;
    const auto source = SaveDataDirectory(root);
    if (!HasSave(root)) throw std::runtime_error("There is no save to export yet");

    std::vector<SaveEntry> entries;
    CollectEntries(source, {}, entries);
    if (entries.empty()) throw std::runtime_error("There is no save to export yet");

    std::uint64_t sizeOfFiles = 0;
    for (const auto& entry : entries) {
        sizeOfFiles += kFileHeaderSize;
        if (!entry.directory)
            sizeOfFiles += ((std::uint64_t(entry.bytes.size()) + kBlockSize - 1) / kBlockSize) * kBlockSize;
    }

    std::ofstream out(destination, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Cannot write the export file");

    // Lead Header (encrypted with the SD key and the fixed initial IV).
    std::vector<std::uint8_t> header(kHeaderSize, 0);
    WriteBe64(header.data(), kTitleId);
    WriteBe32(header.data() + 8, kBannerSize);
    header[12] = kPermissions;
    std::memcpy(header.data() + 14, kMd5Blanker.data(), kMd5Blanker.size());
    {
        CryptoPP::Weak::MD5 hash;
        std::array<std::uint8_t, CryptoPP::Weak::MD5::DIGESTSIZE> digest{};
        hash.Update(header.data(), header.size());
        hash.Final(digest.data());
        std::memcpy(header.data() + 14, digest.data(), digest.size());
    }
    std::vector<std::uint8_t> encryptedHeader;
    EncryptSd(kInitialIv.data(), header.data(), header.size(), encryptedHeader);
    out.write(reinterpret_cast<const char*>(encryptedHeader.data()),
              static_cast<std::streamsize>(encryptedHeader.size()));

    // BkHeader (plaintext).
    std::array<std::uint8_t, kBkHeaderSize> bk{};
    WriteBe32(bk.data() + 0x00, 0x70);
    WriteBe32(bk.data() + 0x04, kBkHeaderMagic);
    WriteBe32(bk.data() + 0x08, kNgId);
    WriteBe32(bk.data() + 0x0C, static_cast<std::uint32_t>(entries.size()));
    WriteBe32(bk.data() + 0x10, static_cast<std::uint32_t>(sizeOfFiles));
    WriteBe32(bk.data() + 0x1C, static_cast<std::uint32_t>(sizeOfFiles + kFullCertSize));
    WriteBe64(bk.data() + 0x60, kTitleId);
    out.write(reinterpret_cast<const char*>(bk.data()), bk.size());

    // File entries: FileHDR followed by SD-key-encrypted payload (block padded).
    std::array<std::uint8_t, kFileHeaderSize> fh{};
    const std::array<std::uint8_t, 16> zeroIv{};
    for (const auto& entry : entries) {
        fh.fill(0);
        WriteBe32(fh.data() + 0x00, kFileHeaderMagic);
        WriteBe32(fh.data() + 0x04,
                  entry.directory ? 0 : static_cast<std::uint32_t>(entry.bytes.size()));
        fh[0x08] = kPermissions;
        fh[0x0A] = entry.directory ? 2 : 1;
        const auto name = entry.path.generic_string();
        if (name.size() > 0x40) throw std::runtime_error("Save path is too long to export");
        std::memcpy(fh.data() + 0x0B, name.data(), name.size());
        out.write(reinterpret_cast<const char*>(fh.data()), fh.size());
        if (entry.directory) continue;

        const std::size_t rounded =
            ((entry.bytes.size() + kBlockSize - 1) / kBlockSize) * kBlockSize;
        std::vector<std::uint8_t> padded(rounded, 0);
        std::memcpy(padded.data(), entry.bytes.data(), entry.bytes.size());
        std::vector<std::uint8_t> encrypted;
        EncryptSd(zeroIv.data(), padded.data(), padded.size(), encrypted);
        out.write(reinterpret_cast<const char*>(encrypted.data()),
                  static_cast<std::streamsize>(encrypted.size()));
    }
    if (!out) throw std::runtime_error("Cannot finish writing the export file");
    return "Save exported to .bin";
}

std::vector<BackupEntry> ListBackups(const std::filesystem::path& root) {
    std::vector<BackupEntry> out;
    auto names = BackupNames(root);  // Oldest first.
    for (auto it = names.rbegin(); it != names.rend(); ++it)
        out.push_back({*it, FormatTimestamp(*it)});
    return out;
}

std::string RestoreBackup(const std::filesystem::path& root, const std::string& name) {
    namespace fs = std::filesystem;
    if (!ValidBackupName(name)) throw std::runtime_error("Invalid backup name");
    const auto backup = BackupDirectory(root) / name;
    std::error_code ec;
    if (!fs::is_directory(backup, ec)) throw std::runtime_error("That backup no longer exists");
    const auto staging = MakeStaging(root);
    try {
        CopyTree(backup, staging);
        return PublishStaging(root, staging, "Backup restored");
    } catch (...) {
        std::error_code cleanup;
        fs::remove_all(staging, cleanup);
        throw;
    }
}

std::string DeleteBackup(const std::filesystem::path& root, const std::string& name) {
    namespace fs = std::filesystem;
    if (!ValidBackupName(name)) throw std::runtime_error("Invalid backup name");
    const auto backup = BackupDirectory(root) / name;
    std::error_code ec;
    if (!fs::is_directory(backup, ec)) throw std::runtime_error("That backup no longer exists");
    fs::remove_all(backup, ec);
    if (ec) throw std::runtime_error("Cannot delete the backup");
    return "Backup deleted";
}

}  // namespace Launcher
