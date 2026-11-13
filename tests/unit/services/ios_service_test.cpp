// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/services/ios_service.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
std::array<uint8_t, 4096> ram{};
unsigned checks;
uint8_t* Resolve(uint32_t a, uint32_t n) { return a && uint64_t(a)+n <= ram.size() ? ram.data()+a : nullptr; }
void Put(uint32_t a, uint32_t v) { for(unsigned i=0;i<4;++i) ram.at(a+i)=v>>(24-8*i); }
uint32_t Get(uint32_t a) { return uint32_t(ram[a])<<24 | uint32_t(ram[a+1])<<16 | uint32_t(ram[a+2])<<8 | ram[a+3]; }
void Check(bool ok,const char* why) { ++checks; if(!ok) throw std::runtime_error(why); }
}
int main() {
 try {
    Kirby::IosServices ios(Resolve);
    auto open = [&](const char* path,uint32_t mode=0) { std::strcpy(reinterpret_cast<char*>(ram.data()+512),path); Put(32,1); Put(44,512); Put(48,mode); return *ios.Submit(32,false); };
    const auto im = open("/dev/stm/immediate"), eh = open("/dev/stm/eventhook");
    Check(im >= 0 && eh >= 0 && im != eh && ios.HasHandle(im) && ios.HasHandle(eh), "STM descriptors have independent ownership");
    auto event = [&] { Put(64,6); Put(72,eh); Put(76,0x1000); Put(80,768); Put(84,32); Put(88,800); Put(92,32); };
    event(); Check(!ios.Submit(64,true) && ios.PendingEventRequest()==64, "event stays pending without a button press");
    Check(!ios.TakeCompletion(), "no fabricated startup event");
    std::memcpy(ram.data()+96,ram.data()+64,32);
    Check(ios.Submit(96,true)==-2, "second hook fails with EEXIST");
    ios.TriggerEvent(0x20000);
    auto completed=ios.TakeCompletion();
    Check(completed && completed->request==64 && completed->result==0 && Get(800)==0x20000, "reset event writes BE output and completes original request");
    Check(!ios.PendingEventRequest() && !ios.TakeCompletion(), "event completes once");
    event(); Check(!ios.Submit(64,true), "hook can be rearmed");
    Put(32,6); Put(40,im); Put(44,0x3002); Put(48,0); Put(52,0); Put(56,0); Put(60,0);
    Check(ios.Submit(32,false)==0 && Get(800)==0, "release completes pending hook with zero event");
    completed=ios.TakeCompletion(); Check(completed && completed->request==64, "released hook retains request identity");
    Check(ios.Submit(32,false)==-6, "releasing missing hook returns ENOENT");
    event(); Put(88,4094); Check(ios.Submit(64,true)==-4 && !ios.PendingEventRequest(), "invalid output rejected before retaining request");
    event(); Put(84,0xFFFFFFFF); Check(ios.Submit(64,true)==-4, "overflowing input span rejected");
    Put(32,2); Put(40,im); Check(ios.Submit(32,false)==0 && !ios.HasHandle(im), "close releases descriptor");
    Check(ios.Submit(32,false)==-4, "double close fails");
    bool unsupported=false;
    try { open("/dev/fs"); } catch(const std::runtime_error& e) { unsupported=std::strstr(e.what(),"/dev/fs"); }
    Check(unsupported, "unimplemented service stops explicitly instead of reporting absent device");
    Check(ios.Submit(4090,false)==-4, "unaligned/truncated request rejected");
    Check(ios.Submit(0,false)==-4, "null request rejected");
    Put(32,1); Put(44,4095); ram[4095]='x';
    Check(ios.Submit(32,false)==-4, "pathname crossing RAM boundary rejected");
    Put(44,512); std::memset(ram.data()+512,'x',64);
    Check(ios.Submit(32,false)==-4, "unterminated pathname rejected");
    const auto reopened=open("/dev/stm/immediate");
    Check(reopened==im, "released descriptor is reused");
    event(); Put(92,3);
    Check(ios.Submit(64,true)==-4 && !ios.PendingEventRequest(), "event output needs a full word");
    event(); bool syncWait=false;
    try { ios.Submit(64,false); } catch(const std::runtime_error&) { syncWait=true; }
    Check(syncWait && !ios.PendingEventRequest(), "synchronous event wait is explicit and does not retain a request");
    ios.TriggerEvent(0x800);
    Check(!ios.TakeCompletion(), "button without a hook produces no completion");
    const auto kd=open("/dev/net/kd/request",1), time=open("/dev/net/kd/time");
    Check(kd>=0 && time>=0 && kd!=time, "network resource managers have owned descriptors");
    Put(32,6);Put(40,time);Put(44,0x17);Put(48,768);Put(52,32);Put(56,800);Put(60,32);
    Put(768,123456);Put(772,0);Put(800,0xFFFFFFFF);
    Check(ios.Submit(32,false)==0 && ios.NetworkRtcCounter()==123456 && Get(800)==0, "KD time stores RTC and returns BE common result");
    Put(772,1); bool persistent=false;
    try { ios.Submit(32,false); } catch(const std::runtime_error&) { persistent=true; }
    Check(persistent && ios.NetworkRtcCounter()==123456, "unimplemented persistent counter update is explicit");
    Put(40,reopened);Put(44,0x6002);Put(768,2);
    Check(ios.Submit(32,false)==0 && ios.IdleLedMode()==2, "STM idle LED mode is retained");
    Put(768,3);
    Check(ios.Submit(32,false)==-4 && ios.IdleLedMode()==2, "invalid slot LED setting does not change state");
    Put(44,0x5001); std::memset(ram.data()+768,0,32); Put(788,0xFFFFFFFF); Put(792,0xFFFF0000);
    Put(800,0xDEADBEEF);
    Check(ios.Submit(32,true)==0 && ios.VideoDimmingDisabled() && Get(800)==0xDEADBEEF,
          "STM disable-dimming request completes without inventing output register data");
    Put(52,28); Check(ios.Submit(32,true)==-4, "truncated dimming controls rejected"); Put(52,32);
    Put(792,0); Check(ios.Submit(32,true)==-4, "unexpected dimming register mask rejected"); Put(792,0xFFFF0000);
    Put(768,0x12); bool dimming=false;
    try { ios.Submit(32,true); } catch(const std::runtime_error&) { dimming=true; }
    Check(dimming && Get(800)==0xDEADBEEF, "active dimming remains explicit until renderer support exists");
    std::cout<<checks<<" IOS service checks passed\n";
 } catch(const std::exception& e) { std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1; }
}
