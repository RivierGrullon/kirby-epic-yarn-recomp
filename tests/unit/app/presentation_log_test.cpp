// SPDX-License-Identifier: GPL-3.0-only
#include "presentation_log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "../filesystem_fixture.h"

namespace fs=std::filesystem;
void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Reject(F function) {
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid presentation log operation succeeded");
}
int main() {
    const auto directory=TestDirectory("kirby-presentation-");
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    const auto path=fs::path(directory)/"presentation's feedback.csv";
    {
        Kirby::PresentationLog log(path);
        Kirby::PresentationRecord clock{};clock.event=Kirby::PresentationEvent::Clock;
        clock.clockId=1;clock.clockNs=1234567890123;clock.sdlBeforeNs=100;clock.sdlAfterNs=103;
        log.Write(clock);
        Kirby::PresentationRecord frame{};frame.event=Kirby::PresentationEvent::Presented;
        frame.request=2;frame.requestSdlNs=104;frame.callbackSdlNs=120;
        frame.presentNs=1234567890130;frame.refreshNs=6944444;frame.sequence=4294967297;frame.flags=7;
        log.Write(frame);frame.event=Kirby::PresentationEvent::Discarded;log.Write(frame);
        frame.event=Kirby::PresentationEvent::Pending;log.Write(frame);log.Finish();log.Finish();
        Reject([&] { log.Write(frame); });
    }
    std::ifstream input(path);std::string line;
    std::getline(input,line);Check(line=="event,request,request_sdl_ns,callback_sdl_ns,present_ns,refresh_ns,sequence,flags,clock_id,clock_ns,sdl_before_ns,sdl_after_ns","Presentation CSV schema changed");
    std::getline(input,line);Check(line=="clock,0,0,0,0,0,0,0,1,1234567890123,100,103","Clock calibration lost precision");
    std::getline(input,line);Check(line=="presented,2,104,120,1234567890130,6944444,4294967297,7,0,0,0,0","Feedback fields lost 64-bit precision or flags");
    std::getline(input,line);Check(line.starts_with("discarded,"),"Discarded feedback was counted as presented");
    std::getline(input,line);Check(line.starts_with("pending,"),"Unresolved feedback was counted as presented");
    Reject([&] { Kirby::PresentationLog existing(path); });
#ifndef _WIN32
    const auto link=fs::path(directory)/"link.csv";fs::create_symlink(path,link);
    Reject([&] { Kirby::PresentationLog linked(link); });
#endif
    std::cout<<"Presentation CSV: clock, event types, 64-bit fields, flags, finalization and exclusive output verified\n";
}
