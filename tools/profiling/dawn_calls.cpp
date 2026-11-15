// SPDX-License-Identifier: GPL-3.0-only
// Optional Linux profiling wrappers; WebGPU declarations come from pinned Dawn.
// Calls retain their original arguments and return values. No game data is logged.
#include <SDL3/SDL_timer.h>
#include <webgpu/webgpu.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <sys/syscall.h>
#include <time.h>
#include <type_traits>
#include <unistd.h>
namespace {
uint64_t cpu_time() {
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return uint64_t(value.tv_sec) * 1'000'000'000 + value.tv_nsec;
}

struct Output {
    FILE* file = nullptr;
    std::mutex mutex;
    uint64_t sequence = 0;

    Output() {
        const char* path = std::getenv("KIRBY_DAWN_RESOURCE_LOG");
        if (!path || !*path) return;
        const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) {
            std::fprintf(stderr, "[resource-trace] Cannot create exclusive trace: errno=%d\n", errno);
            return;
        }
        file = fdopen(fd, "w");
        if (!file) { close(fd); return; }
        setvbuf(file, nullptr, _IOFBF, 65536);
        std::fputs("sequence,operation,start_ns,end_ns,cpu_ns,tid,width,height,amount,result\n", file);
        std::fflush(file);
    }

    ~Output() { if (file) std::fclose(file); }
};

Output& output() {
    static Output result;
    return result;
}

struct Span {
    Output& out;
    const char* operation;
    uint32_t width, height;
    uint64_t amount, result = 0, start = 0, cpu = 0;
    long tid = 0;

    Span(const char* name, uint32_t w, uint32_t h, uint64_t n)
        : out(output()), operation(name), width(w), height(h), amount(n) {
        if (out.file) {
            tid = syscall(SYS_gettid);
            start = SDL_GetTicksNS();
            cpu = cpu_time();
        }
    }

    ~Span() {
        if (!out.file) return;
        const auto used = cpu_time() - cpu;
        const auto end = SDL_GetTicksNS();
        // File-lock and write costs are excluded from this call's duration.
        // They still affect the instrumented program's overall frame timing.
        std::lock_guard lock(out.mutex);
        std::fprintf(out.file, "%llu,%s,%llu,%llu,%llu,%ld,%u,%u,%llu,%llu\n",
            (unsigned long long)++out.sequence, operation, (unsigned long long)start, (unsigned long long)end,
            (unsigned long long)used, tid, width, height, (unsigned long long)amount, (unsigned long long)result);
        if (!(out.sequence % 64)) std::fflush(out.file);
    }
};

