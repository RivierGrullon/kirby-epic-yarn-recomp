// SPDX-License-Identifier: GPL-3.0-only
#include "wayland_presentation.h"
#include "presentation_log.h"
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <array>
#include <cstring>
#include <exception>
#include <stdexcept>
#ifdef KIRBY_HAVE_WAYLAND_PRESENTATION
#include <wayland-client.h>
#include <time.h>
#include "kirby-presentation-time.h"
#endif

namespace Kirby {
#ifdef KIRBY_HAVE_WAYLAND_PRESENTATION
struct WaylandPresentationTrace::Impl {
    PresentationLog& log;
    wl_display* display=nullptr;
    wl_surface* surface=nullptr;
    wl_event_queue* queue=nullptr;
    wl_registry* registry=nullptr;
    wp_presentation* presentation=nullptr;
    std::exception_ptr error;
    bool hasClock=false,finished=false;
    int32_t clockId=0;
    struct Slot {
        Impl* owner=nullptr;
        struct wp_presentation_feedback* feedback=nullptr;
        uint64_t index=0,requested=0;
    };
    std::array<Slot,128> slots{};

    explicit Impl(PresentationLog& output):log(output) {}
    ~Impl() { Close(); }
    void Close() {
        for (auto& slot:slots) if (slot.feedback) {
            wp_presentation_feedback_destroy(slot.feedback);slot.feedback=nullptr;
        }
        if (presentation) { wp_presentation_destroy(presentation);presentation=nullptr; }
        if (registry) { wl_registry_destroy(registry);registry=nullptr; }
        if (queue) { wl_event_queue_destroy(queue);queue=nullptr; }
    }
    static void Global(void* data,wl_registry* reg,uint32_t name,const char* interface,uint32_t version) {
        auto& self=*static_cast<Impl*>(data);
        if (!self.presentation && version && std::strcmp(interface,wp_presentation_interface.name)==0)
            self.presentation=static_cast<wp_presentation*>(wl_registry_bind(reg,name,&wp_presentation_interface,1));
    }
    static void Removed(void*,wl_registry*,uint32_t) {}
    static void Clock(void* data,wp_presentation*,uint32_t id) {
        auto& self=*static_cast<Impl*>(data);self.clockId=static_cast<int32_t>(id);self.hasClock=true;
    }
    static void SyncOutput(void*,struct wp_presentation_feedback*,wl_output*) {}
    static void Presented(void* data,struct wp_presentation_feedback* feedback,uint32_t hi,uint32_t lo,
        uint32_t nsec,uint32_t refresh,uint32_t seqHi,uint32_t seqLo,uint32_t flags) {
        auto& slot=*static_cast<Slot*>(data);auto& self=*slot.owner;
        try {
            PresentationRecord row{};row.event=PresentationEvent::Presented;
            row.request=slot.index;row.requestSdlNs=slot.requested;row.callbackSdlNs=SDL_GetTicksNS();
            const uint64_t seconds=(uint64_t(hi)<<32)|lo;
            if (nsec>=1000000000 || seconds>(UINT64_MAX-nsec)/1000000000)
                throw std::runtime_error("Invalid Wayland presentation timestamp");
            row.presentNs=seconds*1000000000+nsec;row.refreshNs=refresh;
            row.sequence=(uint64_t(seqHi)<<32)|seqLo;row.flags=flags;self.log.Write(row);
        } catch (...) { self.error=std::current_exception(); }
        wp_presentation_feedback_destroy(feedback);slot.feedback=nullptr;
    }
    static void Discarded(void* data,struct wp_presentation_feedback* feedback) {
        auto& slot=*static_cast<Slot*>(data);auto& self=*slot.owner;
        try {
            PresentationRecord row{};row.event=PresentationEvent::Discarded;
            row.request=slot.index;row.requestSdlNs=slot.requested;row.callbackSdlNs=SDL_GetTicksNS();
            self.log.Write(row);
        } catch (...) { self.error=std::current_exception(); }
        wp_presentation_feedback_destroy(feedback);slot.feedback=nullptr;
    }
    void Initialize(SDL_Window* window) {
        const char* driver=SDL_GetCurrentVideoDriver();
        if (!driver || std::strcmp(driver,"wayland"))
            throw std::runtime_error("Presentation feedback requires the SDL Wayland driver");
        const auto properties=SDL_GetWindowProperties(window);
        display=static_cast<wl_display*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,nullptr));
        surface=static_cast<wl_surface*>(SDL_GetPointerProperty(properties,SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,nullptr));
        if (!display || !surface) throw std::runtime_error("SDL window has no Wayland display/surface");
        queue=wl_display_create_queue(display);
        if (!queue) throw std::runtime_error("Cannot create presentation feedback queue");
        auto* wrapper=static_cast<wl_display*>(wl_proxy_create_wrapper(display));
        if (!wrapper) throw std::runtime_error("Cannot wrap Wayland display");
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper),queue);
        registry=wl_display_get_registry(wrapper);wl_proxy_wrapper_destroy(wrapper);
        if (!registry) throw std::runtime_error("Cannot request Wayland registry");
        static const wl_registry_listener globals{Global,Removed};
        wl_registry_add_listener(registry,&globals,this);
        if (wl_display_roundtrip_queue(display,queue)<0 || !presentation)
            throw std::runtime_error("Compositor does not provide wp_presentation");
        static const wp_presentation_listener listener{Clock};
        wp_presentation_add_listener(presentation,&listener,this);
        if (wl_display_roundtrip_queue(display,queue)<0 || !hasClock)
            throw std::runtime_error("Compositor did not report its presentation clock");
        PresentationRecord row{};row.event=PresentationEvent::Clock;row.clockId=clockId;
        timespec time{};row.sdlBeforeNs=SDL_GetTicksNS();
        if (clock_gettime(clockId,&time)) throw std::runtime_error("Cannot sample the compositor presentation clock");
        row.sdlAfterNs=SDL_GetTicksNS();row.clockNs=uint64_t(time.tv_sec)*1000000000+uint64_t(time.tv_nsec);
        log.Write(row);
    }
    void Poll() {
        if (finished) return;
        if (wl_display_dispatch_queue_pending(display,queue)<0)
            throw std::runtime_error("Wayland presentation connection failed");
        if (error) std::rethrow_exception(error);
    }
    void Request(uint64_t index) {
        if (finished) throw std::runtime_error("Presentation trace is closed");
        Poll();
        for (auto& slot:slots) if (!slot.feedback) {
            slot.owner=this;slot.index=index;slot.requested=SDL_GetTicksNS();
            slot.feedback=wp_presentation_feedback(presentation,surface);
            if (!slot.feedback) throw std::runtime_error("Cannot request presentation feedback");
            static const wp_presentation_feedback_listener listener{SyncOutput,Presented,Discarded};
            wp_presentation_feedback_add_listener(slot.feedback,&listener,&slot);
            return;
        }
        throw std::runtime_error("Presentation feedback queue exceeded 128 unresolved requests");
    }
    void Finish() {
        if (finished) return;
        Poll();
        for (auto& slot:slots) if (slot.feedback) {
            PresentationRecord row{};row.event=PresentationEvent::Pending;
            row.request=slot.index;row.requestSdlNs=slot.requested;row.callbackSdlNs=SDL_GetTicksNS();log.Write(row);
        }
        finished=true;Close();
    }
};
WaylandPresentationTrace::WaylandPresentationTrace(SDL_Window* window,PresentationLog& log):impl_(std::make_unique<Impl>(log)) { impl_->Initialize(window); }
WaylandPresentationTrace::~WaylandPresentationTrace()=default;
void WaylandPresentationTrace::Poll() { impl_->Poll(); }
void WaylandPresentationTrace::Request(uint64_t index) { impl_->Request(index); }
void WaylandPresentationTrace::Finish() { impl_->Finish(); }
#else
struct WaylandPresentationTrace::Impl {};
WaylandPresentationTrace::WaylandPresentationTrace(SDL_Window*,PresentationLog&) {
    throw std::runtime_error("Wayland presentation tracing is unavailable in this build");
}
WaylandPresentationTrace::~WaylandPresentationTrace()=default;
void WaylandPresentationTrace::Poll() {}
void WaylandPresentationTrace::Request(uint64_t) {}
void WaylandPresentationTrace::Finish() {}
#endif
}
