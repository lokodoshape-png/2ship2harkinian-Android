#include "ButtonPrompts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>
#include <ship/controller/controldevice/controller/mapping/ControllerMapping.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/Archive.h>
#include <ship/window/gui/resource/Font.h>
#include <fast/resource/ResourceType.h>

#include "2s2h/BenGui/HudEditor.h"
#include "2s2h/BenGui/Translation.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
Gfx* Gfx_DrawTexRectIA8(Gfx* gfx, TexturePtr texture, s16 textureWidth, s16 textureHeight, s16 rectLeft, s16 rectTop,
                        s16 rectWidth, s16 rectHeight, u16 dsdx, u16 dtdy);
}

#define CVAR_STYLE "gEnhancements.ButtonPrompts.Style"
#define CVAR_HUD_BADGES "gEnhancements.ButtonPrompts.HudBadges"

namespace {

// Same order as the style combobox in the menu.
enum class Style { Auto, N64, Xbox, PlayStation, Nintendo, Keyboard };

constexpr int kScale = 4; // every icon texture has 4x the texels of the slot it is drawn in
constexpr int kIcon = 16 * kScale;

// ------------------------------------------------------------------ which device and which buttons

Style sLastDevice = Style::Keyboard;
bool sLastDeviceKnown = false;

Style StyleOfGamepad(SDL_GameController* gamepad) {
    const char* name = SDL_GameControllerName(gamepad);
    if (name != nullptr) {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.find("n64") != std::string::npos || lower.find("nintendo 64") != std::string::npos) {
            return Style::N64;
        }
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    switch (SDL_GameControllerGetType(gamepad)) {
        case SDL_CONTROLLER_TYPE_PS3:
        case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
        case SDL_CONTROLLER_TYPE_PS5:
#endif
            return Style::PlayStation;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        case SDL_CONTROLLER_TYPE_VIRTUAL:
            // The Android touch controls, which are laid out like an N64 controller.
            return Style::N64;
#endif
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
            return Style::Nintendo;
        default:
            break;
    }
#endif
    return Style::Xbox;
}

std::unordered_map<int32_t, SDL_GameController*> Gamepads() {
    return Ship::Context::GetInstance()
        ->GetControlDeck()
        ->GetConnectedPhysicalDeviceManager()
        ->GetConnectedSDLGamepadsForPort(0);
}

// Remembers the last device that was used, for the automatic style.
void PollLastDevice() {
    auto gamepads = Gamepads();
    if (!sLastDeviceKnown) {
        sLastDeviceKnown = true;
#ifdef __ANDROID__
        // The touch controls are laid out like an N64 controller.
        sLastDevice = Style::N64;
#else
        sLastDevice = Style::Keyboard;
#endif
        // A real controller wins over the touch overlay's virtual one.
        for (const auto& [id, gamepad] : gamepads) {
            sLastDevice = StyleOfGamepad(gamepad);
#if SDL_VERSION_ATLEAST(2, 0, 14)
            if (SDL_GameControllerGetType(gamepad) != SDL_CONTROLLER_TYPE_VIRTUAL) {
                break;
            }
#else
            break;
#endif
        }
    }

    for (const auto& [id, gamepad] : gamepads) {
        bool used = false;
        for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && !used; b++) {
            used = SDL_GameControllerGetButton(gamepad, (SDL_GameControllerButton)b);
        }
        for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX && !used; a++) {
            used = std::abs(SDL_GameControllerGetAxis(gamepad, (SDL_GameControllerAxis)a)) > 20000;
        }
        if (used) {
            sLastDevice = StyleOfGamepad(gamepad);
            return;
        }
    }

    if (!ImGui::GetIO().WantCaptureKeyboard) {
        int count = 0;
        const Uint8* keys = SDL_GetKeyboardState(&count);
        for (int k = SDL_SCANCODE_A; k < count; k++) {
            if (keys[k]) {
                sLastDevice = Style::Keyboard;
                return;
            }
        }
    }
}

Style CurrentStyle() {
    Style style = (Style)CVarGetInteger(CVAR_STYLE, (int)Style::Auto);
    if (style < Style::Auto || style > Style::Keyboard) {
        style = Style::Auto;
    }
    return style == Style::Auto ? sLastDevice : style;
}

struct Input {
    enum Kind { None, Button, Axis, Key } kind = None;
    int index = 0;    // SDL button or axis
    bool positive = true;
    std::string name; // key name
};

const CONTROLLERBUTTONS_T kPromptMasks[BUTTON_PROMPT_MAX] = {
    BTN_A,      BTN_B,      BTN_CDOWN,  BTN_L, BTN_R, BTN_Z, BTN_CUP, BTN_CDOWN,
    BTN_CLEFT,  BTN_CRIGHT, 0,          BTN_START,
};

Input FindInput(CONTROLLERBUTTONS_T mask, bool keyboard) {
    Input input;
    auto controller = Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(0);
    if (controller == nullptr || controller->GetButton(mask) == nullptr) {
        return input;
    }
    std::map<std::string, std::shared_ptr<Ship::ControllerButtonMapping>> mappings; // sorted, so the pick is stable
    for (const auto& [id, mapping] : controller->GetButton(mask)->GetAllButtonMappings()) {
        mappings[id] = mapping;
    }
    for (const auto& [id, mapping] : mappings) {
        if (keyboard) {
            if (mapping->GetMappingType() == MAPPING_TYPE_KEYBOARD) {
                input.kind = Input::Key;
                input.name = mapping->GetPhysicalInputName();
                return input;
            }
            continue;
        }
        if (mapping->GetMappingType() != MAPPING_TYPE_GAMEPAD) {
            continue;
        }
        // "P0-B32768-SDLB0" or "P0-B8192-SDLA4-ADP"
        size_t pos = id.find("-SDLB");
        if (pos != std::string::npos) {
            input.kind = Input::Button;
            input.index = std::atoi(id.c_str() + pos + 5);
            return input;
        }
        pos = id.find("-SDLA");
        if (pos != std::string::npos) {
            input.kind = Input::Axis;
            input.index = std::atoi(id.c_str() + pos + 5);
            input.positive = id.find("-ADN") == std::string::npos;
            return input;
        }
    }
    return input;
}

