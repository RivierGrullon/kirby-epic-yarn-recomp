// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/services/ios_service.h"
#include <algorithm>
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
uint32_t Get(const uint8_t* p) { return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3]; }
void Save(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if (!out) throw std::runtime_error("Cannot write synthetic metadata");
}
std::array<uint8_t,8192> ram{};
uint8_t* Resolve(uint32_t at, uint32_t size) { return at && uint64_t(at)+size<=ram.size()?ram.data()+at:nullptr; }
}
int main() {
    char pattern[]="/tmp/kirby-es-test-XXXXXX";
    const auto* temp=mkdtemp(pattern);
    if (!temp) return 1;
    const std::filesystem::path root(temp);
    try {
        constexpr uint64_t title=0x00010000524B3545ull;
        std::vector<uint8_t> ticket(0x2A4,0),tmd(0x208,0);
        Put(ticket.data(),0x10001); Put(tmd.data(),0x10001);
        for (size_t i=0x1D0;i<ticket.size();++i) ticket[i]=uint8_t(i*7);
        for (unsigned i=0;i<8;++i) Put(ticket.data()+0x264+i*8,0);
        std::fill_n(ticket.data()+0x1BF,16,0xAB); // Synthetic excluded key field.
        Put(ticket.data()+0x1DC,title>>32); Put(ticket.data()+0x1E0,uint32_t(title));
        Put(tmd.data()+0x18C,title>>32); Put(tmd.data()+0x190,uint32_t(title)); tmd[0x1DF]=1;
        Kirby::DiscTitleMetadata metadata(ticket,tmd,title);
        const auto& view=metadata.TicketView();
        Check(view[0]==0 && view[1]==0 && view[2]==0 && view[3]==0,"view version field has correct padding");
        Check(std::equal(view.begin()+4,view.end(),ticket.begin()+0x1D0),"view copies ticket ID through time limits exactly");
        auto differentKey=ticket; std::fill_n(differentKey.data()+0x1BF,16,0xCD);
        Check(Kirby::DiscTitleMetadata::MakeTicketView(differentKey)==view,"encrypted title key is excluded from view");
        Check(metadata.TitleId()==title && std::equal(metadata.Tmd().begin(),metadata.Tmd().end(),tmd.begin()),
              "current title and complete TMD are retained");
        Check(!metadata.HasTimeLimits(),"disabled time-limit entries do not require consumption tracking");
        Check(!Kirby::DiscTitleMetadata::MakeTicketView(std::span(ticket).first(675)),"truncated ticket rejected");
        auto bad=ticket; bad[3]=2;
        Check(!Kirby::DiscTitleMetadata::MakeTicketView(bad),"unsupported signature format rejected");
        bad=ticket; bad[0x1BC]=1;
        Check(!Kirby::DiscTitleMetadata::MakeTicketView(bad),"v1 ticket rejected instead of misparsed");
        bad=ticket; bad[0x1E0]^=1;
        Check(Rejects([&]{Kirby::DiscTitleMetadata invalid(bad,tmd,title);}),"active ticket title mismatch rejected");
        bad=tmd; bad[0x190]^=1;
        Check(Rejects([&]{Kirby::DiscTitleMetadata invalid(ticket,bad,title);}),"active TMD title mismatch rejected");
        bad=tmd; bad[0x1DF]=2;
        Check(Rejects([&]{Kirby::DiscTitleMetadata invalid(ticket,bad,title);}),"truncated content table rejected");
        Check(Rejects([&]{Kirby::DiscTitleMetadata invalid(ticket,std::span(tmd).first(32),title);}),"short TMD header rejected");
        Save(root/"ticket.bin",ticket); Save(root/"tmd.bin",tmd);
        Kirby::IosServices ios(Resolve); ios.ConfigureNand(root/"nand",title);
        std::strcpy(reinterpret_cast<char*>(ram.data()+512),"/dev/es");
        Put(ram.data()+32,1); Put(ram.data()+44,512);
        const auto fd=*ios.Submit(32,false);
        Check(fd>=0,"ES descriptor opens");
        auto request=[&](uint32_t command,uint32_t ins,uint32_t in,uint32_t inSize,uint32_t outSize) {
            Put(ram.data()+32,7); Put(ram.data()+40,fd); Put(ram.data()+44,command);
            Put(ram.data()+48,ins); Put(ram.data()+52,1); Put(ram.data()+56,640);
            Put(ram.data()+640,in); Put(ram.data()+644,inSize);
            Put(ram.data()+640+ins*8,2048); Put(ram.data()+644+ins*8,outSize);
            std::fill(ram.begin()+2048,ram.begin()+4096,0xCC);
        };
        auto submit=[&]{return *ios.Submit(32,false);};
        request(0x1B,1,0,0,0xD8);
        Check(submit()==-1017 && ram[2048]==0xCC,"no active title returns EINVAL without output");
        std::copy(ticket.begin(),ticket.end(),ram.begin()+1024);
        request(0x1B,1,1024,0x2A4,0xD8);
        Check(submit()==0 && std::equal(view.begin(),view.end(),ram.begin()+2048),"explicit ticket view works without active disc title");
        ios.ConfigureDiscTitle(root,title);
        request(0x1B,1,0,0,0xD8);
        Check(submit()==0 && std::equal(view.begin(),view.end(),ram.begin()+2048),"empty input selects mounted ticket view");
        request(0x1B,1,1024,0x2A3,0xD8);
        Check(submit()==-1017 && ram[2048]==0xCC,"invalid ticket vector size preserves output");
        request(0x1B,1,8000,0x2A4,0xD8);
        Check(submit()==-1017,"whole input must fit guest RAM");
        request(0x1B,1,0,0,0xD7);
        Check(submit()==-1017,"ticket view requires exact output size");
        request(0x1B,1,0,0,0xD8); Put(ram.data()+648,8190);
        Check(submit()==-1017,"whole output must fit guest RAM");
        request(0x1B,1,0,0,0xD8); Put(ram.data()+48,UINT32_MAX);
        Check(submit()==-1017,"vector count overflow rejected");
        request(0x1B,1,0,0,0xD8); Put(ram.data()+56,8188);
        Check(submit()==-1017,"vector table span checked");
        request(0x39,0,0,0,4);
        Check(submit()==0 && Get(ram.data()+2048)==tmd.size(),"DI TMD size is exact extracted size");
        request(0x39,0,0,0,8); Check(submit()==-1017,"TMD size output must be four bytes");
        request(0x3A,1,1024,4,tmd.size()+32); Put(ram.data()+1024,tmd.size()+32);
        Check(submit()==0 && std::equal(tmd.begin(),tmd.end(),ram.begin()+2048) && ram[2048+tmd.size()]==0xCC,
              "DI TMD copies metadata and leaves surplus output untouched");
        request(0x3A,1,1024,4,tmd.size()); Put(ram.data()+1024,tmd.size()-1);
        Check(submit()==-1017 && ram[2048]==0xCC,"TMD requested size must match output vector");
        request(0x3A,1,1024,4,32); Put(ram.data()+1024,32);
        Check(submit()==-1017,"TMD output must hold full metadata");
        auto consumption=[&] {
            request(0x16,1,1024,8,16); Put(ram.data()+52,2);
            Put(ram.data()+656,3072); Put(ram.data()+660,4);
            std::copy_n(view.begin()+4,8,ram.begin()+1024);
        };
        consumption();
        Check(submit()==0 && Get(ram.data()+3072)==0 && ram[2048]==0xCC,
              "unlimited mounted ticket returns zero entries without inventing records");
        consumption(); Put(ram.data()+648,0); Put(ram.data()+652,0);
        Check(submit()==0 && Get(ram.data()+3072)==0,"consumption supports count-only query");
        consumption(); Put(ram.data()+660,3);
        Check(submit()==-1017,"consumption count buffer must be four bytes");
        consumption(); Put(ram.data()+648,8190);
        Check(submit()==-1017,"consumption entry span must fit RAM");
        consumption(); ram[1024]^=1;
        Check(Rejects([&]{submit();}) && ram[3072]==0xCC,"unknown ticket never receives fabricated usage");
        auto limited=ticket; Put(limited.data()+0x264,1); Save(root/"ticket.bin",limited);
        Kirby::IosServices limitedIos(Resolve); limitedIos.ConfigureNand(root/"nand",title);
        limitedIos.ConfigureDiscTitle(root,title);
        Put(ram.data()+32,1); Put(ram.data()+44,512); Put(ram.data()+48,0);
        Check(limitedIos.Submit(32,false)==fd,"limited fixture opens same independent ES descriptor");
        consumption();
        Check(Rejects([&]{limitedIos.Submit(32,false);}) && ram[3072]==0xCC,
              "enabled time limits stop explicitly until usage tracking exists");
        Check(Rejects([&]{ios.ConfigureDiscTitle(root,title);}),"mounted title cannot silently change");
        std::filesystem::remove(root/"ticket.bin");
        Check(Rejects([&]{Kirby::DiscTitleMetadata invalid(root,title);}),"missing ticket is explicit");
        std::cout<<checks<<" ES metadata and IOS vector checks passed\n";
    } catch (const std::exception& e) {
        std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';
        std::filesystem::remove_all(root); return 1;
    }
    std::filesystem::remove_all(root);
}
