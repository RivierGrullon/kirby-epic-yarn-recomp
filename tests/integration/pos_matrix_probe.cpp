// SPDX-License-Identifier: GPL-3.0-only
// Compare against the user's locally translated SDK body, never embedded bytes.
#include "abi_bridge.h"
#include "generated/KirbyBootConfig.h"
#include "kirby/graphics/pos_matrix.h"
#include "kirby/graphics/gx_fifo.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <ctime>

extern "C" void func_8068BD70(CpuContext*);
namespace Kirby {
namespace {
constexpr uint32_t ring = 0x01200000;
using Words = std::array<uint32_t,12>;
using Routine = void (*)(CpuContext*);
void RegisteredUpload(CpuContext* ctx) { InvokeIndirectCpu(KirbyBootConfig::GXLoadPosMtxImm,ctx); }
void RegisteredDirectUpload(CpuContext* ctx) { InvokeDirectCpu<KirbyBootConfig::GXLoadPosMtxImm>(ctx); }
uint8_t* Resolve(uint32_t address, uint32_t size) { return Memory::GetPointer(address,size); }
void Check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void CpWord(unsigned index, uint32_t value) {
    auto& fifo=GraphicsFifo();
    Check(fifo.Write16(0xCC000020+index*4,uint16_t(value)),"CP low write failed");
    Check(fifo.Write16(0xCC000022+index*4,uint16_t(value>>16)),"CP high write failed");
}
struct Result {
    std::vector<uint8_t> observations;
    template<class T> void Add(const T& value) {
        const auto* first=reinterpret_cast<const uint8_t*>(&value);
        observations.insert(observations.end(),first,first+sizeof(value));
    }
};
Result Capture(Routine routine, const CpuContext& initial, const Words& matrix,
               unsigned prefix, bool wrap, bool writeback) {
    CpuContext cpu;
    std::memcpy(&cpu,&initial,sizeof(cpu));
    Result result; result.observations.reserve(4096);
    auto& fifo=GraphicsFifo(); fifo.Reset();
    std::memset(Resolve(ring,128),0xCD,128);
    for (unsigned i=0;i<matrix.size();++i) Memory::Write32(cpu.gpr[3]+i*4,matrix[i]);
    unsigned callbacks=0;
    fifo.Configure(Resolve,[&](std::span<const uint8_t> bytes) {
        result.Add(cpu);
        result.observations.insert(result.observations.end(),bytes.begin(),bytes.end());
        result.Add(fifo.Bursts()); result.Add(fifo.ConsumedBursts());
        result.Add(fifo.Status()); result.Add(fifo.Distance());
        // Model an earlier command writing back into the matrix source when
        // its last bytes enter the gather pipe. Header/load ordering matters.
        if (writeback && callbacks++==0)
            for (unsigned i=0;i<matrix.size();++i)
                Memory::Write32(initial.gpr[3]+i*4,matrix[i]^0x00800001u);
    });
    const auto start=wrap ? ring+96 : ring;
    fifo.SetWritePipeAddress(0x0C008000);
    Check(fifo.Write32(0xCC00300C,ring) && fifo.Write32(0xCC003010,ring+96) &&
          fifo.Write32(0xCC003014,start),"PI FIFO setup failed");
    CpWord(0,ring); CpWord(1,ring+96); CpWord(2,96); CpWord(3,0);
    CpWord(4,0); CpWord(5,start); CpWord(6,start); CpWord(7,0);
    Check(fifo.Write16(0xCC000002,17),"CP linking failed");
    std::array<uint8_t,31> leading{};
    for (unsigned i=0;i<leading.size();++i) leading[i]=uint8_t(0x40+i);
    fifo.Gather(std::span(leading).first(prefix));
    {
        CpuContextScope scope(&cpu);
        routine(&cpu);
        result.Add(cpu);
        result.Add(MkwGetHostFpControl());
    }
    result.Add(fifo.Bursts()); result.Add(fifo.ConsumedBursts());
    result.Add(fifo.Status()); result.Add(fifo.Distance());
    // Force any partial matrix payload into complete bursts without changing
    // the routine's own flush behavior. Remaining pending bytes are all zero.
    fifo.Gather(std::array<uint8_t,32>{});
    for (uint32_t address=0xCC00300C;address<=0xCC003014;address+=4) {
        uint32_t value=0; Check(fifo.Read32(address,value),"PI read failed"); result.Add(value);
    }
    result.observations.insert(result.observations.end(),Resolve(ring,128),Resolve(ring,128)+128);
    const auto source=Memory::GetPointer(initial.gpr[3],48);
    result.observations.insert(result.observations.end(),source,source+48);
    fifo.Reset();
    return result;
}
CpuContext Seed(uint32_t source, uint32_t index, uint32_t gqr, uint32_t fpscr) {
    CpuContext cpu;
    std::memset(&cpu,0,sizeof(cpu));
    for (unsigned i=0;i<32;++i) {
        cpu.gpr[i]=0x19283700+i;
        cpu.fpr[i].raw=0xFE123456ABCDEF00ull+i;
    }
    cpu.gpr[3]=source; cpu.gpr[4]=index;
    cpu.gqr[0]=gqr; cpu.fpscr=fpscr; cpu.cr=0xA7654321;
    cpu.lr=0x80004000; cpu.ctr=0x11223344; cpu.xer=0xA0000000; cpu.msr=0x30;
    return cpu;
}
}
void ProbePositionMatrix(CpuContext&) {
    static_assert(KirbyBootConfig::GXLoadPosMtxImm==0x8068BD70);
    const auto before=PositionMatrixStats();
    const std::array<uint32_t,16> special{0,0x80000000,1,0x80000001,0x007FFFFF,0x807FFFFF,
        0x00800000,0x80800000,0x3F800000,0xBF800000,0x7F800000,0xFF800000,
        0x7F800001,0xFF800001,0x7FC12345,0xFFC12345};
    const std::array<uint32_t,8> sources{0x81010000,0x01010000,0xC1010000,0x90010000,
        0x10010000,0xD0010000,0x810FFFF0,ring};
    const std::array<uint32_t,8> indices{0,1,7,30,255,0x3FFFFFFF,0x80000000,0xFFFFFFFF};
    const std::array<uint32_t,4> fpscrs{0,4,0x82000003,0xFFFFFFFF};
    uint32_t random=0x12345678; unsigned checks=0;
    const auto compare=[&](const CpuContext& seed,const Words& data,unsigned prefix,
                           bool wrap,bool writeback,const char* reason) {
        const auto expected=Capture(func_8068BD70,seed,data,prefix,wrap,writeback);
        for (const auto routine : {RegisteredDirectUpload,RegisteredUpload}) {
            Check(expected.observations==Capture(routine,seed,data,prefix,wrap,writeback).observations,reason);
            ++checks;
        }
    };
    for (unsigned pattern=0;pattern<48;++pattern) {
        Words data{};
        for (unsigned i=0;i<data.size();++i) {
            random^=random<<13; random^=random>>17; random^=random<<5;
            data[i]=pattern<16 ? special[(pattern+i)%special.size()] : random;
        }
        for (unsigned prefix=0;prefix<32;++prefix) for (unsigned j=0;j<indices.size();++j) {
            const auto seed=Seed(sources[(pattern+prefix+j)%sources.size()],indices[j],0,
                                 fpscrs[(pattern+prefix+j)%fpscrs.size()]);
            compare(seed,data,prefix,(pattern+j)&1,false,
                    "Native position matrix differs from original FIFO/CPU effects");
        }
    }
    // Nondefault GQRs and page crossings must retain the original path.
    for (const uint32_t gqr : {0x00040004u,0x00050005u,0x00060006u,0x00070007u,0x01040004u,0x3D043D04u,
                               0x00000004u,0x00000005u,0x00000006u,0x00000007u,0x00003D04u}) {
        for (unsigned prefix=0;prefix<32;++prefix) {
            Words data{};
            for (unsigned i=0;i<data.size();++i) data[i]=special[(prefix+i)%special.size()];
            const auto seed=Seed(sources[prefix%sources.size()],7,gqr,fpscrs[prefix%fpscrs.size()]);
            compare(seed,data,prefix,true,false,
                    "Nondefault quantization did not preserve the original upload");
        }
    }
    for (unsigned prefix=0;prefix<32;++prefix) {
        Words data{}; data.fill(0x3F800000);
        const auto seed=Seed(0x81010000,7,0,0);
        compare(seed,data,prefix,true,true,
                "Header or payload writeback changed matrix-load ordering");
    }
    const auto after=PositionMatrixStats();
    Check(after.fast>before.fast && after.fallback>before.fallback,"Matrix probe did not exercise both upload paths");
    std::cout << std::dec << "[kirby] position matrix cases: " << checks
              << " fast=" << after.fast-before.fast << " fallback=" << after.fallback-before.fallback << '\n';
    std::cout << "[kirby] position matrix: original SDK bytes, CPU state and FIFO ordering verified\n";
}
void BenchmarkPositionMatrix(CpuContext&) {
    constexpr unsigned iterations=500000, warmup=16384;
    const std::array routines{func_8068BD70,RegisteredDirectUpload,RegisteredDirectUpload,func_8068BD70};
    uint64_t expectedBursts=0,expectedChecksum=0;
    const auto cpuTime=[] {
        timespec value{};
        Check(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&value)==0,"Cannot read matrix benchmark CPU clock");
        return double(value.tv_sec)+double(value.tv_nsec)/1e9;
    };
    for (unsigned phase=0;phase<routines.size();++phase) {
        auto cpu=Seed(0x81010000,0,0,0);
        for (unsigned i=0;i<12;++i) Memory::Write32(cpu.gpr[3]+i*4,std::bit_cast<uint32_t>(float(i)/8));
        auto& fifo=GraphicsFifo(); fifo.Reset();
        uint64_t checksum=0;
        fifo.Configure(Resolve,[&](std::span<const uint8_t> bytes) {
            uint64_t value=0; std::memcpy(&value,bytes.data(),sizeof(value));
            checksum^=value;
        });
        fifo.SetWritePipeAddress(0x0C008000);
        fifo.Write32(0xCC00300C,ring); fifo.Write32(0xCC003010,ring+96); fifo.Write32(0xCC003014,ring);
        CpWord(0,ring); CpWord(1,ring+96); CpWord(2,96); CpWord(3,0);
        CpWord(4,0); CpWord(5,ring); CpWord(6,ring); CpWord(7,0);
        fifo.Write16(0xCC000002,17);
        CpuContextScope scope(&cpu);
        const auto routine=routines[phase];
        for (unsigned i=0;i<warmup;++i) { cpu.gpr[4]=i%10; routine(&cpu); }
        const auto beforeCpu=cpuTime();
        const auto before=std::chrono::steady_clock::now();
        for (unsigned i=0;i<iterations;++i) { cpu.gpr[4]=i%10; routine(&cpu); }
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count();
        const auto elapsedCpu=cpuTime()-beforeCpu;
        const auto bursts=fifo.Bursts();
        Check(bursts==uint64_t(warmup+iterations)*53/32 && fifo.ConsumedBursts()==bursts,
              "Matrix benchmark did not consume every 32-byte burst");
        if (!phase) { expectedBursts=bursts; expectedChecksum=checksum; }
        Check(bursts==expectedBursts && checksum==expectedChecksum,"Matrix benchmark outputs differ");
        std::cout << "[kirby:matrix-bench] " << phase << '|' << (phase==0 || phase==3 ? "original" : "registered-native")
                  << '|' << iterations << '|' << elapsedCpu << '|' << elapsed << '|' << bursts << '\n';
    }
    GraphicsFifo().Reset();
}
}