template<class Function>
auto trace(const char* name, uint32_t w, uint32_t h, uint64_t n, Function&& fn) {
    Span span(name, w, h, n);
    if constexpr (std::is_void_v<decltype(fn())>) {
        fn();
    } else {
        auto result = fn();
        if constexpr (std::is_pointer_v<decltype(result)>) span.result = reinterpret_cast<uintptr_t>(result);
        else span.result = static_cast<uint64_t>(result);
        return result;
    }
}
} // namespace
extern "C" {
WGPUTexture __real_wgpuDeviceCreateTexture(WGPUDevice,WGPUTextureDescriptor const*);
WGPUTexture __wrap_wgpuDeviceCreateTexture(WGPUDevice d,WGPUTextureDescriptor const* p) {
 return trace("CreateTexture",p?p->size.width:0,p?p->size.height:0,p?p->format:0,[&]{return __real_wgpuDeviceCreateTexture(d,p);}); }
WGPUTextureView __real_wgpuTextureCreateView(WGPUTexture,WGPUTextureViewDescriptor const*);
WGPUTextureView __wrap_wgpuTextureCreateView(WGPUTexture t,WGPUTextureViewDescriptor const* p) {
 return trace("CreateView",0,0,0,[&]{return __real_wgpuTextureCreateView(t,p);}); }
WGPUBindGroup __real_wgpuDeviceCreateBindGroup(WGPUDevice,WGPUBindGroupDescriptor const*);
WGPUBindGroup __wrap_wgpuDeviceCreateBindGroup(WGPUDevice d,WGPUBindGroupDescriptor const* p) {
 return trace("CreateBindGroup",0,0,p?p->entryCount:0,[&]{return __real_wgpuDeviceCreateBindGroup(d,p);}); }
WGPUSampler __real_wgpuDeviceCreateSampler(WGPUDevice,WGPUSamplerDescriptor const*);
WGPUSampler __wrap_wgpuDeviceCreateSampler(WGPUDevice d,WGPUSamplerDescriptor const* p) {
 return trace("CreateSampler",0,0,0,[&]{return __real_wgpuDeviceCreateSampler(d,p);}); }
WGPUBuffer __real_wgpuDeviceCreateBuffer(WGPUDevice,WGPUBufferDescriptor const*);
WGPUBuffer __wrap_wgpuDeviceCreateBuffer(WGPUDevice d,WGPUBufferDescriptor const* p) {
 return trace("CreateBuffer",0,0,p?p->size:0,[&]{return __real_wgpuDeviceCreateBuffer(d,p);}); }
WGPUShaderModule __real_wgpuDeviceCreateShaderModule(WGPUDevice,WGPUShaderModuleDescriptor const*);
WGPUShaderModule __wrap_wgpuDeviceCreateShaderModule(WGPUDevice d,WGPUShaderModuleDescriptor const* p) {
 return trace("CreateShaderModule",0,0,0,[&]{return __real_wgpuDeviceCreateShaderModule(d,p);}); }
WGPURenderPipeline __real_wgpuDeviceCreateRenderPipeline(WGPUDevice,WGPURenderPipelineDescriptor const*);
WGPURenderPipeline __wrap_wgpuDeviceCreateRenderPipeline(WGPUDevice d,WGPURenderPipelineDescriptor const* p) {
 return trace("CreateRenderPipeline",0,0,0,[&]{return __real_wgpuDeviceCreateRenderPipeline(d,p);}); }
WGPUCommandBuffer __real_wgpuCommandEncoderFinish(WGPUCommandEncoder,WGPUCommandBufferDescriptor const*);
WGPUCommandBuffer __wrap_wgpuCommandEncoderFinish(WGPUCommandEncoder e,WGPUCommandBufferDescriptor const* p) {
 return trace("FinishEncoder",0,0,0,[&]{return __real_wgpuCommandEncoderFinish(e,p);}); }
void __real_wgpuQueueSubmit(WGPUQueue,size_t,WGPUCommandBuffer const*);
void __wrap_wgpuQueueSubmit(WGPUQueue q,size_t n,WGPUCommandBuffer const* p) {
 trace("QueueSubmit",0,0,n,[&]{__real_wgpuQueueSubmit(q,n,p);}); }
void __real_wgpuQueueWriteBuffer(WGPUQueue,WGPUBuffer,uint64_t,void const*,size_t);
void __wrap_wgpuQueueWriteBuffer(WGPUQueue q,WGPUBuffer b,uint64_t o,void const* p,size_t n) {
 trace("WriteBuffer",0,0,n,[&]{__real_wgpuQueueWriteBuffer(q,b,o,p,n);}); }
void __real_wgpuQueueWriteTexture(WGPUQueue,WGPUTexelCopyTextureInfo const*,void const*,size_t,WGPUTexelCopyBufferLayout const*,WGPUExtent3D const*);
void __wrap_wgpuQueueWriteTexture(WGPUQueue q,WGPUTexelCopyTextureInfo const* d,void const* p,size_t n,WGPUTexelCopyBufferLayout const* l,WGPUExtent3D const* s) {
 trace("WriteTexture",s?s->width:0,s?s->height:0,n,[&]{__real_wgpuQueueWriteTexture(q,d,p,n,l,s);}); }
void __real_wgpuSurfaceGetCurrentTexture(WGPUSurface,WGPUSurfaceTexture*);
void __wrap_wgpuSurfaceGetCurrentTexture(WGPUSurface s,WGPUSurfaceTexture* t) {
 trace("AcquireSurface",0,0,0,[&]{__real_wgpuSurfaceGetCurrentTexture(s,t);}); }
WGPUStatus __real_wgpuSurfacePresent(WGPUSurface);
WGPUStatus __wrap_wgpuSurfacePresent(WGPUSurface s) {
 return trace("PresentSurface",0,0,0,[&]{return __real_wgpuSurfacePresent(s);}); }
void __real_aurora_wait_for_frame_worker();
void __wrap_aurora_wait_for_frame_worker() {
 trace("FrameWorkerWait",0,0,0,[&]{__real_aurora_wait_for_frame_worker();}); }
}
