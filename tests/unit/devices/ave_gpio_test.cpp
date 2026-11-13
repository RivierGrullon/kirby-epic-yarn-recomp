// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/video_si.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks;
void Check(bool value,const char* why) { ++checks; if(!value) throw std::runtime_error(why); }
struct Bus {
    Kirby::AveGpio device;
    uint32_t out=0,dir=0xC000;
    void Lines(bool clock,bool data) {
        out=(clock?0x4000:0)|(data?0x8000:0);
        if(!device.Write32(0xCD8000C0,out))throw std::runtime_error("GPIO write rejected");
    }
    void Output(bool enabled) {
        dir=0x4000|(enabled?0x8000:0);
        if(!device.Write32(0xCD8000C4,dir))throw std::runtime_error("GPIO direction rejected");
    }
    bool Data() { uint32_t in=0;device.Read32(0xCD8000C8,in);return in&0x8000; }
    void Start() { Output(true); Lines(false,true); Lines(true,true); Lines(true,false); Lines(false,false); }
    void Stop() { Output(true); Lines(false,false); Lines(true,false); Lines(true,true); }
    bool Byte(uint8_t byte) {
        Output(true);
        for(unsigned i=0;i<8;++i) {
            const bool bit=byte&(0x80>>i);
            Lines(false,bit);Lines(true,bit);Lines(false,bit);
        }
        Output(false); Lines(false,true); Lines(true,true);
        const bool ack=!Data();
        Lines(false,true);Output(true);
        return ack;
    }
};
}
int main() {
    try {
        Bus bus;
        bus.Lines(true,true);
        Check(bus.Data(),"idle data is high, not a permanent ACK");
        bus.Start();
        Check(bus.Byte(0xE0),"encoder write address acknowledged");
        Check(bus.Byte(0x6A) && bus.Byte(1),"register and value acknowledged");
        Check(bus.device.WriteCount()==0,"in-progress payload is not reported as completed");
        bus.Stop();
        Check(bus.device.WriteCount()==1 && bus.device.AckCount()==3 &&
              bus.device.Payload(0x6A).size()==1 && bus.device.Payload(0x6A)[0]==1,"completed encoder register payload");
        Check(bus.Data(),"encoder releases SDA after STOP");
        bus.Start();Check(!bus.Byte(0xE2),"absent I2C address NACKs");
        Check(!bus.Byte(0x6A) && !bus.Byte(7),"unselected device does not accept payload");bus.Stop();
        Check(bus.device.WriteCount()==1 && bus.device.Payload(0x6A)[0]==1,"unselected writes cannot mutate encoder");
        bus.Start();Check(bus.Byte(0xE0) && bus.Byte(0x40),"multi-byte register selected");
        for(uint8_t byte:{0x12,0x34,0x56}) Check(bus.Byte(byte),"multi-byte payload ACK");
        bus.Stop();
        const std::array<uint8_t,3> expected{0x12,0x34,0x56};
        Check(std::equal(bus.device.Payload(0x40).begin(),bus.device.Payload(0x40).end(),expected.begin(),expected.end()),
              "register payload retains all bytes in wire order");
        bus.Start();Check(bus.Byte(0xE0) && bus.Byte(0x6A) && bus.Byte(0),"register replacement acknowledged");bus.Stop();
        Check(bus.device.Payload(0x6A).size()==1 && bus.device.Payload(0x6A)[0]==0,"later transaction replaces register payload");
        Check(!bus.device.Write32(0xCD8000C4,0xC100) && !bus.device.Write32(0xCD8000C8,0),"unreviewed GPIO consumers and input writes rejected");
        uint32_t value=0;
        Check(!bus.device.Read32(0xCD8000CC,value),"unknown GPIO register rejected");
        bus.Start();bool unsupported=false;
        try {bus.Byte(0xE1);} catch(const std::runtime_error&){unsupported=true;}
        Check(unsupported,"encoder read transactions stop explicitly");
        bus.device.Reset();
        Check(bus.device.WriteCount()==0 && bus.device.AckCount()==0 && bus.device.Payload(0x6A).empty(),"reset clears decoder and payloads");
        Kirby::VideoSi devices;
        Check(devices.Read32(0xCD8000C4,value) && value==0xC000,"video MMIO routes encoder GPIO");
        std::cout<<checks<<" AV encoder GPIO checks passed\n";
    } catch(const std::exception& e) {std::cerr<<"check "<<checks<<": "<<e.what()<<'\n';return 1;}
}
