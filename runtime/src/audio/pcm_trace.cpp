// SPDX-License-Identifier: GPL-3.0-only
#include "pcm_trace.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace Kirby {
PcmTrace::PcmTrace(const std::filesystem::path& path) {
#ifdef _WIN32
    const int descriptor=_wopen(path.c_str(),_O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_S_IREAD|_S_IWRITE);
    if (descriptor<0) throw std::runtime_error(std::string("Cannot create PCM trace: ")+std::strerror(errno));
    file_.reset(_fdopen(descriptor,"w+b"));
    if (!file_) { _close(descriptor);throw std::runtime_error("Cannot open PCM trace stream"); }
#else
    const int descriptor=open(path.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (descriptor<0) throw std::runtime_error(std::string("Cannot create PCM trace: ")+std::strerror(errno));
    file_.reset(fdopen(descriptor,"w+b"));
    if (!file_) { close(descriptor);throw std::runtime_error("Cannot open PCM trace stream"); }
#endif
    if (std::setvbuf(file_.get(),buffer_.data(),_IOFBF,buffer_.size())!=0) throw std::runtime_error("Cannot buffer PCM trace");
    const uint8_t header[]{'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
        16,0,0,0,1,0,2,0,0,125,0,0,0,244,1,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    Put(header);
    if (std::fflush(file_.get())!=0) throw std::runtime_error("Cannot flush PCM trace header");
}
PcmTrace::~PcmTrace() {
    try { Finish(); }
    catch (const std::exception& error) { std::fprintf(stderr,"[kirby] PCM trace finalization failed: %s\n",error.what()); }
}
void PcmTrace::Put(std::span<const uint8_t> bytes) {
    if (std::fwrite(bytes.data(),1,bytes.size(),file_.get())!=bytes.size())
        throw std::runtime_error("Cannot write PCM trace");
}
void PcmTrace::U32(uint32_t value) {
    const uint8_t bytes[]{uint8_t(value),uint8_t(value>>8),uint8_t(value>>16),uint8_t(value>>24)};Put(bytes);
}
void PcmTrace::U64(uint64_t value) { U32(uint32_t(value));U32(uint32_t(value>>32)); }
void PcmTrace::Write(std::span<const uint8_t> bytes,uint64_t hostNs) {
    if (!file_) throw std::runtime_error("PCM trace is closed");
    if (bytes.size()%4) throw std::runtime_error("PCM trace requires complete stereo frames");
    if (bytes.empty()) return;
    if (frames_ && hostNs<lastHostNs_) throw std::runtime_error("PCM trace host clock moved backwards");
    // Reserve RIFF space for clock metadata; recording remains below 4 GiB.
    constexpr uint64_t limit=(uint64_t(std::numeric_limits<uint32_t>::max())-16*1024*1024)/4;
    if (bytes.size()/4>limit-frames_) throw std::runtime_error("PCM trace reached its WAV size limit");
    if (stamps_.empty() || frames_-stamps_.back().frame>=4096) stamps_.push_back({frames_,hostNs});
    lastHostNs_=hostNs;
    std::array<uint8_t,4096> converted;
    while (!bytes.empty()) {
        const auto count=std::min(bytes.size(),converted.size());
        for (size_t index=0;index<count;index+=4) {
            converted[index]=bytes[index+3];converted[index+1]=bytes[index+2];
            converted[index+2]=bytes[index+1];converted[index+3]=bytes[index];
        }
        Put(std::span(converted).first(count));frames_+=count/4;bytes=bytes.subspan(count);
    }
}
void PcmTrace::Finish() {
    if (!file_) return;
    const uint64_t pcmBytes=frames_*4;
    const uint32_t clockBytes=8+uint32_t(stamps_.size())*16;
    const uint64_t riffBytes=44+pcmBytes+8+clockBytes-8;
    if (riffBytes>std::numeric_limits<uint32_t>::max()) throw std::runtime_error("PCM trace exceeds RIFF size limit");
    const uint8_t tag[]{'k','T','I','M'};Put(tag);U32(clockBytes);U32(1);U32(16);
    for (const auto& stamp : stamps_) { U64(stamp.frame);U64(stamp.hostNs); }
    if (std::fseek(file_.get(),4,SEEK_SET)!=0) throw std::runtime_error("Cannot finalize PCM trace size");
    U32(uint32_t(riffBytes));
    if (std::fseek(file_.get(),40,SEEK_SET)!=0) throw std::runtime_error("Cannot finalize PCM trace data");
    U32(uint32_t(pcmBytes));
    if (std::fclose(file_.release())!=0) throw std::runtime_error("Cannot flush PCM trace");
}
}
