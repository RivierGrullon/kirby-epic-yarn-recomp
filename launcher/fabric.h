// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace Launcher {
struct Fabric {
    SDL_Texture* illustration = nullptr;
    ImFont* heading = nullptr;
    ImFont* emphasis = nullptr;
    void Load(SDL_Renderer* renderer, const std::filesystem::path& root);
    void Close();
};
void Stitch(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color);
void SewButton(ImDrawList* draw, ImVec2 center, float radius, ImU32 color);
void Quilt(const Fabric& fabric, ImVec2 corner, ImVec2 size);
}
