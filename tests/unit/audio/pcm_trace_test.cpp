// SPDX-License-Identifier: GPL-3.0-only
#include "pcm_trace.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <unistd.h>

namespace fs=std::filesystem;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F function) {
    try { function(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid PCM capture operation succeeded");
}
uint32_t Le32(const std::vector<uint8_t>& bytes,size_t at) {
    return uint32_t(bytes.at(at)) | uint32_t(bytes.at(at+1))<<8 |
           uint32_t(bytes.at(at+2))<<16 | uint32_t(bytes.at(at+3))<<24;
}
uint64_t Le64(const std::vector<uint8_t>& bytes,size_t at) { return Le32(bytes,at) | uint64_t(Le32(bytes,at+4))<<32; }
std::vector<uint8_t> Read(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),{}};
}
int main() {
    std::string directory=(fs::temp_directory_path()/"kirby-pcm-XXXXXX").string();
    Check(mkdtemp(directory.data())!=nullptr,"Cannot create PCM test directory");
    struct Cleanup { fs::path path;~Cleanup() { fs::remove_all(path); } } cleanup{directory};
    const auto path=fs::path(directory)/"audio's trace.wav";
    {
        Kirby::PcmTrace trace(path);
        const uint8_t sample[]{0xFF,0xFE,0x12,0x34, 0x7F,0xFF,0x80,0x00};
        trace.Write(sample,1000000000);
        trace.Finish();trace.Finish();
        Reject([&] { trace.Write(sample,1000000001); });
    }
    const auto bytes=Read(path);
    Check(std::string(bytes.begin(),bytes.begin()+4)=="RIFF" &&
          std::string(bytes.begin()+8,bytes.begin()+16)=="WAVEfmt ","Capture is not a WAV file");
    Check(Le32(bytes,4)==bytes.size()-8 && Le32(bytes,40)==8,"WAV lengths do not match the recorded data");
    Check(bytes[20]==1 && bytes[22]==2 && Le32(bytes,24)==32000 && Le32(bytes,28)==128000 &&
          bytes[32]==4 && bytes[34]==16,"Capture does not declare stereo 32 kHz signed PCM16");
    const std::vector<uint8_t> expected{0x34,0x12,0xFE,0xFF,0x00,0x80,0xFF,0x7F};
    Check(std::equal(expected.begin(),expected.end(),bytes.begin()+44),"Wii right/left BE samples were not preserved as left/right LE PCM");
    Check(std::string(bytes.begin()+52,bytes.begin()+56)=="kTIM" && Le32(bytes,60)==1 && Le32(bytes,64)==16 &&
          Le64(bytes,68)==0 && Le64(bytes,76)==1000000000,"Capture lost its initial sample/host clock alignment");
    Reject([&] { Kirby::PcmTrace existing(path); });
    Check(Read(path)==bytes,"Opening a capture overwrote existing audio");
    const auto link=fs::path(directory)/"link.wav";fs::create_symlink(path,link);
    Reject([&] { Kirby::PcmTrace linked(link); });
    Check(Read(path)==bytes,"Capture followed and overwrote a symlink");
    const auto timing=fs::path(directory)/"timing.wav";
    {
        Kirby::PcmTrace trace(timing);
        const uint8_t partial[]{1,2,3};Reject([&] { trace.Write(partial,1); });
        std::vector<uint8_t> silence(4096*4);trace.Write(silence,10);
        const uint8_t one[]{1,2,3,4};Reject([&] { trace.Write(one,9); });
        trace.Write(one,20);
    }
    const auto timed=Read(timing);const size_t index=44+4097*4;
    Check(Le32(timed,40)==4097*4 && Le32(timed,index+4)==40,"Destructor did not finish PCM and clock records");
    Check(Le64(timed,index+16)==0 && Le64(timed,index+24)==10 &&
          Le64(timed,index+32)==4096 && Le64(timed,index+40)==20,"Clock checkpoints do not refer to the right stereo frames");
    std::cout<<"PCM capture: WAV decoding fields, channel order, timing, finalization and exclusive output verified\n";
}
