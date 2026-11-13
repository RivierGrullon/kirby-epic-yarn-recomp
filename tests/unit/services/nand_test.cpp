// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/services/nand.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace {
unsigned checks;
void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
}
int main() {
    char pattern[] = "/tmp/kirby-nand-test-XXXXXX";
    const auto* temp = mkdtemp(pattern);
    if (!temp) return 1;
    const std::filesystem::path root(temp);
    try {
        Kirby::NandStore nand(root, 0x00010000524B3545ull);
        Check(nand.DataDirectory() == "/title/00010000/524b3545/data", "title directory uses both halves of Kirby's ID");
        Check(std::filesystem::is_directory(root / nand.DataDirectory().substr(1)), "title directory exists");
        Check(std::filesystem::is_directory(root/"tmp"),"IOS boot initializes the temporary NAND directory");
        Check(nand.CreateDirectory("/tmp/save",{3,3,3,0})==0 &&
              nand.CreateFile("/tmp/save/data.bin",{3,0,0,0})==0,
              "IOS boot supports temporary save directories and files");
        uint32_t blocks = 99, nodes = 99;
        Check(nand.Usage(nand.DataDirectory(), blocks, nodes)==0 && blocks==0 && nodes==1,
              "empty directory usage includes its own inode");
        std::filesystem::create_directories(root/"usage/sub");
        std::ofstream(root/"usage/empty");
        std::ofstream(root/"usage/sub/partial") << std::string(16385, 'x');
        Check(nand.Usage("/usage", blocks, nodes)==0 && blocks==2 && nodes==4,
              "recursive usage rounds each file to 16 KiB clusters");
        Check(nand.Usage("/usage", blocks, nodes)==0 && blocks==2 && nodes==4,
              "repeated usage keeps an independent directory cursor");
        Check(nand.Usage("/missing", blocks, nodes)==-106 && blocks==2 && nodes==4,
              "failed usage preserves outputs");
        auto [error, file] = nand.Open("/shared2/sys/SYSCONF", 1);
        Check(error == 0 && file, "default SYSCONF opens");
        std::array<uint8_t, 0x4000> buffer{};
        Check(file->Read(buffer) == 0x4000 && !std::memcmp(buffer.data(), "SCv0", 4) &&
              !std::memcmp(buffer.data()+0x3FFC, "SCed", 4), "complete SYSCONF framing");
        uint32_t length=0, offset=0;
        Check(file->Stat(length, offset)==0 && length==0x4000 && offset==0x4000, "file stats include real cursor");
        Check(file->Read(buffer)==0, "EOF is zero bytes");
        Check(file->Seek(-4,2)==0x3FFC && file->Read(std::span(buffer).first(4))==4 &&
              !std::memcmp(buffer.data(),"SCed",4), "signed seek and partial read");
        Check(file->Seek(-1,0)==-101 && file->Seek(1,2)==-101, "seek rejects outside file");
        Check(file->Write(std::span(buffer).first(4))==-102, "read-only descriptor rejects writes");
        Check(nand.Open("/missing",1).first==-106, "missing file returns FS_ENOENT");
        Check(nand.Open("/shared2/sys/SYSCONF",0).first==-101, "invalid file mode rejected");
        Check(nand.Open("/../etc/passwd",1).first==-101 && nand.Open("relative",1).first==-101,
              "absolute normalized NAND paths required");
        std::filesystem::create_symlink("/etc/passwd",root/"escape");
        Check(nand.Open("/escape",1).first==-102, "symlink file is rejected");
        std::filesystem::create_directory_symlink("/etc",root/"linked");
        Check(nand.Open("/linked/passwd",1).first==-102, "symlink directory is rejected");
        Check(nand.Delete("/escape")==-102 && nand.Delete("/linked/passwd")==-102,
              "delete cannot follow host symlinks");
        Check(nand.Usage("/", blocks, nodes)==-102, "usage cannot follow a nested host symlink");
        auto [settingError, settings]=nand.Open("/title/00000001/00000002/data/setting.txt",1);
        std::array<uint8_t,256> encoded{};
        Check(settingError==0 && settings->Read(encoded)==256, "product settings have SDK size");
        std::string decoded;
        uint32_t key=0x73B5DBFA;
        for (const auto byte:encoded) {
            if (!byte) break;
            decoded += char(byte ^ uint8_t(key));
            key = (key << 1) | (key >> 31);
        }
        Check(decoded.find("AREA=USA\r\n")!=std::string::npos && decoded.find("VIDEO=NTSC\r\n")!=std::string::npos,
              "product settings decode through SDK's first encrypted NUL rule");
        auto [rwError,rw]=nand.Open("/shared2/sys/SYSCONF",3);
        const std::array<uint8_t,3> marker{1,2,3};
        Check(rwError==0 && rw->Seek(128,0)==128 && rw->Write(marker)==3, "writable file changes local backing");
        Kirby::NandStore reopened(root,0x00010000524B3545ull);
        auto [againError,again]=reopened.Open("/shared2/sys/SYSCONF",1);
        std::array<uint8_t,3> actual{};
        Check(againError==0 && again->Seek(128,0)==128 && again->Read(actual)==3 && actual==marker,
              "initialization preserves existing settings");
        std::cout<<checks<<" NAND checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    std::filesystem::remove_all(root);
}
