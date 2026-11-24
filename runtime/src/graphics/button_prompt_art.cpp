// SPDX-License-Identifier: GPL-3.0-only
#include "button_prompt_art.h"
#include "kirby/graphics/button_prompt_textures.h"
#include "gfx/png_io.hpp"
#include "gfx/hash.hpp"
#include <aurora/texture.hpp>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <bit>
#include <iostream>
#include <map>
#include <tuple>

namespace Kirby::PromptArt {
namespace {
struct Icon {
    std::string_view name;
    std::span<const uint8_t> png;
};
#include "button_icons.inc"

using ActionSet = unsigned;
constexpr ActionSet Bit(GameAction action) { return 1u << static_cast<unsigned>(action); }
constexpr auto jump = Bit(GameAction::Jump), yarn = Bit(GameAction::Yarn);
constexpr auto wiiA = Bit(GameAction::WiiA), wiiB = Bit(GameAction::WiiB);
constexpr auto pause = Bit(GameAction::Pause), map = Bit(GameAction::Map), home = Bit(GameAction::Home);
constexpr auto up = Bit(GameAction::Up), down = Bit(GameAction::Down);
constexpr auto left = Bit(GameAction::Left), right = Bit(GameAction::Right);
constexpr auto horizontal = left | right, vertical = up | down, directions = horizontal | vertical;

enum Layer { RGBA, Mask, Ink, Badge, HotBadge, Dim, Lit, Paper };
struct Region {
    ActionSet actions;
    unsigned x, y, width, height;
    Layer layer;
};
struct Texture {
    uint64_t hash;
    uint32_t sample;
    unsigned width, height, format;
    std::string_view label;
    std::initializer_list<Region> regions;
};
constexpr Texture catalog[] = {
#include "button_prompt_catalog.inc"
};

// A malformed atlas rectangle could overwrite adjacent text or the upload buffer.
static_assert([] {
    for (const auto& texture : catalog) {
        for (const auto& region : texture.regions) {
            if (!region.actions || !region.width || !region.height ||
                region.x + region.width > texture.width || region.y + region.height > texture.height)
                return false;
        }
    }
    return true;
}());

using Registration = aurora::texture::ReplacementRegistration;
struct Sheet {
    const Texture& spec;
    std::vector<uint8_t> original, composed;
    Registration registration{};
};

struct CatalogState {
    using Layout = std::tuple<unsigned, unsigned, unsigned>;
    std::map<Layout, std::vector<const Texture*>> pending;
    std::vector<Sheet> sheets;
    Glyphs glyphs;
    Image dpad;
};
std::optional<CatalogState> state;

using Pixel = std::array<uint8_t,4>;
Pixel Sample(const Image& image, float u, float v) {
    if (image.pixels.empty() || u < 0 || v < 0 || u >= 1 || v >= 1) return {};
    const auto x = static_cast<unsigned>(u * image.width);
    const auto y = static_cast<unsigned>(v * image.height);
    Pixel pixel;
    std::copy_n(image.pixels.data() + 4 * (y * image.width + x), 4, pixel.begin());
    return pixel;
}

Pixel Symbol(ActionSet actions, float u, float v) {
    if (std::has_single_bit(actions))
        return Sample(state->glyphs[std::countr_zero(actions)], u, v);
    if (actions == directions && !state->dpad.pixels.empty()) return Sample(state->dpad, u, v);
    // For remapped directions show the actual keys in their compass positions.
    for (const auto [action, x, y] : {std::tuple{up, .5f, .25f}, {down, .5f, .75f},
                                      {left, .25f, .5f}, {right, .75f, .5f}}) {
        if (actions & action) {
            const float size = actions == directions ? .46f : .5f;
            auto pixel = Sample(state->glyphs[std::countr_zero(action)], (u-x)/size+.5f, (v-y)/size+.5f);
            if (pixel[3]) return pixel;
        }
    }
    return {};
}

bool HasGlyphs(ActionSet actions) {
    for (size_t i = 0; i < state->glyphs.size(); ++i)
        if ((actions & (1u << i)) && state->glyphs[i].pixels.empty()) return false;
    return true;
}

void Paint(Sheet& sheet, const Region& r) {
    for (unsigned y = 0; y < r.height; ++y) {
        for (unsigned x = 0; x < r.width; ++x) {
            float u = (x + .5f) / r.width, v = (y + .5f) / r.height;
            auto pixel = Symbol(r.actions, u, v);
            const unsigned alpha = pixel[3];
            switch (r.layer) {
            case Ink: { // The font material colors the I4 coverage, including its shadow pass.
                const auto ink = static_cast<uint8_t>((pixel[0]+pixel[1]+pixel[2])*alpha / (3*255));
                pixel.fill(ink);
                break;
            }
            case Mask: pixel.fill(alpha); break;
            case HotBadge: case Badge:
                // MiniHelp uses one circular intensity texture for five different
                // color maps. Composite onto its dark rim, keeping the shared shadow.
                for (unsigned c=0; c<3; ++c) pixel[c] = pixel[c] * alpha / 255;
                pixel[3] = 255;
                if (r.layer == HotBadge) { pixel[1] /= 2; pixel[2] /= 2; }
                break;
            case Dim: case Lit:
                // The tutorial projector multiplies RGB onto the cloth. Original
                // RGB5A3 texels are white even where their alpha is zero.
                if (r.layer == Lit) {
                    pixel[1] = pixel[1] * 3 / 4;
                    pixel[2] /= 2;
                } else {
                    pixel[3] = alpha * 2 / 3;
                }
                for (unsigned c = 0; c < 3; ++c)
                    pixel[c] = (pixel[c] * pixel[3] + 255 * (255 - pixel[3])) / 255;
                break;
            case Paper:
                for (unsigned c=0; c<3; ++c) pixel[c] = (pixel[c]*alpha + 255*(255-alpha)) / 255;
                pixel[3] = 255;
                break;
            case RGBA: break;
            }
            const size_t pos = 4 * ((r.y + y) * sheet.spec.width + r.x + x);
            std::copy(pixel.begin(), pixel.end(), sheet.composed.begin()+pos);
        }
    }
}

void Update(Sheet& sheet) {
    const auto& spec = sheet.spec;
    if (sheet.registration.id) aurora::texture::unregister_replacement(sheet.registration);
    sheet.registration = {};
    sheet.composed = sheet.original;
    bool changed = false;
    for (const auto& region : spec.regions) {
        if (!HasGlyphs(region.actions)) continue;
        Paint(sheet, region);
        changed = true;
    }
    if (changed) {
        aurora::texture::TextureSourceKey key{.textureHash=spec.hash, .width=spec.width,
                                              .height=spec.height, .format=spec.format};
        sheet.registration = aurora::texture::register_replacement(key,
            {sheet.composed, spec.width, spec.height, 1, GX_TF_RGBA8_PC, spec.label});
    }
}

void Observe(unsigned format, unsigned width, unsigned height, std::span<const uint8_t> source) {
    const auto group = state->pending.find({format,width,height});
    if (group == state->pending.end()) return;
    // These GX formats use complete blocks at the right and bottom edges.
    const unsigned blockWidth = format == GX_TF_RGB5A3 ? 4 : 8;
    const unsigned blockHeight = format == GX_TF_IA4 ? 4 : blockWidth;
    const size_t size = ((width+blockWidth-1)/blockWidth) * ((height+blockHeight-1)/blockHeight) * 32;
    if (source.size() < size) return;
    auto& entries = group->second;
    // Screen-sized atlases and small UI textures share formats with many other
    // assets. Reject those cheaply before hashing the complete base level.
    const uint32_t tag = (uint32_t(source[size/4]) << 24) | (uint32_t(source[size/2+3]) << 16)
                       | (uint32_t(source[size*3/4+7]) << 8) | source[size/3];
    if (std::ranges::none_of(entries, [tag](const Texture* t) { return tag == t->sample; })) return;
    const uint64_t hash = XXH64(source.data(), size, 0);
    const auto match = std::find_if(entries.begin(), entries.end(),
                                    [hash](const Texture* t) { return t->hash == hash; });
    if (match == entries.end()) return;
    const Texture& spec = **match;
    const auto image = aurora::gfx::convert_texture(format,width,height,1,{source.data(),size});
    if (image.data.size() != width*height*4) return;
    auto& sheet = state->sheets.emplace_back(Sheet{spec, {image.data.data(),image.data.data()+image.data.size()}, {}});
    Update(sheet);
    std::cout << "[kirby] button prompt texture: " << spec.label << '\n';
    entries.erase(match);
    if (entries.empty()) state->pending.erase(group);
    if (state->pending.empty()) observeButtonPromptTexture = nullptr;
}

Image Label(std::string_view label) {
    if (label.empty()) return {};
    const int width = std::max<int>(16, 8*label.size()+4);
    SDL_Surface* surface = SDL_CreateSurface(width,width,SDL_PIXELFORMAT_RGBA32);
    if (!surface) return {};
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    Image image;
    if (renderer) {
        SDL_SetRenderDrawColor(renderer,0,0,0,0);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer,255,255,255,255);
        SDL_FRect box{0.f, (width-14)*.5f, float(width), 14.f};
        SDL_RenderFillRect(renderer,&box);
        SDL_SetRenderDrawColor(renderer,0,0,0,255);
        SDL_RenderDebugText(renderer,(width-8*label.size())*.5f,(width-8)*.5f,std::string(label).c_str());
        SDL_RenderPresent(renderer);
        image.width = image.height = width;
        for (int y=0; y<width; ++y) {
            auto* row = static_cast<uint8_t*>(surface->pixels) + y*surface->pitch;
            image.pixels.insert(image.pixels.end(),row,row+4*width);
        }
        SDL_DestroyRenderer(renderer);
    }
    SDL_DestroySurface(surface);
    return image;
}
}