// The keys of the stick, as one label like "WASD", or empty when they are not single characters.
std::string StickKeys() {
    auto controller = Ship::Context::GetInstance()->GetControlDeck()->GetControllerByPort(0);
    if (controller == nullptr) {
        return "";
    }
    std::string label;
    for (Ship::Direction direction : { Ship::UP, Ship::LEFT, Ship::DOWN, Ship::RIGHT }) {
        std::string key;
        for (const auto& [id, mapping] : controller->GetLeftStick()->GetAllAxisDirectionMappingByDirection(direction)) {
            if (mapping->GetMappingType() == MAPPING_TYPE_KEYBOARD) {
                key = mapping->GetPhysicalInputName();
                break;
            }
        }
        if (key.size() != 1) {
            return "";
        }
        label += key;
    }
    return label;
}

// ------------------------------------------------------------------ drawing

struct Color {
    float r, g, b, a = 1.0f;
};

const Color kWhite = { 1.0f, 1.0f, 1.0f };
const Color kFace = { 0.15f, 0.15f, 0.17f };
const Color kRim = { 0.50f, 0.50f, 0.54f };
const Color kShadow = { 0.0f, 0.0f, 0.0f, 0.55f };
const Color kKeyFace = { 0.93f, 0.93f, 0.92f };
const Color kKeyEdge = { 0.52f, 0.52f, 0.55f };
const Color kKeyText = { 0.12f, 0.12f, 0.14f };

struct Canvas {
    int w, h;
    std::vector<float> px; // straight RGBA

    Canvas(int width, int height) : w(width), h(height), px(width * height * 4, 0.0f) {
    }

    void Over(int x, int y, const Color& c, float coverage) {
        if (x < 0 || y < 0 || x >= w || y >= h || coverage <= 0.0f) {
            return;
        }
        float* p = &px[(y * w + x) * 4];
        float a = std::min(coverage, 1.0f) * c.a;
        float outA = a + p[3] * (1.0f - a);
        if (outA <= 0.0f) {
            return;
        }
        for (int i = 0; i < 3; i++) {
            float src = i == 0 ? c.r : (i == 1 ? c.g : c.b);
            p[i] = (src * a + p[i] * p[3] * (1.0f - a)) / outA;
        }
        p[3] = outA;
    }

    // Fills the area where `sdf` (a signed distance in pixels) is negative, antialiased.
    template <typename F> void Fill(float x0, float y0, float x1, float y1, const Color& c, F sdf) {
        for (int y = std::max(0, (int)std::floor(y0) - 1); y < std::min(h, (int)std::ceil(y1) + 1); y++) {
            for (int x = std::max(0, (int)std::floor(x0) - 1); x < std::min(w, (int)std::ceil(x1) + 1); x++) {
                Over(x, y, c, std::clamp(0.5f - sdf(x + 0.5f, y + 0.5f), 0.0f, 1.0f));
            }
        }
    }

    void Disc(float cx, float cy, float r, const Color& c) {
        Fill(cx - r, cy - r, cx + r, cy + r, c, [=](float x, float y) { return std::hypot(x - cx, y - cy) - r; });
    }

    void Ring(float cx, float cy, float r, float width, const Color& c) {
        float o = r + width / 2;
        Fill(cx - o, cy - o, cx + o, cy + o, c,
             [=](float x, float y) { return std::abs(std::hypot(x - cx, y - cy) - r) - width / 2; });
    }

    void RoundRect(float cx, float cy, float hw, float hh, float radius, const Color& c) {
        Fill(cx - hw, cy - hh, cx + hw, cy + hh, c, [=](float x, float y) {
            float qx = std::abs(x - cx) - (hw - radius);
            float qy = std::abs(y - cy) - (hh - radius);
            return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
        });
    }

    void RoundRectOutline(float cx, float cy, float hw, float hh, float radius, float width, const Color& c) {
        Fill(cx - hw - width, cy - hh - width, cx + hw + width, cy + hh + width, c, [=](float x, float y) {
            float qx = std::abs(x - cx) - (hw - radius);
            float qy = std::abs(y - cy) - (hh - radius);
            float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
            return std::abs(d) - width / 2;
        });
    }

    void Line(float ax, float ay, float bx, float by, float width, const Color& c) {
        float r = width / 2;
        Fill(std::min(ax, bx) - r, std::min(ay, by) - r, std::max(ax, bx) + r, std::max(ay, by) + r, c,
             [=](float x, float y) {
                 float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
                 float t = std::clamp((px * dx + py * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
                 return std::hypot(px - dx * t, py - dy * t) - r;
             });
    }

    // Convex or concave polygon, filled.
    void Polygon(const std::vector<std::pair<float, float>>& v, const Color& c) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto [x, y] : v) {
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
        Fill(x0, y0, x1, y1, c, [&](float x, float y) {
            float d = 1e9f;
            bool inside = false;
            for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
                float ex = v[j].first - v[i].first, ey = v[j].second - v[i].second;
                float wx = x - v[i].first, wy = y - v[i].second;
                float t = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
                d = std::min(d, std::hypot(wx - ex * t, wy - ey * t));
                if (((v[i].second > y) != (v[j].second > y)) &&
                    (x < (v[j].first - v[i].first) * (y - v[i].second) / (v[j].second - v[i].second) + v[i].first)) {
                    inside = !inside;
                }
            }
            return inside ? -d : d;
        });
    }

    void PolygonOutline(const std::vector<std::pair<float, float>>& v, float width, const Color& c) {
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
            Line(v[j].first, v[j].second, v[i].first, v[i].second, width, c);
        }
    }

    std::vector<uint8_t> ToRgba() const {
        std::vector<uint8_t> out(w * h * 4);
        for (size_t i = 0; i < out.size(); i++) {
            out[i] = (uint8_t)std::lround(std::clamp(px[i], 0.0f, 1.0f) * 255.0f);
        }
        return out;
    }
};

