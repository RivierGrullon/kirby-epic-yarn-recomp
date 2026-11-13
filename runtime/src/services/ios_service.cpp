// SPDX-License-Identifier: GPL-3.0-only
// STM event semantics checked against Dolphin's IOS/STM implementation;
// see THIRD_PARTY.md. The SDK request layout comes from Swiftshine/key.
#include "kirby/services/ios_service.h"
#include <cstring>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace Kirby {
namespace {
uint32_t Read(const uint8_t* p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void Write(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = value >> (24 - i * 8);
}
}

void IosServices::ConfigureNand(const std::filesystem::path& root, uint64_t titleId) {
    if (nand_) throw std::runtime_error("IOS NAND is already configured");
    if (discTitle_ && discTitle_->TitleId() != titleId)
        throw std::runtime_error("NAND and disc title identities disagree");
    nand_ = std::make_unique<NandStore>(root, titleId);
}

void IosServices::ConfigureDisc(const std::filesystem::path& root, uint64_t capacity) {
    if (disc_) throw std::runtime_error("IOS disc is already configured");
    disc_ = std::make_unique<ExtractedDisc>(root, capacity);
}

void IosServices::ConfigureDiscTitle(const std::filesystem::path& root, uint64_t titleId) {
    if (discTitle_) throw std::runtime_error("Disc title metadata is already configured");
    if (nand_ && nand_->TitleId() != titleId)
        throw std::runtime_error("NAND and disc title identities disagree");
    discTitle_ = std::make_unique<DiscTitleMetadata>(root, titleId);
}

std::optional<int32_t> IosServices::Submit(uint32_t address, bool asynchronous) {
    const auto* req = address && !(address & 31) ? resolve_(address, 32) : nullptr;
    if (!req) return -4;
    const auto command = Read(req), arg = Read(req + 12);
    if (command == 1) {
        std::string path;
        bool terminated = false;
        for (uint32_t i = 0; arg && i < 64 && uint64_t(arg) + i <= UINT32_MAX; ++i) {
            const auto* p = resolve_(arg + i, 1);
            if (!p) return -4;
            if (!*p) { terminated = true; break; }
            path += char(*p);
        }
        if (!terminated) return -4;
        Handle handle{};
        const auto mode = Read(req + 16);
        if (path == "/dev/stm/immediate") handle.device = Device::StmImmediate;
        else if (path == "/dev/stm/eventhook") handle.device = Device::StmEventHook;
        else if (path == "/dev/net/kd/request") handle.device = Device::KdRequest;
        else if (path == "/dev/net/kd/time") handle.device = Device::KdTime;
        else if (disc_ && path == "/dev/di") handle.device = Device::Di;
        else if (nand_ && path == "/dev/fs") handle.device = Device::Fs;
        else if (nand_ && path == "/dev/es") handle.device = Device::Es;
        else if (nand_ && !path.starts_with("/dev/")) {
            auto [error, file] = nand_->Open(path, mode);
            if (error) return error;
            handle = {Device::File, std::move(file)};
        }
        else throw std::runtime_error("Unimplemented IOS open: " + path);
        if (handle.device != Device::File && mode != 0 &&
            !(handle.device == Device::KdRequest && mode == 1)) return -4;
        // Reuse the lowest free descriptor; do not fabricate SDK globals.
        int32_t fd = 0;
        while (handles_.contains(fd)) ++fd;
        handles_.emplace(fd, std::move(handle));
        return fd;
    }
    const auto fd = static_cast<int32_t>(Read(req + 8));
    const auto handle = handles_.find(fd);
    if (handle == handles_.end()) return -4;
    if (command == 2) {
        if (eventRequest_ && fd == eventHandle_)
            throw std::runtime_error("STM close with a pending event requires cancellation support");
        handles_.erase(handle);
        return 0;
    }
    if (handle->second.device == Device::File) {
        auto& file = *handle->second.file;
        const auto count = Read(req + 16);
        if (command == 3 || command == 4) {
            auto* data = count ? resolve_(arg, count) : nullptr;
            if (count > INT32_MAX || (count && (!arg || !data))) return -101;
            return command == 3 ? file.Read({data, count}) : file.Write({data, count});
        }
        if (command == 5) return file.Seek(static_cast<int32_t>(arg), count);
        if (command == 6 && arg == 11) {
            const auto out = Read(req + 24), size = Read(req + 28);
            auto* bytes = out && size == 8 ? resolve_(out, size) : nullptr;
            if (!bytes || Read(req+20)) return -101;
            uint32_t length, offset;
            const auto result = file.Stat(length, offset);
            if (result) return result;
            Write(bytes, length); Write(bytes+4, offset);
            return 0;
        }
        throw std::runtime_error("Unimplemented NAND file operation: " + std::to_string(command));
    }
    if (handle->second.device == Device::Es && command == 7) {
        const auto ins = Read(req+16), outs = Read(req+20), vector = Read(req+24);
        if (arg == 0x16) {
            if (ins != 1 || outs != 2 || !discTitle_) return -1017;
            const auto* vectors = vector && !(vector & 3) ? resolve_(vector, 24) : nullptr;
            if (!vectors || Read(vectors+4) != 8 || Read(vectors+20) != 4) return -1017;
            const auto in = Read(vectors), entries = Read(vectors+8), length = Read(vectors+12);
            const auto out = Read(vectors+16);
            const auto* ticketId = in ? resolve_(in, 8) : nullptr;
            auto* count = out ? resolve_(out, 4) : nullptr;
            if (!ticketId || !count || (length & 7) || (length && (!entries || !resolve_(entries, length))))
                return -1017;
            if (std::memcmp(ticketId, discTitle_->TicketView().data()+4, 8))
                throw std::runtime_error("ES consumption requested for an unmounted ticket");
            if (discTitle_->HasTimeLimits())
                throw std::runtime_error("ES timed-ticket consumption tracking is not implemented");
            Write(count, 0); // This mounted ticket has no enabled time limits.
            return 0;
        }
        if (arg == 0x1B || arg == 0x39 || arg == 0x3A) {
            if (ins != (arg == 0x39 ? 0u : 1u) || outs != 1) return -1017;
            const auto* vectors = vector && !(vector & 3) ? resolve_(vector, (ins+outs)*8) : nullptr;
            if (!vectors) return -1017;
            const auto out = Read(vectors+ins*8), outSize = Read(vectors+ins*8+4);
            auto* output = out && outSize ? resolve_(out, outSize) : nullptr;
            if (!output) return -1017;
            if (arg == 0x39) {
                if (outSize != 4 || !discTitle_) return -1017;
                Write(output, discTitle_->Tmd().size());
                return 0;
            }
            const auto in = Read(vectors), inSize = Read(vectors+4);
            const auto* input = in && inSize ? resolve_(in, inSize) : nullptr;
            if (inSize && !input) return -1017;
            if (arg == 0x1B) {
                if (outSize != 0xD8 || (inSize != 0 && inSize != 0x2A4)) return -1017;
                if (!inSize) {
                    if (!discTitle_) return -1017;
                    std::memcpy(output, discTitle_->TicketView().data(), 0xD8);
                } else {
                    const auto view = DiscTitleMetadata::MakeTicketView({input, inSize});
                    if (!view) return -1017;
                    std::memcpy(output, view->data(), view->size());
                }
                return 0;
            }
            if (inSize != 4 || Read(input) != outSize || !discTitle_ || outSize < discTitle_->Tmd().size())
                return -1017;
            std::memcpy(output, discTitle_->Tmd().data(), discTitle_->Tmd().size());
            return 0;
        }
        if ((arg == 0x20 && (ins != 0 || outs != 1)) ||
            (arg == 0x1D && (ins != 1 || outs != 1))) return -1017;
        if (arg != 0x20 && arg != 0x1D)
            throw std::runtime_error("Unimplemented ES ioctlv: " + std::to_string(arg));
        const auto* vectors = vector && !(vector & 3) ? resolve_(vector, (ins+outs)*8) : nullptr;
        if (!vectors) return -1017;
        const auto out = Read(vectors+ins*8), size = Read(vectors+ins*8+4);
        auto* output = out && size == (arg == 0x20 ? 8u : 30u) ? resolve_(out, size) : nullptr;
        if (!output) return -1017;
        if (arg == 0x20) {
            Write(output, nand_->TitleId() >> 32);
            Write(output+4, nand_->TitleId());
        } else {
            const auto input = Read(vectors), inputSize = Read(vectors+4);
            const auto* title = input && inputSize == 8 ? resolve_(input, 8) : nullptr;
            if (!title) return -1017;
            if ((uint64_t(Read(title)) << 32 | Read(title+4)) != nand_->TitleId())
                throw std::runtime_error("ES data directory requested for an uninstalled title");
            std::memset(output, 0, size);
            std::memcpy(output, nand_->DataDirectory().c_str(), nand_->DataDirectory().size()+1);
        }
        return 0;
    }
    if (handle->second.device == Device::Fs && command == 7 && arg == 12) {
        const auto vector = Read(req+24);
        if (Read(req+16) != 1 || Read(req+20) != 2 || !vector || (vector & 3)) return -101;
        const auto* vectors = resolve_(vector, 24);
        if (!vectors || Read(vectors+4) != 64 || Read(vectors+12) != 4 || Read(vectors+20) != 4)
            return -101;
        const auto path = Read(vectors), blocks = Read(vectors+8), files = Read(vectors+16);
        const auto* input = path ? resolve_(path, 64) : nullptr;
        auto* blockOutput = blocks ? resolve_(blocks, 4) : nullptr;
        auto* fileOutput = files ? resolve_(files, 4) : nullptr;
        if (!input || !blockOutput || !fileOutput) return -101;
        const auto* end = static_cast<const uint8_t*>(std::memchr(input, 0, 64));
        if (!end) return -101;
        uint32_t clusters, inodes;
        const auto result = nand_->Usage(std::string(reinterpret_cast<const char*>(input), end-input), clusters, inodes);
        if (!result) { Write(blockOutput, clusters); Write(fileOutput, inodes); }
        return result;
    }
    if (handle->second.device == Device::Fs && command == 6 && arg == 6) {
        const auto in=Read(req+16), out=Read(req+24);
        const auto* input=in && Read(req+20)==64 ? resolve_(in,64) : nullptr;
        auto* output=out && Read(req+28)==0x4C ? resolve_(out,0x4C) : nullptr;
        if (!input || !output) return -101;
        const auto* end=static_cast<const uint8_t*>(std::memchr(input,0,64));
        if (!end) return -101;
        const std::string path(reinterpret_cast<const char*>(input),end-input);
        std::array<uint8_t,4> metadata{};
        const auto result=nand_->Attributes(path,metadata);
        if (!result) {
            // Logical IOS UID/GID are 0 in this private mount; never expose host IDs.
            // Like Dolphin, zero undefined path/padding bytes in the SDK output.
            std::memset(output,0,0x4C);
            std::copy(metadata.begin(),metadata.end(),output+0x46);
        }
        std::cout << "[kirby] NAND attributes " << path << " result=" << result << std::endl;
        return result;
    }
    if (handle->second.device == Device::Fs && command == 6 && (arg == 3 || arg == 9)) {
        // RK5E01 FSFileIoctl: path at 6, permissions at 0x46..48, attribute at 0x49.
        const auto address=Read(req+16), size=Read(req+20);
        const auto* input=address && size==0x4C ? resolve_(address,size) : nullptr;
        if (!input || Read(req+28)) return -101;
        const auto* end=static_cast<const uint8_t*>(std::memchr(input+6,0,64));
        if (!end) return -101;
        const std::string path(reinterpret_cast<const char*>(input+6),end-input-6);
        const std::array<uint8_t,4> metadata{input[0x46],input[0x47],input[0x48],input[0x49]};
        const auto result=arg==3 ? nand_->CreateDirectory(path,metadata) : nand_->CreateFile(path,metadata);
        std::cout << "[kirby] NAND create " << (arg==3 ? "directory " : "file ") << path
                  << " result=" << result << std::endl;
        return result;
    }
    if (handle->second.device == Device::Fs && command == 6 && arg == 8) {
        const auto address=Read(req+16);
        const auto* input=address && Read(req+20)==128 ? resolve_(address,128) : nullptr;
        if (!input || Read(req+28)) return -101;
        const auto* oldEnd=static_cast<const uint8_t*>(std::memchr(input,0,64));
        const auto* newEnd=static_cast<const uint8_t*>(std::memchr(input+64,0,64));
        if (!oldEnd || !newEnd) return -101;
        const std::string source(reinterpret_cast<const char*>(input),oldEnd-input);
        const std::string destination(reinterpret_cast<const char*>(input+64),newEnd-input-64);
        const auto result=nand_->Rename(source,destination);
        std::cout << "[kirby] NAND rename " << source << " -> " << destination << " result=" << result << std::endl;
        return result;
    }
    if (handle->second.device == Device::Fs && command == 6 && arg == 7) {
        // ISFS_Delete: 64-byte NUL-terminated path, no output buffer.
        const auto in = Read(req+16), size = Read(req+20);
        const auto* bytes = in && size == 64 ? resolve_(in, size) : nullptr;
        if (!bytes || Read(req+28)) return -101;
        const auto* end = static_cast<const uint8_t*>(std::memchr(bytes, 0, size));
        if (!end) return -101;
        return nand_->Delete(std::string(reinterpret_cast<const char*>(bytes), end-bytes));
    }
    if (handle->second.device == Device::Fs || handle->second.device == Device::Es)
        throw std::runtime_error("Unimplemented IOS " + std::string(handle->second.device == Device::Fs ? "FS" : "ES") +
                                 " command=" + std::to_string(command) + " ioctl=" + std::to_string(arg));
    if (command != 6)
        throw std::runtime_error("Unimplemented IOS device command: " + std::to_string(command));
    const auto in = Read(req + 16), inSize = Read(req + 20);
    const auto out = Read(req + 24), outSize = Read(req + 28);
    if ((inSize && (!in || !resolve_(in, inSize))) ||
        (outSize && (!out || !resolve_(out, outSize)))) return -4;
    if (handle->second.device == Device::Di)
        return disc_->Ioctl(arg, {inSize ? resolve_(in, inSize) : nullptr, inSize},
                            {outSize ? resolve_(out, outSize) : nullptr, outSize}, out);
    if (handle->second.device == Device::KdTime && arg == 0x17) {
        if (inSize < 8 || outSize < 4) return -4;
        const auto* counter = resolve_(in, inSize);
        if (Read(counter+4) != 0)
            throw std::runtime_error("Persistent KD RTC update requires the NWC24 misc.bin format");
        networkRtcCounter_ = Read(counter);
        Write(resolve_(out, outSize), 0);
        return 0;
    }
    if (handle->second.device == Device::KdRequest || handle->second.device == Device::KdTime)
        throw std::runtime_error("Unimplemented " + std::string(handle->second.device == Device::KdRequest ? "KD request" : "KD time") +
                                 " ioctl=" + std::to_string(arg));
    if (handle->second.device == Device::StmImmediate && arg == 0x5001) {
        if (inSize != 32 || outSize != 32) return -4;
        const auto* input = resolve_(in, inSize);
        // RK5E01 __OSSetVIForceDimming sends eight BE words. Support the
        // startup request that keeps the native presentation at full brightness.
        if (Read(input+4) || Read(input+8) || Read(input+12) || Read(input+16) ||
            Read(input+20) != 0xFFFFFFFF || Read(input+24) != 0xFFFF0000 || Read(input+28)) return -4;
        if (Read(input))
            throw std::runtime_error("STM active video dimming needs a brightness adapter: control=" + std::to_string(Read(input)));
        videoDimmingDisabled_ = true;
        // The SDK completion only releases StmVdInUse. Like Dolphin, leave
        // the output payload untouched; no output register values are invented.
        return 0;
    }
    if (handle->second.device == Device::StmImmediate && arg == 0x6002) {
        if (inSize < 4) return -4;
        const auto mode = Read(resolve_(in, inSize));
        if (mode > 2) return -4;
        idleLedMode_ = mode; // Console slot LED policy; no matching host LED exists.
        return 0;
    }
    if (handle->second.device == Device::StmEventHook && arg == 0x1000) {
        if (outSize < 4) return -4;
        if (eventRequest_) return -2;
        if (!asynchronous)
            throw std::runtime_error("Synchronous STM EVENTHOOK requires a guest wait scheduler");
        eventRequest_ = address;
        eventOutput_ = out;
        eventHandle_ = fd;
        return std::nullopt;
    }
    if (handle->second.device == Device::StmImmediate && arg == 0x3002) {
        if (!eventRequest_) return -6;
        TriggerEvent(0);
        return 0;
    }
    throw std::runtime_error("Unimplemented STM ioctl: " + std::to_string(arg));
}

void IosServices::TriggerEvent(uint32_t event) {
    if (!eventRequest_) return;
    auto* output = resolve_(eventOutput_, 4);
    if (!output) throw std::runtime_error("STM pending output is no longer mapped");
    Write(output, event);
    completions_.push_back({eventRequest_, 0});
    eventRequest_ = eventOutput_ = 0;
    eventHandle_ = -1;
}

std::optional<IosServices::Completion> IosServices::TakeCompletion() {
    if (completions_.empty()) return std::nullopt;
    const auto result = completions_.front();
    completions_.pop_front();
    return result;
}
} // namespace Kirby