Image Load(std::string_view name, std::string_view fallback) {
    const auto found = std::ranges::find(icons, name, &Icon::name);
    if (found == std::end(icons)) {
        const auto pos = name.find("/dpad_");
        if (pos == std::string_view::npos) return Label(fallback);
        auto image = Load(std::string(name.substr(0,pos)) + "/dpad", {});
        const auto direction = name.substr(pos+6);
        for (unsigned y = 0; y < image.height; ++y) {
            for (unsigned x = 0; x < image.width; ++x) {
                const int dx = int(2*x)-int(image.width), dy = int(2*y)-int(image.height);
                const auto arm = std::abs(dx)>std::abs(dy) ? (dx<0 ? "left" : "right") : (dy<0 ? "up" : "down");
                if (direction == arm) continue;
                auto* pixel = image.pixels.data() + 4*(y*image.width+x);
                for (unsigned channel=0; channel<3; ++channel) pixel[channel] /= 3;
            }
        }
        return image;
    }
    const auto image = aurora::gfx::png::parse_png_bytes({found->png.data(),found->png.size()});
    if (!image) return Label(fallback);
    return {image->width,image->height,{image->data.data(),image->data.data()+image->data.size()}};
}
void Start() {
    state.emplace();
    for (const auto& texture : catalog)
        state->pending[{texture.format,texture.width,texture.height}].push_back(&texture);
    observeButtonPromptTexture = Observe;
}
void Apply(const Glyphs& glyphs, const Image& dpad) {
    state->glyphs = glyphs;
    state->dpad = dpad;
    for (auto& sheet : state->sheets) Update(sheet);
}
void Stop() {
    observeButtonPromptTexture = nullptr;
    if (!state) return;
    for (auto& sheet : state->sheets)
        if (sheet.registration.id) aurora::texture::unregister_replacement(sheet.registration);
    state.reset();
}
}