// Text, in the menu's font (Montserrat, from 2ship.o2r).
stbtt_fontinfo sFont;
std::shared_ptr<Ship::Font> sFontData;
bool sFontReady = false;

void LoadFont() {
    if (sFontReady || sFontData != nullptr) {
        return;
    }
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_FONT);
    initData->ResourceVersion = 0;
    initData->Path = "fonts/Montserrat-Regular.ttf";
    sFontData = std::static_pointer_cast<Ship::Font>(
        Ship::Context::GetInstance()->GetResourceManager()->LoadResource(initData->Path, false, initData));
    if (sFontData != nullptr && sFontData->Data != nullptr) {
        sFontReady = stbtt_InitFont(&sFont, (const unsigned char*)sFontData->Data,
                                    stbtt_GetFontOffsetForIndex((const unsigned char*)sFontData->Data, 0)) != 0;
    }
}

std::vector<int> Codepoints(const std::string& text) {
    std::vector<int> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i];
        int cp = c, extra = 0;
        if (c >= 0xF0) {
            cp = c & 0x07, extra = 3;
        } else if (c >= 0xE0) {
            cp = c & 0x0F, extra = 2;
        } else if (c >= 0xC0) {
            cp = c & 0x1F, extra = 1;
        }
        i++;
        for (; extra > 0 && i < text.size(); extra--, i++) {
            cp = (cp << 6) | (text[i] & 0x3F);
        }
        out.push_back(cp);
    }
    return out;
}

// Width of `text` at a cap height of `capHeight` pixels.
float TextWidth(const std::string& text, float capHeight, float* scaleOut = nullptr) {
    if (!sFontReady) {
        return 0.0f;
    }
    int x0, y0, x1, y1;
    stbtt_GetCodepointBox(&sFont, 'H', &x0, &y0, &x1, &y1);
    float scale = capHeight / std::max(1, y1 - y0);
    float width = 0.0f;
    std::vector<int> cps = Codepoints(text);
    for (size_t i = 0; i < cps.size(); i++) {
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&sFont, cps[i], &advance, &lsb);
        width += advance * scale;
        if (i + 1 < cps.size()) {
            width += stbtt_GetCodepointKernAdvance(&sFont, cps[i], cps[i + 1]) * scale;
        }
    }
    if (scaleOut != nullptr) {
        *scaleOut = scale;
    }
    return width;
}

// Draws `text` centered on (cx, cy), with capitals `capHeight` tall, shrunk to fit `maxWidth`.
// `bold` thickens the strokes by that many pixels.
void Text(Canvas& canvas, const std::string& text, float cx, float cy, float capHeight, float maxWidth,
          const Color& c, float bold) {
    float scale;
    float width = TextWidth(text, capHeight, &scale);
    if (width <= 0.0f) {
        return;
    }
    if (width > maxWidth) {
        scale *= maxWidth / width;
        capHeight *= maxWidth / width;
        width = maxWidth;
    }
    float penX = cx - width / 2;
    float baseline = cy + capHeight / 2;
    std::vector<int> cps = Codepoints(text);
    for (size_t i = 0; i < cps.size(); i++) {
        float shift = penX - std::floor(penX);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel(&sFont, cps[i], scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        int bw = x1 - x0, bh = y1 - y0;
        if (bw > 0 && bh > 0) {
            std::vector<unsigned char> bitmap(bw * bh);
            stbtt_MakeCodepointBitmapSubpixel(&sFont, bitmap.data(), bw, bh, bw, scale, scale, shift, 0, cps[i]);
            int r = (int)std::ceil(bold);
            int ox = (int)std::floor(penX) + x0, oy = (int)std::lround(baseline) + y0;
            for (int y = -r; y < bh + r; y++) {
                for (int x = -r; x < bw + r; x++) {
                    // Dilate the coverage by `bold` pixels for a heavier weight.
                    float cov = 0.0f;
                    for (int dy = -r; dy <= r; dy++) {
                        for (int dx = -r; dx <= r; dx++) {
                            int sx = x + dx, sy = y + dy;
                            if (sx < 0 || sy < 0 || sx >= bw || sy >= bh || dx * dx + dy * dy > bold * bold + 0.5f) {
                                continue;
                            }
                            cov = std::max(cov, bitmap[sy * bw + sx] / 255.0f);
                        }
                    }
                    canvas.Over(ox + x, oy + y, c, cov);
                }
            }
        }
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&sFont, cps[i], &advance, &lsb);
        penX += advance * scale;
        if (i + 1 < cps.size()) {
            penX += stbtt_GetCodepointKernAdvance(&sFont, cps[i], cps[i + 1]) * scale;
        }
    }
}

// An arrow pointing `dx, dy` (a unit direction), centered on (cx, cy), `len` long.
void Arrow(Canvas& canvas, float cx, float cy, float dx, float dy, float len, const Color& c) {
    float px = -dy, py = dx;
    float hx = cx + dx * len / 2, hy = cy + dy * len / 2; // tip
    float bx = cx - dx * len / 2, by = cy - dy * len / 2;
    float head = len * 0.5f;
    canvas.Line(bx, by, hx - dx * head * 0.6f, hy - dy * head * 0.6f, len * 0.22f, c);
    canvas.Polygon({ { hx, hy },
                     { hx - dx * head + px * head * 0.6f, hy - dy * head + py * head * 0.6f },
                     { hx - dx * head - px * head * 0.6f, hy - dy * head - py * head * 0.6f } },
                   c);
}

// Each icon is drawn in the square (x, y, s).

void DiscButton(Canvas& canvas, float x, float y, float s, const Color& face, const Color& rim) {
    float cx = x + s / 2, cy = y + s / 2;
    canvas.Disc(cx, cy + s * 0.03f, s * 0.47f, kShadow);
    canvas.Disc(cx, cy, s * 0.44f, face);
    canvas.Ring(cx, cy, s * 0.42f, s * 0.04f, rim);
}

