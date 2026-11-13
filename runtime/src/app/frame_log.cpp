// SPDX-License-Identifier: GPL-3.0-only
#include "kirby/app/frame_log.h"
#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <unistd.h>

namespace Kirby {
FrameLog::FrameLog(const std::filesystem::path& path) {
    const int fd = open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    if (fd < 0) throw std::runtime_error(std::string("Cannot create frame log: ")+std::strerror(errno));
    file_.reset(fdopen(fd,"w"));
    if (!file_) { close(fd); throw std::runtime_error("Cannot open frame log stream"); }
    if (std::fputs("sequence,vi_field,start_ns,seal_start_ns,seal_end_ns,wait_end_ns,finish_ns,"
                   "surface_presents,readbacks,validation_bytes,draws,queued_pipelines\n",file_.get()) == EOF ||
        std::fflush(file_.get()) != 0) throw std::runtime_error("Cannot write frame log header");
}
void FrameLog::Write(const FrameMetrics& sample) {
    const std::array values{sample.sequence,sample.viField,sample.startNs,sample.sealStartNs,
        sample.sealEndNs,sample.waitEndNs,sample.finishNs,sample.surfacePresents,sample.readbacks,
        sample.validationBytes,sample.draws,sample.queuedPipelines};
    for (size_t i=0;i<values.size();++i)
        if (std::fprintf(file_.get(),"%" PRIu64 "%c",values[i],i+1==values.size() ? '\n' : ',') < 0)
            throw std::runtime_error("Cannot write frame log sample");
    if ((sample.sequence == 1 || sample.sequence%120 == 0) && std::fflush(file_.get()) != 0)
        throw std::runtime_error("Cannot flush frame log");
}
}
