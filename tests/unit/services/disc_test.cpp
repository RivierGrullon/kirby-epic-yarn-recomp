// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/services/ios_service.h"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace {
unsigned checks;
void Check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }
template<class F> bool Rejects(F&& fn) {
    try { fn(); } catch (const std::runtime_error&) { return true; }
    return false;
}
void Put(uint8_t* p, uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=v>>(24-i*8); }
void Save(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file) throw std::runtime_error("Cannot write fixture");
}
void Fixture(const std::filesystem::path& root) {
    std::vector<uint8_t> boot(0x440), fst(80);
    Put(boot.data()+0x18,0x5D1C9EA3); Put(boot.data()+0x420,0x1000); Put(boot.data()+0x424,0x1100);
    Put(boot.data()+0x428,fst.size()/4);
    Put(fst.data(),0x01000000); Put(fst.data()+8,4);
    Put(fst.data()+12,0x01000001); Put(fst.data()+20,4); // directory "dir", parent=root
    Put(fst.data()+24,5); Put(fst.data()+28,0x2000); Put(fst.data()+32,35);
    Put(fst.data()+36,7); Put(fst.data()+40,0x40000000); Put(fst.data()+44,32); // beyond 4 GiB
    std::memcpy(fst.data()+48,"\0dir\0a\0b\0",9);
    Save(root/"sys/boot.bin",boot); Save(root/"sys/bi2.bin",std::vector<uint8_t>(0x2000,0x33));
    Save(root/"sys/apploader.img",std::vector<uint8_t>(32,0x44));
    Save(root/"sys/main.dol",std::vector<uint8_t>(32,0x55)); Save(root/"sys/fst.bin",fst);
    Save(root/"files/dir/a",std::vector<uint8_t>(35,0xA1)); Save(root/"files/dir/b",std::vector<uint8_t>(32,0xB2));
}
std::array<uint8_t,4096> ram{};
uint8_t* Resolve(uint32_t at,uint32_t size) { return at && uint64_t(at)+size<=ram.size()?ram.data()+at:nullptr; }
}
int main() {
    char pattern[]="/tmp/kirby-disc-test-XXXXXX";
    const auto* temp=mkdtemp(pattern);
    if (!temp) return 1;
    const std::filesystem::path root(temp);
    try {
        Fixture(root);
        Kirby::ExtractedDisc disc(root,0x118240000ull);
        Check(disc.FileCount()==2,"nested FST file count");
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x100000000ull);}),"FST extent must fit configured capacity");
        std::array<uint8_t,64> out{};
        disc.Read(0x8000,out);
        Check(out[0]==0xA1 && out[34]==0xA1 && out[35]==0 && out[63]==0,"read data plus bounded DMA padding");
        disc.Read(0x100000000ull,std::span(out).first(32));
        Check(out[0]==0xB2 && out[31]==0xB2,"offset above 4 GiB is not truncated");
        disc.Read(0x420,out);
        Check(out[32]==0x33 && out[63]==0x33,"read across contiguous metadata extents");
        out.fill(0xCC);
        Check(Rejects([&]{disc.Read(0x8030,out);}) && out.front()==0xCC && out.back()==0xCC,
              "unbacked gap is explicit and output is unchanged");
        Check(Rejects([&]{disc.Read(UINT64_MAX-15,out);}),"offset overflow rejected");
        Save(root/"files/dir/a",std::vector<uint8_t>(34,0));
        Check(Rejects([&]{disc.Read(0x8000,out);}) && out.front()==0xCC,"changed extracted file rejected");
        Fixture(root);
        std::array<uint8_t,32> input{};
        Put(input.data()+4,32); Put(input.data()+8,0x40000000);
        Check(disc.Ioctl(0x71,input,out,0x1000)==1 && out[0]==0xB2,"DI uses word offset and TC result");
        Check(disc.Ioctl(0x71,input,std::span(out).first(16),0x1000)==32,"undersized read output rejected");
        Check(disc.Ioctl(0x71,input,out,0x1001)==128,"unaligned read output rejected");
        Check(disc.Ioctl(0x70,input,out,0x1000)==1 && out[24]==0x5D,"disc ID comes from boot header");
        Check(disc.Ioctl(0x12,input,out,0x1000)==1 && out[3]==2 && out[8]==0x41,"inquiry profile");
        Check(disc.Ioctl(0x12,input,std::span(out).first(16),0x1000)==16,"undersized inquiry output times out");
        Check(disc.Ioctl(0x12,std::span(input).first(16),out,0x1000)==32,"DI input size checked");
        Check(disc.Ioctl(0x7A,input,out,0x1000)==1 && out[0]==0 && out[3]==0,"mounted closed cover");
        Check(disc.Ioctl(0x88,input,out,0x1000)==1 && out[3]==2,"disc present cover status");
        Check(disc.Ioctl(0x86,input,{},0)==1,"clear absent cover interrupt");
        Put(input.data()+8,0);
        Check(Rejects([&]{disc.Ioctl(0x8D,input,out,0x1000);}),"in-bounds raw-disc read is not confused with partition read");
        Put(input.data()+8,0x460A0000); out.fill(0xCC);
        Check(disc.Ioctl(0x8D,input,out,0x1000)==2 && out[0]==0xCC && out[63]==0xCC,
              "single-layer out-of-bounds read gives drive error without changing output");
        Check(disc.Ioctl(0xE0,input,out,0x1000)==1 && out[0]==0 && out[1]==5 && out[2]==0x21 && out[3]==0,
              "request error returns the latched out-of-bounds sense code");
        Check(disc.Ioctl(0xE0,input,out,0x1000)==1 && out[1]==0 && out[2]==0,"request error clears latched sense");
        Check(disc.Ioctl(0xA4,input,out,0x1000)==2,"REPORT KEY is invalid on a retail drive");
        Check(disc.Ioctl(0xE0,input,out,0x1000)==1 && out[1]==5 && out[2]==0x20,"REPORT KEY latches invalid-command sense");
        Put(input.data()+8,0x460A0001);
        Check(disc.Ioctl(0x8D,input,out,0x1000)==32,"raw-read range must contain the entire request");
        Put(input.data()+8,0xFFFFFFFC);
        Check(disc.Ioctl(0x8D,input,out,0x1000)==32,"raw-read range arithmetic does not wrap");
        Kirby::ExtractedDisc larger(root,0x200000000ull);
        Put(input.data()+8,0x460A0000);
        Check(Rejects([&]{larger.Ioctl(0x8D,input,out,0x1000);}),"out-of-bounds result depends on supplied capacity");
        Put(input.data()+8,0x40000000);
        Kirby::IosServices ios(Resolve); ios.ConfigureDisc(root,0x118240000ull);
        std::strcpy(reinterpret_cast<char*>(ram.data()+512),"/dev/di");
        Put(ram.data()+32,1); Put(ram.data()+44,512);
        const auto fd=*ios.Submit(32,false);
        Check(fd>=0 && ios.HasHandle(fd),"DI owns an IOS descriptor");
        Put(ram.data()+32,6); Put(ram.data()+40,fd); Put(ram.data()+44,0x71);
        Put(ram.data()+48,640); Put(ram.data()+52,32); Put(ram.data()+56,1024); Put(ram.data()+60,32);
        std::copy(input.begin(),input.end(),ram.begin()+640);
        Check(ios.Submit(32,true)==1 && ram[1024]==0xB2,"async IOS result is ready for bridge callback delivery");
        Put(ram.data()+56,4090);
        Check(ios.Submit(32,true)==-4,"IOS checks complete guest output span");
        Put(ram.data()+32,2);
        Check(ios.Submit(32,false)==0 && !ios.HasHandle(fd),"DI descriptor closes");
        Put(ram.data()+32,6);
        Check(ios.Submit(32,false)==-4,"closed DI descriptor is rejected");
        // Validate malformed metadata and symlink rejection using independent mounts.
        Save(root/"files/dir/a",std::vector<uint8_t>(34));
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"FST size must match extracted file");
        Fixture(root); std::filesystem::remove(root/"files/dir/a");
        std::filesystem::create_symlink(root/"sys/boot.bin",root/"files/dir/a");
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"file symlink rejected");
        std::filesystem::remove(root/"files/dir/a"); Fixture(root);
        std::filesystem::rename(root/"files/dir",root/"actual");
        std::filesystem::create_directory_symlink(root/"actual",root/"files/dir");
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"directory symlink rejected");
        std::filesystem::remove(root/"files/dir"); Fixture(root);
        std::fstream fst(root/"sys/fst.bin",std::ios::binary|std::ios::in|std::ios::out);
        fst.seekp(53); fst.write("..",2); fst.close();
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"FST traversal rejected");
        Fixture(root);
        fst.open(root/"sys/fst.bin",std::ios::binary|std::ios::in|std::ios::out);
        std::array<uint8_t,4> word{}; Put(word.data(),5);
        fst.seekp(20); fst.write(reinterpret_cast<const char*>(word.data()),4); fst.close();
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"FST child directory must fit parent");
        Fixture(root);
        fst.open(root/"sys/fst.bin",std::ios::binary|std::ios::in|std::ios::out);
        Put(word.data(),0x2009); fst.seekp(40); fst.write(reinterpret_cast<const char*>(word.data()),4); fst.close();
        Kirby::ExtractedDisc adjacent(root,0x118240000ull);
        adjacent.Read(0x8000,out);
        Check(out[34]==0xA1 && out[35]==0 && out[36]==0xB2 && out[63]==0xB2,
              "DMA padding never replaces an adjacent file's actual bytes");
        Fixture(root);
        fst.open(root/"sys/fst.bin",std::ios::binary|std::ios::in|std::ios::out);
        Put(word.data(),0x2004); fst.seekp(40); fst.write(reinterpret_cast<const char*>(word.data()),4); fst.close();
        Check(Rejects([&]{Kirby::ExtractedDisc bad(root,0x118240000ull);}),"overlapping file extents rejected");
        std::cout<<checks<<" extracted disc and IOS DI checks passed\n";
    } catch (const std::exception& e) {
        std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';
        std::filesystem::remove_all(root); return 1;
    }
    std::filesystem::remove_all(root);
}