void LetterButton(Canvas& canvas, float x, float y, float s, const std::string& label, const Color& face,
                  const Color& letter) {
    DiscButton(canvas, x, y, s, face, kRim);
    Text(canvas, label, x + s / 2, y + s / 2, s * (label.size() > 1 ? 0.30f : 0.42f), s * 0.66f, letter,
                s * 0.025f);
}

void PillButton(Canvas& canvas, float x, float y, float s, const std::string& label, float hh, float radius) {
    float cx = x + s / 2, cy = y + s / 2;
    canvas.RoundRect(cx, cy + s * 0.03f, s * 0.48f, hh + s * 0.03f, radius + s * 0.03f, kShadow);
    canvas.RoundRect(cx, cy, s * 0.45f, hh, radius, kFace);
    canvas.RoundRectOutline(cx, cy, s * 0.43f, hh - s * 0.02f, radius, s * 0.04f, kRim);
    Text(canvas, label, cx, cy, std::min(hh * 0.9f, s * 0.30f), s * 0.74f, kWhite, s * 0.02f);
}

void MenuLines(Canvas& canvas, float cx, float cy, float s) {
    for (int i = -1; i <= 1; i++) {
        canvas.Line(cx - s * 0.14f, cy + i * s * 0.10f, cx + s * 0.14f, cy + i * s * 0.10f, s * 0.05f, kWhite);
    }
}

void PlayStationSymbol(Canvas& canvas, float cx, float cy, float s, int button) {
    const float w = s * 0.07f;
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: { // cross
            const Color c = { 0.49f, 0.70f, 0.95f };
            float r = s * 0.15f;
            canvas.Line(cx - r, cy - r, cx + r, cy + r, w, c);
            canvas.Line(cx - r, cy + r, cx + r, cy - r, w, c);
            break;
        }
        case SDL_CONTROLLER_BUTTON_B: // circle
            canvas.Ring(cx, cy, s * 0.16f, w, { 0.96f, 0.40f, 0.45f });
            break;
        case SDL_CONTROLLER_BUTTON_X: { // square
            float r = s * 0.15f;
            canvas.PolygonOutline({ { cx - r, cy - r }, { cx + r, cy - r }, { cx + r, cy + r }, { cx - r, cy + r } }, w,
                                  { 0.93f, 0.58f, 0.82f });
            break;
        }
        default: { // triangle
            float r = s * 0.19f;
            canvas.PolygonOutline({ { cx, cy - r }, { cx + r * 0.87f, cy + r * 0.55f }, { cx - r * 0.87f, cy + r * 0.55f } },
                                  w, { 0.26f, 0.82f, 0.68f });
            break;
        }
    }
}

void Stick(Canvas& canvas, float x, float y, float s, const std::string& label, int direction) {
    float cx = x + s / 2, cy = y + s / 2;
    DiscButton(canvas, x, y, s, kFace, kRim);
    canvas.Ring(cx, cy, s * 0.24f, s * 0.035f, { 0.75f, 0.75f, 0.78f });
    if (direction >= 0) {
        // 0 left, 1 right, 2 up, 3 down
        static const float dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        float dx = dirs[direction][0], dy = dirs[direction][1];
        Arrow(canvas, cx + dx * s * 0.30f, cy + dy * s * 0.30f, dx, dy, s * 0.20f, { 1.0f, 0.86f, 0.30f });
    }
    Text(canvas, label, cx, cy, s * 0.20f, s * 0.40f, kWhite, s * 0.02f);
}

void DPad(Canvas& canvas, float x, float y, float s, int highlight) {
    float cx = x + s / 2, cy = y + s / 2, arm = s * 0.15f, len = s * 0.44f;
    canvas.RoundRect(cx, cy + s * 0.03f, len + s * 0.03f, arm + s * 0.03f, s * 0.05f, kShadow);
    canvas.RoundRect(cx, cy + s * 0.03f, arm + s * 0.03f, len + s * 0.03f, s * 0.05f, kShadow);
    canvas.RoundRect(cx, cy, len, arm, s * 0.05f, kFace);
    canvas.RoundRect(cx, cy, arm, len, s * 0.05f, kFace);
    static const float dirs[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } }; // up, down, left, right
    float dx = dirs[highlight][0], dy = dirs[highlight][1];
    canvas.RoundRect(cx + dx * s * 0.28f, cy + dy * s * 0.28f, dx != 0 ? s * 0.12f : arm * 0.75f,
                     dy != 0 ? s * 0.12f : arm * 0.75f, s * 0.03f, { 1.0f, 0.86f, 0.30f });
}

void Keycap(Canvas& canvas, float x, float y, float s, const std::string& name) {
    float cx = x + s / 2, cy = y + s / 2;
    canvas.RoundRect(cx, cy + s * 0.05f, s * 0.46f, s * 0.42f, s * 0.12f, kShadow);
    canvas.RoundRect(cx, cy + s * 0.03f, s * 0.44f, s * 0.40f, s * 0.11f, kKeyEdge);
    canvas.RoundRect(cx, cy - s * 0.02f, s * 0.40f, s * 0.35f, s * 0.09f, kKeyFace);
    static const std::map<std::string, int> arrows = { { "Left", 0 }, { "Right", 1 }, { "Up", 2 }, { "Down", 3 } };
    if (auto arrow = arrows.find(name); arrow != arrows.end()) {
        static const float dirs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        Arrow(canvas, cx, cy - s * 0.02f, dirs[arrow->second][0], dirs[arrow->second][1], s * 0.40f, kKeyText);
        return;
    }
    static const std::map<std::string, std::string> shortNames = {
        { "Return", "Enter" }, { "Escape", "Esc" },      { "Backspace", "Bksp" }, { "Left Shift", "Shift" },
        { "Right Shift", "Shift" }, { "Left Ctrl", "Ctrl" }, { "Right Ctrl", "Ctrl" }, { "Left Alt", "Alt" },
        { "Right Alt", "AltGr" }, { "CapsLock", "Caps" }, { "Delete", "Del" },     { "Insert", "Ins" },
        { "PageUp", "PgUp" },     { "PageDown", "PgDn" },
    };
    std::string label = name;
    if (auto it = shortNames.find(name); it != shortNames.end()) {
        label = it->second;
    } else if (label.rfind("Keypad ", 0) == 0) {
        label = "Num" + label.substr(7);
    }
    Text(canvas, label, cx, cy - s * 0.02f, s * (label.size() > 1 ? 0.24f : 0.34f), s * 0.70f, kKeyText, s * 0.02f);
}

