// SPDX-License-Identifier: GPL-3.0-only
#include "presentation_log.h"
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace Kirby {
PresentationLog::PresentationLog(const std::filesystem::path& path) {
#ifdef _WIN32
    const int fd=_wopen(path.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY|_O_NOINHERIT,_S_IREAD|_S_IWRITE);
    if (fd<0) throw std::runtime_error(std::string("Cannot create presentation log: ")+std::strerror(errno));
    file_.reset(_fdopen(fd,"wb"));
    if (!file_) { _close(fd);throw std::runtime_error("Cannot open presentation log stream"); }
#else
    const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (fd<0) throw std::runtime_error(std::string("Cannot create presentation log: ")+std::strerror(errno));
    file_.reset(fdopen(fd,"wb"));
    if (!file_) { close(fd);throw std::runtime_error("Cannot open presentation log stream"); }
#endif
    if (std::setvbuf(file_.get(),buffer_.data(),_IOFBF,buffer_.size())!=0)
        throw std::runtime_error("Cannot buffer presentation log");
    if (std::fputs("event,request,request_sdl_ns,callback_sdl_ns,present_ns,refresh_ns,sequence,flags,clock_id,clock_ns,sdl_before_ns,sdl_after_ns\n",file_.get())<0 || std::fflush(file_.get()))
        throw std::runtime_error("Cannot write presentation log header");
}
PresentationLog::~PresentationLog() {
    try { Finish(); }
    catch (const std::exception& error) { std::fprintf(stderr,"[kirby] presentation log finalization failed: %s\n",error.what()); }
}
void PresentationLog::Write(const PresentationRecord& r) {
    if (!file_) throw std::runtime_error("Presentation log is closed");
    const char* event=nullptr;
    switch (r.event) {
    case PresentationEvent::Clock:event="clock";break;
    case PresentationEvent::Presented:event="presented";break;
    case PresentationEvent::Discarded:event="discarded";break;
    case PresentationEvent::Pending:event="pending";break;
    }
    if (!event) throw std::runtime_error("Unknown presentation event");
    if (std::fprintf(file_.get(),"%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu32 ",%" PRIu64 ",%" PRIu32 ",%" PRId32 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
        event,r.request,r.requestSdlNs,r.callbackSdlNs,r.presentNs,r.refreshNs,r.sequence,r.flags,
        r.clockId,r.clockNs,r.sdlBeforeNs,r.sdlAfterNs)<0)
        throw std::runtime_error("Cannot write presentation feedback");
    // Flush the first feedback events for startup probes, then every second.
    if (++rows_<=5 || rows_%60==0) {
        if (std::fflush(file_.get())) throw std::runtime_error("Cannot flush presentation feedback");
    }
}
void PresentationLog::Finish() {
    if (file_ && std::fclose(file_.release())) throw std::runtime_error("Cannot finalize presentation log");
}
}
