// SPDX-License-Identifier: GPL-3.0-only
#include "fabric.h"
#include "language.h"
#include "platform.h"
#include <algorithm>
#include <cmath>

namespace Launcher {
void Fabric::Load(SDL_Renderer* renderer, const std::filesystem::path& root) {
    auto& io = ImGui::GetIO();
    auto font = root / "launcher/assets/launcher-ui.ttf";
    if (!std::filesystem::is_regular_file(font)) font = root / "launcher-ui.ttf";
    if (std::filesystem::is_regular_file(font)) {
        const auto name = Utf8(font);
        io.Fonts->AddFontFromFileTTF(name.c_str(), 18, nullptr, LauncherGlyphs());
        emphasis = io.Fonts->AddFontFromFileTTF(name.c_str(), 24, nullptr, LauncherGlyphs());
        static ImVector<ImWchar> titleRanges;
        ImFontGlyphRangesBuilder titles;
        titles.AddText("毛线卡比Kirby's Epic Yarn");
        titles.BuildRanges(&titleRanges);
        heading = io.Fonts->AddFontFromFileTTF(name.c_str(), 56, nullptr, titleRanges.Data);
    }
    const auto path = Utf8(root / "imgs/Kirbys_Epic_Yarn_Concept-Art.png");
    if (auto* surface = SDL_LoadPNG(path.c_str())) {
        illustration = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (illustration) SDL_SetTextureScaleMode(illustration, SDL_SCALEMODE_LINEAR);
    }
    ImGui::StyleColorsLight();
    auto& s = ImGui::GetStyle();
    s.WindowPadding = {22, 15}; s.FramePadding = {12, 7}; s.ItemSpacing = {10, 9};
    s.ChildRounding = 15; s.FrameRounding = 9; s.PopupRounding = 10; s.ScrollbarRounding = 8;
    s.WindowBorderSize = 0; s.ChildBorderSize = 1;
    auto color = [&](ImGuiCol slot, ImU32 value) { s.Colors[slot] = ImGui::ColorConvertU32ToFloat4(value); };
    color(ImGuiCol_Text, IM_COL32(46,64,54,255)); color(ImGuiCol_TextDisabled, IM_COL32(93,105,92,255));
    color(ImGuiCol_WindowBg, IM_COL32(247,244,231,255)); color(ImGuiCol_ChildBg, IM_COL32(255,252,242,255));
    color(ImGuiCol_PopupBg, IM_COL32(255,252,244,255)); color(ImGuiCol_Border, IM_COL32(209,205,184,255));
    color(ImGuiCol_Button, IM_COL32(231,234,215,255)); color(ImGuiCol_ButtonHovered, IM_COL32(215,230,211,255));
    color(ImGuiCol_ButtonActive, IM_COL32(190,216,195,255)); color(ImGuiCol_FrameBg, IM_COL32(234,238,223,255));
    color(ImGuiCol_FrameBgHovered, IM_COL32(220,234,215,255)); color(ImGuiCol_FrameBgActive, IM_COL32(203,225,206,255));
    color(ImGuiCol_Header, IM_COL32(248,219,221,255)); color(ImGuiCol_HeaderHovered, IM_COL32(246,206,216,255));
    color(ImGuiCol_HeaderActive, IM_COL32(235,185,200,255)); color(ImGuiCol_CheckMark, IM_COL32(150,65,95,255));
    color(ImGuiCol_SliderGrab, IM_COL32(174,86,113,255)); color(ImGuiCol_SliderGrabActive, IM_COL32(146,62,91,255));
    color(ImGuiCol_NavCursor, IM_COL32(155,68,98,255)); color(ImGuiCol_Separator, IM_COL32(205,208,187,255));
}

void Fabric::Close() {
    if (illustration) SDL_DestroyTexture(illustration);
}

void Stitch(ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 color) {
    const float dx = b.x - a.x, dy = b.y - a.y, length = std::hypot(dx, dy);
    for (float i = 0; i < length; i += 8) {
        const float end = std::min(i + 4, length);
        draw->AddLine({a.x + dx*i/length, a.y + dy*i/length},
                      {a.x + dx*end/length, a.y + dy*end/length}, color, 1.4f);
    }
}

void SewButton(ImDrawList* d, ImVec2 c, float r, ImU32 fill) {
    d->AddCircleFilled({c.x, c.y+2}, r, IM_COL32(111,92,81,90));
    d->AddCircleFilled(c, r, fill);
    d->AddCircle(c, r-3, IM_COL32(119,76,86,125), 0, 1.2f);
    const float hole = r * .25f;
    for (int i : {-1,1}) {
        d->AddLine({c.x-hole,c.y+i*hole}, {c.x+hole,c.y-i*hole}, IM_COL32(255,250,232,255), 2);
    }
}

void Quilt(const Fabric& fabric, ImVec2 p, ImVec2 size) {
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 end{p.x + size.x, p.y + size.y};
    d->AddRectFilled({p.x,p.y+3}, {end.x,end.y+3}, IM_COL32(189,196,170,255), 18);
    d->AddRectFilled(p, end, IM_COL32(216,236,219,255), 18);
    d->PushClipRect({p.x+6,p.y+6}, {end.x-6,end.y-6}, true);
    for (float x = p.x-size.y; x < end.x+size.y; x += 31) {
        d->AddLine({x,p.y}, {x+size.y,end.y}, IM_COL32(129,173,147,34), 2);
        d->AddLine({x,end.y}, {x+size.y,p.y}, IM_COL32(129,173,147,34), 2);
    }
    if (fabric.illustration) {
        const float scale = std::min(size.x*.55f / fabric.illustration->w, (size.y+6) / fabric.illustration->h);
        const float w = fabric.illustration->w * scale, h = fabric.illustration->h * scale;
        d->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(fabric.illustration)),
                    {end.x-10-w,end.y-h}, {end.x-10,end.y});
    } else {
        // A stitched spool and loose pink yarn also work in packages without the concept art.
        const ImVec2 ball{p.x+size.x*.78f,p.y+size.y*.56f};
        const float r = std::min(size.x*.12f,size.y*.29f);
        d->AddBezierCubic({ball.x-r*.6f,ball.y+r*.65f},{ball.x-r*2,ball.y+r*1.8f},
                          {ball.x+r*.6f,ball.y+r*1.6f},{end.x+7,ball.y-r*.2f},IM_COL32(211,116,146,255),5);
        d->AddCircleFilled(ball,r,IM_COL32(231,146,167,255));
        for (float y : {-.65f,-.35f,-.05f,.25f,.55f}) {
            const float x = r * std::sqrt(1-y*y);
            d->AddBezierCubic({ball.x-x,ball.y+y*r},{ball.x-r*.35f,ball.y+(y-.53f)*r},
                              {ball.x+r*.4f,ball.y+(y+.5f)*r},{ball.x+x,ball.y+y*r},
                              IM_COL32(255,211,219,235),3);
        }
        SewButton(d,{ball.x+r*.9f,ball.y-r*.8f},r*.31f,IM_COL32(239,194,99,255));
    }
    d->PopClipRect();
    const auto edge = IM_COL32(132,169,143,220);
    for (float y : {p.y+8,end.y-8}) Stitch(d,{p.x+18,y},{end.x-18,y},edge);
    for (float x : {p.x+8,end.x-8}) Stitch(d,{x,p.y+18},{x,end.y-18},edge);
}
}