// The original N64 buttons, drawn for the cutscene hint and for prompts that have no mapping in another style.
void N64Button(Canvas& canvas, float x, float y, float s, int prompt) {
    const Color blue = { 0.20f, 0.36f, 0.95f }, green = { 0.18f, 0.66f, 0.30f }, yellow = { 1.0f, 0.82f, 0.10f };
    const Color grey = { 0.55f, 0.55f, 0.58f }, red = { 0.88f, 0.20f, 0.18f };
    float cx = x + s / 2, cy = y + s / 2;
    switch (prompt) {
        case BUTTON_PROMPT_A:
            DiscButton(canvas, x, y, s, blue, { 0.55f, 0.65f, 1.0f });
            Text(canvas, "A", cx, cy, s * 0.42f, s * 0.6f, kWhite, s * 0.03f);
            break;
        case BUTTON_PROMPT_B:
            DiscButton(canvas, x, y, s, green, { 0.55f, 0.90f, 0.60f });
            Text(canvas, "B", cx, cy, s * 0.42f, s * 0.6f, kWhite, s * 0.03f);
            break;
        case BUTTON_PROMPT_START:
            DiscButton(canvas, x, y, s, red, { 1.0f, 0.55f, 0.50f });
            Text(canvas, "S", cx, cy, s * 0.42f, s * 0.6f, kWhite, s * 0.03f);
            break;
        case BUTTON_PROMPT_L:
        case BUTTON_PROMPT_R:
        case BUTTON_PROMPT_Z:
            PillButton(canvas, x, y, s, prompt == BUTTON_PROMPT_L ? "L" : (prompt == BUTTON_PROMPT_R ? "R" : "Z"),
                       s * 0.30f, s * 0.14f);
            break;
        case BUTTON_PROMPT_STICK:
            Stick(canvas, x, y, s, "", -1);
            break;
        default: {
            DiscButton(canvas, x, y, s, yellow, { 1.0f, 0.93f, 0.55f });
            static const std::map<int, std::pair<float, float>> dirs = { { BUTTON_PROMPT_CUP, { 0, -1 } },
                                                                          { BUTTON_PROMPT_CDOWN, { 0, 1 } },
                                                                          { BUTTON_PROMPT_CLEFT, { -1, 0 } },
                                                                          { BUTTON_PROMPT_CRIGHT, { 1, 0 } } };
            if (auto dir = dirs.find(prompt); dir != dirs.end()) {
                float dx = dir->second.first, dy = dir->second.second, r = s * 0.20f;
                canvas.Polygon({ { cx + dx * r, cy + dy * r },
                                 { cx - dx * r * 0.6f - dy * r, cy - dy * r * 0.6f + dx * r },
                                 { cx - dx * r * 0.6f + dy * r, cy - dy * r * 0.6f - dx * r } },
                               { 0.35f, 0.25f, 0.0f });
            } else {
                Text(canvas, "C", cx, cy, s * 0.42f, s * 0.6f, { 0.35f, 0.25f, 0.0f }, s * 0.03f);
            }
            break;
        }
    }
}

// Draws the physical input for `prompt` in `style`. Returns false when there is nothing to draw.
bool DrawInput(Canvas& canvas, float x, float y, float s, Style style, const Input& input, int prompt) {
    if (style == Style::N64) {
        N64Button(canvas, x, y, s, prompt);
        return true;
    }
    if (input.kind == Input::Key) {
        Keycap(canvas, x, y, s, input.name);
        return true;
    }
    if (input.kind == Input::Axis) {
        switch (input.index) {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: {
                bool left = input.index == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                const char* xbox = left ? "LT" : "RT";
                const char* ps = left ? "L2" : "R2";
                const char* nin = left ? "ZL" : "ZR";
                PillButton(canvas, x, y, s, style == Style::PlayStation ? ps : (style == Style::Nintendo ? nin : xbox),
                           s * 0.36f, s * 0.20f);
                return true;
            }
            default: {
                bool left = input.index <= SDL_CONTROLLER_AXIS_LEFTY;
                bool horizontal = input.index == SDL_CONTROLLER_AXIS_LEFTX || input.index == SDL_CONTROLLER_AXIS_RIGHTX;
                int direction = horizontal ? (input.positive ? 1 : 0) : (input.positive ? 3 : 2);
                Stick(canvas, x, y, s, left ? "L" : "R", direction);
                return true;
            }
        }
    }
    if (input.kind != Input::Button) {
        return false;
    }

    switch (input.index) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_B:
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_Y: {
            static const char* sdlLabels = "ABXY";
            if (style == Style::PlayStation) {
                DiscButton(canvas, x, y, s, kFace, kRim);
                PlayStationSymbol(canvas, x + s / 2, y + s / 2, s, input.index);
            } else if (style == Style::Nintendo) {
                // With button labels on (SDL's default), SDL's A is the button labeled A; without them it is the
                // bottom one, labeled B.
                static const char* positional = "BAYX";
                bool labels = SDL_GetHintBoolean(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, SDL_TRUE);
                LetterButton(canvas, x, y, s, std::string(1, (labels ? sdlLabels : positional)[input.index]), kFace,
                             kWhite);
            } else {
                static const Color colors[4] = {
                    { 0.45f, 0.80f, 0.28f }, { 0.95f, 0.30f, 0.26f }, { 0.25f, 0.58f, 0.98f }, { 1.0f, 0.80f, 0.15f }
                };
                LetterButton(canvas, x, y, s, std::string(1, sdlLabels[input.index]), kFace, colors[input.index]);
            }
            return true;
        }
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: {
            bool left = input.index == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
            const char* xbox = left ? "LB" : "RB";
            const char* ps = left ? "L1" : "R1";
            const char* nin = left ? "L" : "R";
            PillButton(canvas, x, y, s, style == Style::PlayStation ? ps : (style == Style::Nintendo ? nin : xbox),
                       s * 0.26f, s * 0.12f);
            return true;
        }
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK: {
            bool left = input.index == SDL_CONTROLLER_BUTTON_LEFTSTICK;
            Stick(canvas, x, y, s,
                  style == Style::PlayStation ? (left ? "L3" : "R3") : (left ? "LS" : "RS"), -1);
            return true;
        }
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            DPad(canvas, x, y, s, input.index - SDL_CONTROLLER_BUTTON_DPAD_UP);
            return true;
        case SDL_CONTROLLER_BUTTON_START:
            if (style == Style::Nintendo) {
                LetterButton(canvas, x, y, s, "+", kFace, kWhite);
            } else {
                DiscButton(canvas, x, y, s, kFace, kRim);
                MenuLines(canvas, x + s / 2, y + s / 2, s);
            }
            return true;
        case SDL_CONTROLLER_BUTTON_BACK:
            if (style == Style::Nintendo) {
                LetterButton(canvas, x, y, s, "-", kFace, kWhite);
            } else {
                PillButton(canvas, x, y, s, style == Style::PlayStation ? "Share" : "View", s * 0.22f, s * 0.12f);
            }
            return true;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            LetterButton(canvas, x, y, s, style == Style::PlayStation ? "PS" : "Home", kFace, kWhite);
            return true;
        default:
            PillButton(canvas, x, y, s, "B" + std::to_string(input.index), s * 0.22f, s * 0.12f);
            return true;
    }
}

