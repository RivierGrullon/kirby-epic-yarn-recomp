// SPDX-License-Identifier: GPL-3.0-only
// Compare native register effects with locally translated, verified SDK bodies.
#include "kirby/graphics/cache.h"
#include "guest_abi/cpu_context.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

extern "C" void func_80663A80(CpuContext*);
extern "C" void func_80663AB0(CpuContext*);
extern "C" void func_80663AE0(CpuContext*);
extern "C" void func_80663B10(CpuContext*);
extern "C" void func_80663B40(CpuContext*);
static unsigned systemCalls;
extern "C" uint32_t OSSystemCall() { ++systemCalls; return 0; }

int main() {
    const std::array originals{func_80663A80,func_80663AB0,func_80663AE0,func_80663B10,func_80663B40};
    unsigned checks = 0;
    for (size_t f=0;f<originals.size();++f) {
        for (const uint32_t base : {0x80001000u,0x90002000u,0xD0003000u}) {
            for (uint32_t offset=0;offset<32;++offset) {
                for (const uint32_t length : {0u,1u,31u,32u,33u,129u,307200u}) {
                    CpuContext expected{};
                    for (unsigned r=0;r<32;++r) { expected.gpr[r]=0x12345678+r; expected.fpr[r].raw=0xFE123400+r; }
                    expected.cr=0xA7654321; expected.xer=(offset & 1) ? 0x80000000u : 0;
                    expected.ctr=123; expected.lr=0x80004000; expected.msr=0x8032;
                    expected.gpr[3]=base+offset; expected.gpr[4]=length;
                    auto actual=expected;
                    unsigned calls=0;
                    Kirby::ObserveDataCache([&](uint32_t start,uint64_t size) {
                        if (start!=base || size!=((uint64_t(offset)+length+31)&~uint64_t(31)))
                            throw std::runtime_error("Cache observer did not cover whole cache lines");
                        if (actual.gpr[3]!=base+offset || actual.gpr[4]!=length)
                            throw std::runtime_error("Cache boundary lost original arguments");
                        ++calls;
                    });
                    systemCalls=0; originals[f](&expected); const auto expectedCalls=systemCalls;
                    systemCalls=0; Kirby::MaintainDataCache(&actual,f==1 || f==2);
                    if (std::memcmp(&expected,&actual,sizeof(actual)) || calls!=(length!=0) || systemCalls!=expectedCalls)
                        throw std::runtime_error("Native cache boundary differs from translated SDK ABI");
                    ++checks;
                }
            }
        }
    }
    Kirby::ObserveDataCache({});
    std::cout << checks << " cache ABI/range checks passed against translated SDK functions\n";
}
