// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/devices/dsp.h"
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
unsigned checks;
void Check(bool ok, const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
std::array<uint8_t, 256> ram{}, aram{};
uint64_t ticks;
bool validCode;
uint32_t taskReply = 0, sentMail = 0;
bool taskStopped = false;
bool interruptActive = false;
std::vector<uint8_t> audioSamples;
Kirby::DspTaskImage uploaded;
uint8_t* Resolve(uint32_t address, uint32_t length) {
    if (address >= 0x10000000 && uint64_t(address) + length <= 0x10000100) return aram.data() + address - 0x10000000;
    if (uint64_t(address) + length <= ram.size()) return ram.data() + address;
    return nullptr;
}
uint16_t Read(Kirby::DspBoot& d, uint32_t a) { uint16_t v; Check(d.Read16(a,v), "read16"); return v; }
}
int main() {
    try {
        Kirby::DspBoot d;
        d.Configure(Resolve, [] { return validCode; }, [] { return ticks; });
        Check(Read(d, 0xCC00500A) == 0x804, "DSP begins halted in init state");
        Check(d.Write16(0xCC005012, 0x43), "ARAM configuration");
        Check(Read(d, 0xCC005016) == 1 && !d.Write16(0xCC005016, 0), "ARAM controller mode is read-only");
        Check(Read(d, 0xCC00501A) == 156 && d.Write16(0xCC00501A, 200) && Read(d, 0xCC00501A) == 200, "ARAM refresh register is independent from mode");
        Check(d.Write16(0xCC00500A, 0x8AD) && Read(d, 0xCC00500A) == 0x804, "reset self-clears and IRQs acknowledge");
        Check(Read(d, 0xCC005004) == 0, "ROM mail is not visible while halted");
        for (unsigned i=0;i<ram.size();++i) ram[i] = i;
        Check(d.Write32(0xCC005020, 32) && d.Write32(0xCC005024, 64), "DMA address registers");
        Check(d.Write32(0xCC005028, 32), "start MRAM to ARAM DMA");
        Check(std::equal(ram.begin()+32,ram.begin()+64,aram.begin()+64), "DMA copies actual bytes into Wii MEM2");
        Check((Read(d,0xCC00500A) & 0x220) == 0x200, "DMA active before completion deadline");
        ticks += 21; d.Poll();
        Check((Read(d,0xCC00500A) & 0x220) == 0x20 && d.DmaCount() == 1, "timed completion sets ARAM interrupt");
        Check(d.Write16(0xCC00500A, 0x844) && d.Interrupt(), "ARAM mask raises DSP PI line");
        Check(d.Write16(0xCC00500A, 0x864) && !d.Interrupt(), "ARAM W1C acknowledgment");
        Check(d.Write32(0xCC005020, 128) && d.Write32(0xCC005024, 64) && d.Write32(0xCC005028, 0x80000020), "reverse DMA");
        Check(std::equal(aram.begin()+64, aram.begin()+96, ram.begin()+128), "reverse DMA restores exact bytes");
        ticks += 21; d.Poll();
        Check(d.Write32(0xCC005020, 256), "prepare invalid MRAM span");
        const auto before = aram;
        Check(!d.Write32(0xCC005028, 32) && before == aram && d.DmaCount() == 2, "invalid span cannot copy or signal completion");
        Check(!d.Write16(0xCC00500A, 4) && (Read(d,0xCC00500A) & 0x800), "unknown init microcode rejected before state mutation");
        validCode = true;
        Check(d.Write16(0xCC00500A, 4) && (Read(d,0xCC00500A) & 0x400), "verified init code enters load interval");
        ticks += 129; d.Poll(); Check(Read(d,0xCC00500A) & 0x400, "init-code busy remains before deadline");
        ++ticks; d.Poll(); Check(!(Read(d,0xCC00500A) & 0x400), "init-code busy clears after 130 timebase ticks");
        Check(d.Write16(0xCC00500A, 0), "release DSP halt");
        Check(Read(d,0xCC005004) == 0x8054 && Read(d,0xCC005004) == 0x8054, "high mailbox reads do not consume init mail");
        Check(Read(d,0xCC005006) == 0x4348 && Read(d,0xCC005004) == 0x54 && d.InitMailCount() == 1, "low mailbox read consumes verified init completion");
        Check(d.Write16(0xCC00500A, 0x8AD) && d.InitCount() == 1, "final reset retains diagnostic counters");
        Check(!d.Write16(0xCC005002, 0x1234) && !d.Write16(0xCC005004, 0), "unsupported DSP commands and mailbox direction rejected");
        Kirby::DspBoot task;
        task.Configure(Resolve, [] { return true; }, [] { return ticks; });
        task.ConfigureTaskBackend([](const Kirby::DspTaskImage& image) {
            if (image.entry != 0x10 || image.iram || image.dramLength) return false;
            uploaded = image; taskReply = 0xDCD10000; return true;
        }, [](uint32_t& mail) {
            if (!taskReply) return false;
            mail = taskReply; taskReply = 0; return true;
        }, [](uint32_t mail) { sentMail = mail; taskReply = 0xDCD10001; },
        [] { taskStopped = true; }, [] { return interruptActive; });
        auto send = [&](uint32_t mail) {
            Check(task.Write16(0xCC005000, mail >> 16), "mail high staging");
            return task.Write16(0xCC005002, mail);
        };
        Check(task.Write16(0xCC00500A, 0x805), "reset task loader while halted");
        Check(!send(0x80F3A001), "halted DSP cannot accept a task upload");
        Check(task.Write16(0xCC00500A, 0x900), "release halt with DSP IRQ mask");
        Check(Read(task,0xCC005004) == 0x8071 && Read(task,0xCC005006) == 0xFEED,
              "ROM loader offers its ready handshake");
        Check(send(0x80F3D001) && !send(0x10) && task.TaskBootCount() == 0,
              "incomplete descriptor cannot start a task or claim an init reply");
        Check(task.Write16(0xCC00500A, 0x901), "reset discards incomplete command");
        Read(task,0xCC005006);
        Check(send(0x80F3A001) && send(32) && send(0x80F3C002) && send(0) &&
              send(0x80F3A002) && send(32) && send(0x80F3B002) && send(0) && send(0x80F3D001),
              "ROM upload retains address, length, destination and DMEM fields");
        Check(!send(0x11) && task.TaskBootCount() == 0 && !task.Interrupt(),
              "backend rejects unknown entry before an init IRQ is asserted");
        Check(send(0x10) && task.TaskBootCount() == 1 && task.Interrupt() &&
              uploaded.address == 32 && uploaded.length == 32, "accepted task produces its real backend init reply");
        Check(!(Read(task,0xCC005000) & 0x8000), "accepted CPU mail clears mailbox busy");
        Check(Read(task,0xCC005004) == 0xDCD1 && Read(task,0xCC005004) == 0xDCD1,
              "reading task mail high does not consume it");
        Check(task.Write16(0xCC00500A, 0x980) && !task.Interrupt(), "task IRQ acknowledges independently of queued data");
        Check(Read(task,0xCC005006) == 0 && Read(task,0xCC005004) == 0x5CD1,
              "low read consumes init mail and clears mailbox status");
        Check(send(0xCDD10000) && sentMail == 0xCDD10000 && task.Interrupt(),
              "active task receives full CPU mailbox word and can request another IRQ");
        task.Audio().Configure(Resolve,[] { return ticks; },[](const uint8_t* p,uint32_t n) {
            audioSamples.insert(audioSamples.end(),p,p+n); return true;
        });
        interruptActive = true;
        Check(task.Write16(0xCC00500A,0x910) && task.Write16(0xCC005030,0) &&
              task.Write16(0xCC005032,0) && task.Write16(0xCC005036,0x8002),
              "enable native audio event scheduling");
        ticks += 121500;
        task.Poll();
        Check(audioSamples.empty(),"DMA must not outrun an active guest IRQ callback");
        interruptActive = false;
        task.Poll();
        Check(audioSamples.empty() && !(Read(task,0xCC00500A)&8),
              "queued DSP completion is serviced before the next overdue audio frame");
        interruptActive = true;
        Check(Read(task,0xCC005006)==1 && task.Write16(0xCC00500A,0x990),
              "service the queued DSP resume reply");
        interruptActive = false;
        task.Poll();
        Check(audioSamples.empty() && (Read(task,0xCC00500A)&8),
              "initial audio IRQ latches before fetching overdue samples");
        for (unsigned frame=0;frame<4;++frame) {
            interruptActive = true;
            std::fill_n(ram.begin(),64,uint8_t(0x31+frame));
            Check(task.Write16(0xCC00500A,0x918),"acknowledge audio IRQ");
            task.Poll();
            Check(audioSamples.size()==frame*64,"acknowledgment cannot consume the next buffer before refill returns");
            interruptActive = false;
            task.Poll();
            Check(audioSamples.size()==(frame+1)*64 &&
                  std::all_of(audioSamples.begin()+frame*64,audioSamples.end(),
                              [frame](uint8_t sample) { return sample==0x31+frame; }),
                  "overdue native audio frames carry the corresponding guest refill data");
        }
        Check(task.Audio().Blocks()==8 && !task.Audio().DroppedBlocks(),
              "native audio timeline catches up without stale repeated frames");
        interruptActive = true;
        Check(task.Write16(0xCC00500A,0x908),"disable the AID interrupt mask");
        ticks += 30375;
        task.Poll();
        Check(task.Audio().Blocks()==10 && audioSamples.size()==320 && !task.Interrupt(),
              "masked AID retains continuous raw DMA without requiring CPU callbacks");
        interruptActive = false;
        Check(task.Write16(0xCC00500A, 0x981) && taskStopped && task.TaskBootCount() == 1,
              "reset stops the task backend and preserves diagnostic count");
        std::cout << checks << " DSP boot checks passed\n";
    } catch (const std::exception& e) { std::cerr << "check " << checks << ": " << e.what() << '\n'; return 1; }
}