// ------------------------------------------------------------------ textures

// Paths of the textures, as the game uses them ("__OTR__" + path).
const char* kPrefix = "__OTR__";
const char* kBtn4Dir = "mm3d/prompts/btn4/";
const char* kBtn8Dir = "mm3d/prompts/btn8/";
const char* kPauseDir = "mm3d/prompts/pause/";
const char* kHintHold = "mm3d/prompts/hint/hold";
const char* kHintActive = "mm3d/prompts/hint/active";

std::string sBtn4Paths[BUTTON_PROMPT_MAX];
std::string sBtn8Paths[BUTTON_PROMPT_MAX];
std::string sPausePaths[BUTTON_PROMPT_PAUSE_MAX];
std::string sHintPaths[2];

std::mutex sMutex;
std::map<std::string, std::vector<char>> sFiles; // archive path -> resource file
std::vector<std::shared_ptr<Ship::IResource>> sRetired;
Style sStyle = Style::N64;
bool sHas[BUTTON_PROMPT_MAX] = {};
std::string sStateKey;

// A Texture (version 1) resource with RGBA texels `scale` times the size of the slot it replaces, flagged to be
// uploaded as is, like an HD texture pack.
std::vector<char> MakeTexture(const Canvas& canvas, float hByteScale, float vPixelScale) {
    std::vector<char> file(64, 0);
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++) {
            file.push_back((char)((v >> (8 * i)) & 0xFF));
        }
    };
    auto putF = [&](float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        put32(v);
    };
    file[0] = 0; // little endian
    file[1] = 1; // custom
    uint32_t type = static_cast<uint32_t>(Fast::ResourceType::Texture);
    uint32_t version = 1;
    uint64_t id = 0xDEADBEEFDEADBEEFULL;
    std::memcpy(&file[4], &type, 4);
    std::memcpy(&file[8], &version, 4);
    std::memcpy(&file[12], &id, 8);
    put32(1); // RGBA32
    put32(canvas.w);
    put32(canvas.h);
    put32(1); // TEX_FLAG_LOAD_AS_RAW
    putF(hByteScale);
    putF(vPixelScale);
    std::vector<uint8_t> rgba = canvas.ToRgba();
    put32((uint32_t)rgba.size());
    file.insert(file.end(), rgba.begin(), rgba.end());
    return file;
}

std::string HintText(bool active, std::string* after) {
    if (active) {
        *after = "";
        return BenGui::Tr("Speeding up");
    }
    *after = BenGui::Tr("to speed up");
    return BenGui::Tr("Hold");
}

Canvas MakeHint(bool active, Style style, const Input& input) {
    Canvas canvas(512, 64);
    std::string after;
    std::string before = HintText(active, &after);
    const float cap = 22.0f, gap = 10.0f, icon = 56.0f;
    float width = TextWidth(before, cap) + (active ? gap + 40.0f : gap + icon + gap + TextWidth(after, cap));
    width = std::min(width, 480.0f);
    float x1 = 504.0f, x0 = x1 - width - 24.0f;
    canvas.RoundRect((x0 + x1) / 2, 32, (x1 - x0) / 2, 26, 26, { 0.0f, 0.0f, 0.0f, 0.55f });
    float pen = x0 + 12.0f;
    float w = TextWidth(before, cap);
    Text(canvas, before, pen + w / 2, 32, cap, w, kWhite, 1.2f);
    pen += w + gap;
    if (active) {
        // Two arrows, like a fast-forward sign.
        canvas.Polygon({ { pen, 18 }, { pen + 18, 32 }, { pen, 46 } }, kWhite);
        canvas.Polygon({ { pen + 18, 18 }, { pen + 36, 32 }, { pen + 18, 46 } }, kWhite);
        return canvas;
    }
    if (!DrawInput(canvas, pen, 4, icon, style, input, BUTTON_PROMPT_B)) {
        N64Button(canvas, pen, 4, icon, BUTTON_PROMPT_B);
    }
    pen += icon + gap;
    w = TextWidth(after, cap);
    Text(canvas, after, pen + w / 2, 32, cap, w, kWhite, 1.2f);
    return canvas;
}

