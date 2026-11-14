// SPDX-License-Identifier: GPL-3.0-only
#include "installation.h"
#include <nlohmann/json.hpp>
#include <cryptopp/sha.h>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace Launcher::Setup {
namespace {
namespace fs=std::filesystem;
using Json=nlohmann::json;
struct File {
    int fd;
    explicit File(const fs::path& path) : fd(open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)) {
        if (fd<0) throw std::runtime_error("Cannot open installed build metadata or artifact: "+path.string());
    }
    ~File() { close(fd); }
    File(const File&)=delete;
    File& operator=(const File&)=delete;
};
struct stat Regular(const File& file) {
    struct stat info{};
    if (fstat(file.fd,&info) || !S_ISREG(info.st_mode)) throw std::runtime_error("Installation files must be regular files");
    return info;
}
std::string Hex(CryptoPP::SHA256& hash) {
    std::array<CryptoPP::byte,32> bytes;hash.Final(bytes.data());
    constexpr char digits[]="0123456789abcdef";
    std::string value;value.reserve(64);
    for (auto byte : bytes) { value+=digits[byte>>4];value+=digits[byte&15]; }
    return value;
}
std::string Hash(std::string_view data) {
    CryptoPP::SHA256 hash;
    hash.Update(reinterpret_cast<const CryptoPP::byte*>(data.data()),data.size());return Hex(hash);
}
bool Digest(const Json& value) {
    if (!value.is_string()) return false;
    const auto text=value.get<std::string>();
    return text.size()==64 && text.find_first_not_of("0123456789abcdef")==std::string::npos;
}
std::string Text(const Json& value) {
    if (!value.is_string()) throw std::runtime_error("Invalid installed build string");
    const auto text=value.get<std::string>();
    if (text.empty() || text.find('\0')!=std::string::npos) throw std::runtime_error("Invalid installed build path");
    return text;
}
Json Read(const fs::path& path) {
    File file(path);
    if (Regular(file).st_size>131072) throw std::runtime_error("Installed build metadata exceeds 128 KiB");
    std::array<char,4096> bytes;std::string data;
    while (true) {
        const auto size=read(file.fd,bytes.data(),bytes.size());
        if (size<0) { if (errno==EINTR) continue;throw std::runtime_error("Cannot read installation metadata"); }
        if (!size) break;
        data.append(bytes.data(),size);
        if (data.size()>131072) throw std::runtime_error("Installation metadata grew beyond its limit");
    }
    return Json::parse(data,[](int depth,Json::parse_event_t,Json&) {
        if (depth>16) throw std::runtime_error("Installation metadata is too deeply nested");
        return true;
    });
}
void CheckFile(const fs::path& path,const Json& expected,bool executable=false) {
    if (!Digest(expected.at("sha256")) || !expected.at("size").is_number_unsigned())
        throw std::runtime_error("Invalid installed artifact hash or size");
    File file(path);const auto before=Regular(file);
    if ((executable && before.st_size<=0) || static_cast<uint64_t>(before.st_size)!=expected.at("size").get<uint64_t>() ||
        (executable && !(before.st_mode&S_IXUSR))) throw std::runtime_error("Installed artifact size or permissions mismatch");
    CryptoPP::SHA256 hash;std::array<CryptoPP::byte,1048576> data;
    while (true) {
        const auto size=read(file.fd,data.data(),data.size());
        if (size<0) { if (errno==EINTR) continue;throw std::runtime_error("Cannot read installed artifact"); }
        if (!size) break;
        hash.Update(data.data(),size);
    }
    const auto after=Regular(file);
    if (before.st_size!=after.st_size || before.st_mtim.tv_sec!=after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || Hex(hash)!=expected.at("sha256").get<std::string>())
        throw std::runtime_error("Installed artifact digest mismatch or file changed while reading");
}
void Directories(const fs::path& root,const fs::path& directory) {
    auto current=root;
    for (const auto& part : directory.lexically_relative(root)) {
        if (part=="..") throw std::runtime_error("Installation path is outside this checkout");
        current/=part;
        if (fs::is_symlink(fs::symlink_status(current))) throw std::runtime_error("Installation directories must not be symlinks");
    }
}
}
std::optional<SelectedInstallation> ReadInstallation(const std::filesystem::path& sourceRoot) {
    const auto root=fs::canonical(sourceRoot);
    const auto pointer=root/"local/user/installation.json";
    Directories(root,pointer.parent_path());
    if (fs::symlink_status(pointer).type()==fs::file_type::not_found) return {};
    const auto selection=Read(pointer);
    if (!selection.at("version").is_number_unsigned() || selection.at("version")!=1 || !Digest(selection.at("current")) ||
        (!selection.at("previous").is_null() && !Digest(selection.at("previous"))))
        throw std::runtime_error("Invalid installed build selection");
    SelectedInstallation result;result.identifier=selection.at("current").get<std::string>();
    if (!selection.at("previous").is_null()) result.previous=selection.at("previous").get<std::string>();
    const auto directory=root/"local/setup/installations"/result.identifier;
    Directories(root,directory);
    const auto manifest=Read(directory/"manifest.json");
    if (Hash(manifest.dump(-1,' ',true))!=result.identifier || !manifest.at("version").is_number_unsigned() || manifest.at("version")!=1 ||
        manifest.at("game_id")!="RK5E01" || !manifest.at("revision").is_number_unsigned() || manifest.at("revision")!=0 ||
        manifest.at("dol_sha1")!="f4280a2b0709bfde4132be22e9f7fa8ecfa2a130" ||
        !Digest(manifest.at("project_sha256")) || !Digest(manifest.at("translation_key")) ||
        !Digest(manifest.at("toolchain_image_id")) || manifest.at("executable").at("file")!="WiiCompiled")
        throw std::runtime_error("Installed manifest identity or game revision mismatch");
    const std::set<std::string> checks{"bootstrap","init","disc","es","video"};
    if (!manifest.at("verification").is_array() || manifest.at("verification").size()!=checks.size() ||
        manifest.at("verification").get<std::set<std::string>>()!=checks)
        throw std::runtime_error("Installed build has incomplete native verification");
    const Json resources=Json::array({{{"file","dsp_coef.bin"},{"size",4096},
        {"sha256","d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179"}}});
    if (manifest.at("resources")!=resources) throw std::runtime_error("Installed runtime resource identity mismatch");
    result.dataRoot=Text(manifest.at("data_root"));
    if (!result.dataRoot.is_absolute()) throw std::runtime_error("Installed data root must be absolute");
    result.executable=directory/"WiiCompiled";
    CheckFile(result.executable,manifest.at("executable"),true);
    CheckFile(directory/"dsp_coef.bin",manifest.at("resources").at(0));
    return result;
}
} // namespace Launcher::Setup
