// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/services/ios_service.h"
#include <array>
#include <cstring>
#include <iostream>
#include <fstream>
#ifndef _WIN32
#include <sys/xattr.h>
#endif
#include <stdexcept>
#include "../filesystem_fixture.h"
namespace {
std::array<uint8_t,65536> ram{};
unsigned checks;
uint8_t* Resolve(uint32_t a,uint32_t n) { return a && uint64_t(a)+n<=ram.size()?ram.data()+a:nullptr; }
void Put(uint32_t a,uint32_t v) { for(unsigned i=0;i<4;++i)ram.at(a+i)=v>>(24-i*8); }
uint32_t Get(uint32_t a) { return uint32_t(ram[a])<<24|uint32_t(ram[a+1])<<16|uint32_t(ram[a+2])<<8|ram[a+3]; }
void Check(bool v,const char* why) { ++checks;if(!v)throw std::runtime_error(why); }
}
int main() {
    const auto root = TestDirectory("kirby-ios-nand-");
    try {
        Kirby::IosServices ios(Resolve);
        ios.ConfigureNand(root,0x00010000524B3545ull);
        auto submit=[&] { return *ios.Submit(32,false); };
        auto open=[&](const char* path,uint32_t mode) {
            std::strcpy(reinterpret_cast<char*>(ram.data()+512),path);
            Put(32,1);Put(44,512);Put(48,mode);return submit();
        };
        const auto fs=open("/dev/fs",0),es=open("/dev/es",0);
        Check(fs>=0 && es>=0 && fs!=es,"FS/ES use owned distinct descriptors");
        auto title=[&] { Put(32,7);Put(40,es);Put(44,0x20);Put(48,0);Put(52,1);Put(56,640);Put(640,800);Put(644,8); };
        title();Check(submit()==0 && Get(800)==0x10000 && Get(804)==0x524B3545,"ES title ID is big endian and uses configured title");
        Put(48,0xFFFFFFFF);Check(submit()==-1017,"vector count overflow rejected");
        title();Put(644,4);Check(submit()==-1017,"short ES output rejected");
        title();Put(640,65532);Check(submit()==-1017,"ES output span must fit RAM");
        title();Put(44,0x1D);Put(48,1);Put(52,1);Put(648,832);Put(652,30);
        Check(submit()==0 && !std::strcmp(reinterpret_cast<char*>(ram.data()+832),"/title/00010000/524b3545/data"),"ES data directory request preserves full title identity");
        Put(652,29);Check(submit()==-1017,"data directory output requires NUL space");
        const auto fd=open("/shared2/sys/SYSCONF",1);
        Check(fd>=0 && fd!=fs && fd!=es,"file descriptor does not collide with service descriptors");
        Put(32,3);Put(40,fd);Put(44,1024);Put(48,16);
        Check(submit()==16 && !std::memcmp(ram.data()+1024,"SCv0",4),"IOS read transfers actual bytes");
        Put(44,65532);Put(48,16);Check(submit()==-101,"invalid read buffer rejected before moving cursor");
        Put(32,6);Put(44,11);Put(48,0);Put(52,0);Put(56,1024);Put(60,8);
        Check(submit()==0 && Get(1024)==0x4000 && Get(1028)==16,"file stats return real size and cursor after invalid read");
        Put(32,5);Put(44,uint32_t(-4));Put(48,2);Check(submit()==0x3FFC,"IOS signed seek relative to EOF");
        Put(32,3);Put(44,1024);Put(48,16);Check(submit()==4 && !std::memcmp(ram.data()+1024,"SCed",4),"IOS EOF read is partial");
        Check(submit()==0,"IOS EOF read returns zero");
        Put(32,2);Check(submit()==0 && !ios.HasHandle(fd),"IOS close destroys owned file");
        Check(submit()==-4,"closed descriptor cannot be reused without open");
        Check(open("/missing",1)==-106,"real absent NAND path uses FS_ENOENT");
        auto create=[&](const char* path,uint32_t command=3) {
            std::memset(ram.data()+512,0,0x4C); std::strcpy(reinterpret_cast<char*>(ram.data()+518),path);
            ram[512+0x46]=3; ram[512+0x47]=1; ram[512+0x48]=0; ram[512+0x49]=7;
            Put(32,6);Put(40,fs);Put(44,command);Put(48,512);Put(52,0x4C);Put(56,0);Put(60,0);
            return submit();
        };
        Check(create("/shared2/new")==0 && std::filesystem::is_directory(root/"shared2/new"),
              "create-directory ioctl creates a real local directory");
        std::array<uint8_t,4> metadata{};
        Kirby::NandStore metadataStore(root, 0x00010000524B3545ull);
        const auto getMetadata = [&](const std::filesystem::path& path, uint8_t* output) {
            std::array<uint8_t,4> bytes{};
            if (metadataStore.Attributes("/" + path.lexically_relative(root).generic_string(), bytes)) return -1;
            std::copy(bytes.begin(), bytes.end(), output); return 4;
        };
        const auto setMetadata = [&](const std::filesystem::path& path, const uint8_t* bytes) {
#ifdef _WIN32
            Kirby::WindowsFile::Handle node(CreateFileW(path.c_str(), FILE_WRITE_EA, FILE_SHARE_READ | FILE_SHARE_WRITE |
                FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            struct Record { ULONG next = 0; UCHAR flags = 0, length = 10; USHORT valueLength = 4;
                char bytes[15] = {'K','I','R','B','Y','.','I','S','F','S',0}; } record;
            std::memcpy(record.bytes + 11, bytes, 4);
            IO_STATUS_BLOCK result{};
            using Set = NTSTATUS (NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG);
            const auto set = Kirby::WindowsFile::Function<Set>("NtSetEaFile");
            return node && Kirby::WindowsFile::Status(set(node.value, &result, &record, offsetof(Record, bytes) + 15)) ? 0 : -1;
#else
            return setxattr(path.c_str(), "user.kirby.isfs", bytes, 4, 0);
#endif
        };
        Check(getMetadata(root/"shared2/new", metadata.data())==4 &&
              metadata==std::array<uint8_t,4>{3,1,0,7},"directory permissions and attributes persist as metadata");
        Check(create("/shared2/new")==-105,"existing directory is not silently recreated");
        Check(create("/missing/new")==-106,"create does not invent missing parents");
        Check(create("/../outside")==-101,"create rejects traversal");
        create("/shared2/invalid"); ram[512+0x46]=4;
        Check(submit()==-101,"invalid permission bits rejected");
        create("/shared2/short"); Put(52,0x4B);
        Check(submit()==-101,"create validates the complete wire structure");
        Check(create("/shared2/new/save.bin",9)==0 && std::filesystem::file_size(root/"shared2/new/save.bin")==0,
              "create-file ioctl creates an empty local save file");
        Check(submit()==-105,"create-file does not truncate an existing file");
        Check(getMetadata(root/"shared2/new/save.bin", metadata.data())==4 &&
              metadata==std::array<uint8_t,4>{3,1,0,7},"file permission metadata is persistent");
        const auto save=open("/shared2/new/save.bin",3);
        Check(save>=0,"created save file opens for native read/write");
        ram[1024]=0x5A; Put(32,4);Put(40,save);Put(44,1024);Put(48,1);
        Check(submit()==1,"created save file accepts actual data");
        Put(32,2); Check(submit()==0 && std::filesystem::file_size(root/"shared2/new/save.bin")==1,
              "save data survives closing the IOS descriptor");
        auto attributes=[&](const char* path) {
            std::memset(ram.data()+512,0,64);std::strcpy(reinterpret_cast<char*>(ram.data()+512),path);
            std::memset(ram.data()+1024,0xCD,0x50);
            Put(32,6);Put(40,fs);Put(44,6);Put(48,512);Put(52,64);Put(56,1024);Put(60,0x4C);
            return submit();
        };
        Check(attributes("/shared2/new/save.bin")==0 && Get(1024)==0 && ram[1028]==0 && ram[1029]==0 &&
              ram[1024+0x46]==3 && ram[1024+0x47]==1 && ram[1024+0x48]==0 && ram[1024+0x49]==7,
              "GetAttr returns logical ownership and persistent file metadata in SDK layout");
        Check(ram[1030]==0 && ram[1024+0x4C]==0xCD,"GetAttr does not leak path bytes or overrun its output");
        Check(attributes("/shared2/new")==0,"GetAttr handles directories");
        Check(attributes("/tmp")==0 && ram[1024+0x46]==3,"boot directory metadata is explicit and queryable");
        Check(attributes("/missing")==-106 && ram[1024]==0xCD,"missing GetAttr does not overwrite output");
        attributes("/shared2/new/save.bin");Put(60,0x4B);
        Check(submit()==-101,"GetAttr rejects short output");
        Put(60,0x4C);Put(56,65534);Check(submit()==-101,"GetAttr validates output mapping before access");
        Check(attributes("/../outside")==-101,"GetAttr rejects path traversal");
#ifdef _WIN32
        TestJunction(root/"shared2/sys", root/"escape");
#else
        std::filesystem::create_symlink("/etc/passwd",root/"escape");
#endif
        Check(attributes("/escape")==-102,"GetAttr does not follow symlinks");
        const std::array<uint8_t,4> invalidMetadata{4,1,0,7};
        Check(setMetadata(root/"shared2/new/save.bin", invalidMetadata.data())==0,
              "test can corrupt a local permission record");
        Check(attributes("/shared2/new/save.bin")==-103 && ram[1024]==0xCD,
              "GetAttr rejects corrupt metadata without inventing permissions");
        Check(setMetadata(root/"shared2/new/save.bin", metadata.data())==0,
              "test restores the permission record");
        std::ofstream(root/"shared2/plain") << "x";
        Check(attributes("/shared2/plain")==-103,"unregistered host files do not gain fabricated Wii attributes");
        Check(create("/shared2/moved")==0,"rename destination directory exists");
        auto rename=[&](const char* source,const char* destination) {
            std::memset(ram.data()+512,0,128);std::strcpy(reinterpret_cast<char*>(ram.data()+512),source);
            std::strcpy(reinterpret_cast<char*>(ram.data()+576),destination);
            Put(32,6);Put(40,fs);Put(44,8);Put(48,512);Put(52,128);Put(56,0);Put(60,0);
            return submit();
        };
        const auto held=open("/shared2/new/save.bin",1);
        Check(rename("/shared2/new/save.bin","/shared2/moved/save.bin")==-111,
              "rename rejects an open source file");
        Put(32,2);Put(40,held);Check(submit()==0,"source closes before commit");
        Check(rename("/shared2/new/save.bin","/shared2/moved/different")==-101,
              "IOS rename preserves file basenames");
        Check(rename("/shared2/new/save.bin","/shared2/moved/save.bin")==0 &&
              std::filesystem::file_size(root/"shared2/moved/save.bin")==1 &&
              std::ifstream(root/"shared2/moved/save.bin").get()==0x5A,
              "rename commits actual save bytes to the destination");
        Check(attributes("/shared2/moved/save.bin")==0 && ram[1024+0x49]==7,
              "rename preserves file attributes");
        Check(attributes("/shared2/new/save.bin")==-106,"committed source path is absent");
        Check(create("/shared2/new/save.bin",9)==0,"second temporary save can be created");
        Check(rename("/shared2/new/save.bin","/shared2/moved/save.bin")==0 &&
              std::filesystem::file_size(root/"shared2/moved/save.bin")==0,
              "rename replaces an existing file without preserving stale bytes");
        Check(rename("/shared2/moved/save.bin","/missing/save.bin")==-106,
              "rename does not invent destination parents");
        Check(rename("/../outside","/shared2/moved/save.bin")==-101,"rename rejects traversal");
#ifdef _WIN32
        TestJunction(root/"shared2/sys", root/"linked");
#else
        std::filesystem::create_directory_symlink("/etc",root/"linked");
#endif
        Check(rename("/shared2/moved/save.bin","/linked/save.bin")==-102,"rename cannot traverse host symlinks");
        rename("/missing","/shared2/moved/missing");Put(52,127);
        Check(submit()==-101,"rename validates both complete path fields");
        auto usage=[&] {
            std::memset(ram.data()+512,0,64); std::strcpy(reinterpret_cast<char*>(ram.data()+512),"/shared2/sys");
            Put(32,7);Put(40,fs);Put(44,12);Put(48,1);Put(52,2);Put(56,640);
            Put(640,512);Put(644,64);Put(648,800);Put(652,4);Put(656,804);Put(660,4);
        };
        usage(); Check(submit()==0 && Get(800)==1 && Get(804)==2,"usage serializes actual clusters and inodes");
        usage(); Check(submit()==0 && Get(800)==1 && Get(804)==2,"repeated usage starts from the directory beginning");
        usage(); Put(52,3); Check(submit()==-101,"usage rejects mismatched vector counts");
        usage(); Put(656,65534); Check(submit()==-101,"usage validates every output before writing");
        auto remove=[&](const char* path) {
            std::memset(ram.data()+512,0,64); std::strcpy(reinterpret_cast<char*>(ram.data()+512),path);
            Put(32,6);Put(40,fs);Put(44,7);Put(48,512);Put(52,64);Put(56,0);Put(60,0);
            return submit();
        };
        Check(remove("/missing")==-106,"delete reports an absent path");
        Check(remove("/shared2/sys/SYSCONF")==0 && open("/shared2/sys/SYSCONF",1)==-106,
              "delete removes the requested local file");
        Check(remove("/../outside")==-101,"delete rejects traversal");
        remove("/missing"); std::memset(ram.data()+512,'x',64);
        Check(submit()==-101,"delete rejects unterminated path");
        Put(52,63); Check(submit()==-101,"delete rejects wrong wire size");
        bool unsupported=false;
        try { open("/dev/unknown",0); } catch(const std::runtime_error&) { unsupported=true; }
        Check(unsupported,"unimplemented device remains explicit");
        std::cout<<checks<<" IOS NAND checks passed\n";
    } catch(const std::exception& e) {
        std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';
        std::filesystem::remove_all(root);return 1;
    }
    std::filesystem::remove_all(root);
}