// Builds every texture for the current style and mappings. Done again whenever they change.
void Regenerate() {
    LoadFont();

    Style style = CurrentStyle();
    bool keyboard = style == Style::Keyboard;
    Input inputs[BUTTON_PROMPT_MAX];
    std::string key = std::to_string((int)style) + "|" + BenGui::Tr("Hold") + "|" + (sFontReady ? "f" : "-");
    for (int p = 0; p < BUTTON_PROMPT_MAX; p++) {
        if (p == BUTTON_PROMPT_STICK) {
            if (keyboard) {
                inputs[p].kind = Input::Key;
                inputs[p].name = StickKeys();
                if (inputs[p].name.empty()) {
                    inputs[p].kind = Input::None;
                }
            } else {
                inputs[p].kind = Input::Axis;
                inputs[p].index = SDL_CONTROLLER_AXIS_LEFTX;
            }
        } else {
            inputs[p] = FindInput(kPromptMasks[p], keyboard);
        }
        key += "|" + std::to_string(inputs[p].kind) + ":" + std::to_string(inputs[p].index) +
               (inputs[p].positive ? "+" : "-") + inputs[p].name;
    }
    if (key == sStateKey) {
        return;
    }

    std::map<std::string, std::vector<char>> files;
    bool has[BUTTON_PROMPT_MAX] = {};
    for (int p = 0; p < BUTTON_PROMPT_MAX; p++) {
        Canvas canvas(kIcon, kIcon);
        if (inputs[p].kind == Input::Axis && p == BUTTON_PROMPT_STICK) {
            Stick(canvas, 0, 0, kIcon, "L", -1);
            has[p] = true;
        } else {
            has[p] = DrawInput(canvas, 0, 0, kIcon, style, inputs[p], p);
        }
        // An i4 16x16 slot has 8 bytes per row, an ia8 one 16.
        files[sBtn4Paths[p]] = MakeTexture(canvas, kIcon * 4 / 8.0f, kScale);
        files[sBtn8Paths[p]] = MakeTexture(canvas, kIcon * 4 / 16.0f, kScale);
    }
    {
        Canvas a(24 * kScale, 16 * kScale);
        DrawInput(a, 4 * kScale, 0, kIcon, style, inputs[BUTTON_PROMPT_A], BUTTON_PROMPT_A);
        files[sPausePaths[BUTTON_PROMPT_PAUSE_A]] = MakeTexture(a, 16.0f, kScale);

        Canvas c(48 * kScale, 16 * kScale);
        DrawInput(c, 0, 0, kIcon, style, inputs[BUTTON_PROMPT_CLEFT], BUTTON_PROMPT_CLEFT);
        DrawInput(c, kIcon, 0, kIcon, style, inputs[BUTTON_PROMPT_CDOWN], BUTTON_PROMPT_CDOWN);
        DrawInput(c, kIcon * 2, 0, kIcon, style, inputs[BUTTON_PROMPT_CRIGHT], BUTTON_PROMPT_CRIGHT);
        files[sPausePaths[BUTTON_PROMPT_PAUSE_C]] = MakeTexture(c, 16.0f, kScale);

        for (int i = 0; i < 2; i++) {
            int prompt = i == 0 ? BUTTON_PROMPT_Z : BUTTON_PROMPT_R;
            Canvas zr(24 * kScale, 32 * kScale);
            DrawInput(zr, 0, 4 * kScale, 24 * kScale, style, inputs[prompt], prompt);
            files[sPausePaths[i == 0 ? BUTTON_PROMPT_PAUSE_Z : BUTTON_PROMPT_PAUSE_R]] = MakeTexture(zr, 16.0f, kScale);
        }
    }
    files[sHintPaths[0]] = MakeTexture(MakeHint(false, style, inputs[BUTTON_PROMPT_B]), 16.0f, kScale);
    files[sHintPaths[1]] = MakeTexture(MakeHint(true, style, inputs[BUTTON_PROMPT_B]), 16.0f, kScale);

    {
        std::lock_guard<std::mutex> lock(sMutex);
        sFiles = std::move(files);
        sStyle = style;
        std::copy(std::begin(has), std::end(has), std::begin(sHas));
        sStateKey = key;
    }

    // Drop the old textures so the next draw loads the new ones. They are kept alive, so the renderer never sees a
    // new texture at the address of an old one.
    auto resourceManager = Ship::Context::GetInstance()->GetResourceManager();
    for (const auto& [path, data] : sFiles) {
        if (auto old = resourceManager->GetCachedResource(path); old != nullptr) {
            sRetired.push_back(old);
        }
        resourceManager->UnloadResource(path);
    }
}

class ButtonPromptsArchive final : public Ship::Archive {
  public:
    ButtonPromptsArchive() : Ship::Archive("mm3d-button-prompts") {
    }

    bool Open() override {
        for (int p = 0; p < BUTTON_PROMPT_MAX; p++) {
            IndexFile(sBtn4Paths[p]);
            IndexFile(sBtn8Paths[p]);
        }
        for (const std::string& path : sPausePaths) {
            IndexFile(path);
        }
        for (const std::string& path : sHintPaths) {
            IndexFile(path);
        }
        return true;
    }

    bool Close() override {
        return true;
    }

    bool WriteFile(const std::string&, const std::vector<uint8_t>&) override {
        return false;
    }

    std::shared_ptr<Ship::File> LoadFile(const std::string& filePath) override {
        std::lock_guard<std::mutex> lock(sMutex);
        auto it = sFiles.find(filePath);
        if (it == sFiles.end()) {
            return nullptr;
        }
        auto file = std::make_shared<Ship::File>();
        file->Buffer = std::make_shared<std::vector<char>>(it->second);
        file->IsLoaded = true;
        return file;
    }

    std::shared_ptr<Ship::File> LoadFile(uint64_t hash) override {
        const std::string* path =
            Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToString(hash);
        return path != nullptr ? LoadFile(*path) : nullptr;
    }
};

const char* GamePath(const std::string& path) {
    // Stable storage for the "__OTR__" strings handed to the game.
    static std::map<std::string, std::string> sPaths;
    auto it = sPaths.find(path);
    if (it == sPaths.end()) {
        it = sPaths.emplace(path, kPrefix + path).first;
    }
    return it->second.c_str();
}

