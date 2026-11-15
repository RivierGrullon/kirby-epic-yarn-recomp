// SPDX-License-Identifier: GPL-3.0-only
#include "presentation_log.h"
#include "wayland_presentation.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    if (argc!=2) return 2;
    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::cerr<<SDL_GetError()<<'\n';return 1; }
    struct Cleanup { SDL_Window* window=nullptr;~Cleanup() { if (window) SDL_DestroyWindow(window);SDL_Quit(); } } cleanup;
    try {
        cleanup.window=SDL_CreateWindow("Kirby presentation protocol probe",64,64,SDL_WINDOW_HIDDEN);
        if (!cleanup.window || !(SDL_GetWindowFlags(cleanup.window)&SDL_WINDOW_HIDDEN))
            throw std::runtime_error("Protocol probe must keep its window hidden");
        Kirby::PresentationLog log(argv[1]);
        Kirby::WaylandPresentationTrace trace(cleanup.window,log);
        trace.Request(1);trace.Finish();log.Finish();
        std::cout<<"Real Wayland registry/clock, hidden surface request and pending cleanup verified; no buffer was presented\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