bool Ready() {
    return !sStateKey.empty();
}

} // namespace

// ------------------------------------------------------------------ game side

extern "C" s32 ButtonPrompts_Active(void) {
    return Ready() && sStyle != Style::N64;
}

extern "C" const char* ButtonPrompts_Icon(ButtonPrompt prompt) {
    if (!ButtonPrompts_Active() || prompt < 0 || prompt >= BUTTON_PROMPT_MAX || !sHas[prompt]) {
        return nullptr;
    }
    return GamePath(sBtn8Paths[prompt]);
}

extern "C" const char* ButtonPrompts_MessageGlyph(u8 character) {
    // 0xB0 to 0xBB: A, B, C, L, R, Z, C-Up, C-Down, C-Left, C-Right, Z target (kept), control stick.
    static const s8 sPrompts[] = {
        BUTTON_PROMPT_A,     BUTTON_PROMPT_B,      BUTTON_PROMPT_C,     BUTTON_PROMPT_L,
        BUTTON_PROMPT_R,     BUTTON_PROMPT_Z,      BUTTON_PROMPT_CUP,   BUTTON_PROMPT_CDOWN,
        BUTTON_PROMPT_CLEFT, BUTTON_PROMPT_CRIGHT, -1,                  BUTTON_PROMPT_STICK,
    };
    if (!ButtonPrompts_Active() || character < 0xB0 || character > 0xBB) {
        return nullptr;
    }
    s8 prompt = sPrompts[character - 0xB0];
    if (prompt < 0 || !sHas[prompt]) {
        return nullptr;
    }
    return GamePath(sBtn4Paths[prompt]);
}

extern "C" const char* ButtonPrompts_PauseTexture(ButtonPromptPauseTexture texture) {
    if (!ButtonPrompts_Active() || texture < 0 || texture >= BUTTON_PROMPT_PAUSE_MAX) {
        return nullptr;
    }
    return GamePath(sPausePaths[texture]);
}

static bool HudBadgesShown() {
    return ButtonPrompts_Active() && CVarGetInteger(CVAR_HUD_BADGES, 1);
}

static void DrawBadge(PlayState* play, HudEditorElementID element, ButtonPrompt prompt, s16 left, s16 top, s16 alpha) {
    const char* texture = ButtonPrompts_Icon(prompt);
    if (texture == nullptr || alpha <= 0) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, alpha);
    HudEditor_SetActiveElement(element);
    // 16x16 slot drawn 12 pixels wide.
    OVERLAY_DISP = Gfx_DrawTexRectIA8(OVERLAY_DISP, (TexturePtr)texture, 16, 16, left, top, 12, 12,
                                      (16 << 10) / 12, (16 << 10) / 12);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}

extern "C" void ButtonPrompts_DrawItemButtonBadges(PlayState* play) {
    if (!HudBadgesShown()) {
        return;
    }
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    // Bottom right corner of each button (positions from Interface_DrawItemButtons).
    DrawBadge(play, HUD_EDITOR_ELEMENT_B, BUTTON_PROMPT_B, 167 + 29 - 9, 17 + 29 - 9, interfaceCtx->bAlpha);
    DrawBadge(play, HUD_EDITOR_ELEMENT_C_LEFT, BUTTON_PROMPT_CLEFT, 227 + 27 - 9, 18 + 27 - 9,
              interfaceCtx->cLeftAlpha);
    DrawBadge(play, HUD_EDITOR_ELEMENT_C_DOWN, BUTTON_PROMPT_CDOWN, 249 + 27 - 9, 34 + 27 - 9,
              interfaceCtx->cDownAlpha);
    DrawBadge(play, HUD_EDITOR_ELEMENT_C_RIGHT, BUTTON_PROMPT_CRIGHT, 271 + 27 - 9, 18 + 27 - 9,
              interfaceCtx->cRightAlpha);
}

extern "C" void ButtonPrompts_DrawAButtonBadge(PlayState* play) {
    if (!HudBadgesShown()) {
        return;
    }
    // The A button is drawn in the viewport (190, 23) to (235, 68), around its center.
    DrawBadge(play, HUD_EDITOR_ELEMENT_A, BUTTON_PROMPT_A, 219, 51 + R_A_BTN_Y_OFFSET, play->interfaceCtx.aAlpha);
}

namespace ButtonPrompts {

void Mount() {
    for (int p = 0; p < BUTTON_PROMPT_MAX; p++) {
        sBtn4Paths[p] = kBtn4Dir + std::to_string(p);
        sBtn8Paths[p] = kBtn8Dir + std::to_string(p);
    }
    for (int i = 0; i < BUTTON_PROMPT_PAUSE_MAX; i++) {
        sPausePaths[i] = kPauseDir + std::to_string(i);
    }
    sHintPaths[0] = kHintHold;
    sHintPaths[1] = kHintActive;

    auto archive = std::make_shared<ButtonPromptsArchive>();
    archive->Load();
    if (archive->IsLoaded()) {
        Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->AddArchive(archive);
    }
}

std::string GetActiveStyleName() {
    switch (CurrentStyle()) {
        case Style::Xbox:
            return "Xbox";
        case Style::PlayStation:
            return "PlayStation";
        case Style::Nintendo:
            return "Nintendo Switch";
        case Style::Keyboard:
            return BenGui::Tr("Keyboard");
        default:
            return "N64";
    }
}

const char* GetCutsceneHint(bool active) {
    if (!Ready()) {
        return nullptr;
    }
    return GamePath(sHintPaths[active ? 1 : 0]);
}

} // namespace ButtonPrompts

static RegisterShipInitFunc sRegisterButtonPrompts([]() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>([]() {
        // The device is checked every frame, the mappings (which take longer to read) a few times a second.
        static int sFrames = 0;
        Style before = CurrentStyle();
        PollLastDevice();
        if (CurrentStyle() != before || !Ready() || ++sFrames >= 15) {
            sFrames = 0;
            Regenerate();
        }
    });
});
