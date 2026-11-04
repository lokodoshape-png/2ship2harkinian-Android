#ifndef QUEST_MENU_RENDERER_H
#define QUEST_MENU_RENDERER_H

// Drawing primitives of the quest menu. Everything is laid out on a 1100x700 page (Page) that is scaled to the
// game view, so the screen keeps its look at any resolution. The ornate art (frames, parchment, buttons, icons) comes
// from the PNGs in assets/custom/overlays/questmenu (see Asset / NineSlice); the shapes below draw the small
// pictograms and act as fallbacks when a PNG is missing.

#include <imgui.h>
#include <cfloat>
#include <cmath>
#include <string>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

extern "C" {
#include <z64.h>
#include "variables.h"
}

namespace QuestUi {

inline ImTextureID Texture(const char* path) {
    if (path == nullptr) {
        return nullptr;
    }
    return Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName(path);
}

inline constexpr float kPageW = 1100.0f;
inline constexpr float kPageH = 700.0f;

inline ImU32 Col(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

inline const ImU32 cGold = Col(244, 204, 98);
inline const ImU32 cGoldDark = Col(150, 105, 40);
inline const ImU32 cPurpleDeep = Col(28, 10, 44);
inline const ImU32 cPurple = Col(58, 26, 86);
inline const ImU32 cPurpleLight = Col(92, 50, 128);
inline const ImU32 cParchment = Col(240, 226, 196);
inline const ImU32 cParchmentDark = Col(222, 202, 160);
inline const ImU32 cInk = Col(62, 38, 22);
inline const ImU32 cWine = Col(104, 30, 48);
inline const ImU32 cGreen = Col(84, 214, 70);

struct Page {
    ImDrawList* dl;
    ImVec2 origin;
    float k;
    ImVec2 P(float x, float y) const {
        return ImVec2(origin.x + x * k, origin.y + y * k);
    }
    float S(float v) const {
        return v * k;
    }
};

// The quest menu's fonts (Alegreya, set by the menu from OTRGlobals); the GUI font when they are missing.
inline ImFont* gBodyFont = nullptr;
inline ImFont* gTitleFont = nullptr;

inline ImFont* BodyFont() {
    return gBodyFont != nullptr ? gBodyFont : ImGui::GetFont();
}

inline ImFont* TitleFont() {
    return gTitleFont != nullptr ? gTitleFont : BodyFont();
}

inline void Text(const Page& pg, float x, float y, float size, ImU32 col, const char* text, float wrap = 0.0f,
                 ImFont* font = nullptr) {
    pg.dl->AddText(font ? font : BodyFont(), pg.S(size), pg.P(x, y), col, text, nullptr, wrap > 0 ? pg.S(wrap) : 0.0f);
}

inline void TextShadow(const Page& pg, float x, float y, float size, ImU32 col, const char* text,
                       ImFont* font = nullptr) {
    Text(pg, x + 1.5f, y + 2.0f, size, Col(0, 0, 0, 160), text, 0.0f, font);
    Text(pg, x, y, size, col, text, 0.0f, font);
}

inline float TextWidth(const Page& pg, float size, const char* text, ImFont* font = nullptr) {
    return (font ? font : BodyFont())->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

inline void TextCentered(const Page& pg, float cx, float y, float size, ImU32 col, const char* text, bool shadow = true,
                         ImFont* font = nullptr) {
    float w = TextWidth(pg, size, text, font);
    if (shadow) {
        TextShadow(pg, cx - w / 2, y, size, col, text, font);
    } else {
        Text(pg, cx - w / 2, y, size, col, text, 0.0f, font);
    }
}

inline float WrappedHeight(const Page& pg, float size, float wrap, const char* text, ImFont* font = nullptr) {
    return (font ? font : BodyFont())->CalcTextSizeA(size, FLT_MAX, wrap, text).y;
}

// A gold frame: dark outline, bright gold band, thin highlight, with optional glow around it.
inline void GoldFrame(const Page& pg, ImVec2 a, ImVec2 b, float rounding, float width, bool glow = false) {
    ImDrawList* dl = pg.dl;
    if (glow) {
        for (int i = 5; i >= 1; i--) {
            dl->AddRect(pg.P(a.x - i * 2, a.y - i * 2), pg.P(b.x + i * 2, b.y + i * 2), Col(255, 210, 90, 22 * (6 - i)),
                        pg.S(rounding + i * 2), 0, pg.S(3.0f));
        }
    }
    dl->AddRect(pg.P(a.x, a.y), pg.P(b.x, b.y), Col(40, 20, 10, 230), pg.S(rounding), 0, pg.S(width + 2));
    dl->AddRect(pg.P(a.x, a.y), pg.P(b.x, b.y), cGold, pg.S(rounding), 0, pg.S(width));
    dl->AddRect(pg.P(a.x + width * 0.5f, a.y + width * 0.5f), pg.P(b.x - width * 0.5f, b.y - width * 0.5f),
                Col(255, 240, 180, 120), pg.S(rounding), 0, pg.S(1.0f));
}

inline void Gradient(const Page& pg, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom) {
    pg.dl->AddRectFilledMultiColor(pg.P(a.x, a.y), pg.P(b.x, b.y), top, top, bottom, bottom);
}

inline void Panel(const Page& pg, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float rounding) {
    // Rounded fill with a vertical gradient: a rounded base plus the gradient inset by the rounding.
    pg.dl->AddRectFilled(pg.P(a.x, a.y), pg.P(b.x, b.y), bottom, pg.S(rounding));
    Gradient(pg, ImVec2(a.x + rounding * 0.3f, a.y + rounding * 0.3f), ImVec2(b.x - rounding * 0.3f, b.y - rounding),
             top, bottom);
}

// Little diamond studs at the corners of a frame.
inline void CornerStuds(const Page& pg, ImVec2 a, ImVec2 b, float inset) {
    ImVec2 corners[4] = { { a.x + inset, a.y + inset }, { b.x - inset, a.y + inset }, { a.x + inset, b.y - inset },
                          { b.x - inset, b.y - inset } };
    for (ImVec2 c : corners) {
        float r = 7.0f;
        pg.dl->AddQuadFilled(pg.P(c.x, c.y - r), pg.P(c.x + r, c.y), pg.P(c.x, c.y + r), pg.P(c.x - r, c.y), cGold);
        pg.dl->AddQuadFilled(pg.P(c.x, c.y - r * 0.45f), pg.P(c.x + r * 0.45f, c.y), pg.P(c.x, c.y + r * 0.45f),
                             pg.P(c.x - r * 0.45f, c.y), Col(120, 40, 140));
    }
}

// ----- Icons, drawn with shapes so they are crisp at any size. (cx, cy) is the centre, s the size.

inline void IconTriforce(const Page& pg, float cx, float cy, float s, ImU32 col = cGold) {
    float h = s * 0.866f;
    auto tri = [&](float x, float y, float w) {
        float th = w * 0.866f;
        pg.dl->AddTriangleFilled(pg.P(x, y - th / 2), pg.P(x + w / 2, y + th / 2), pg.P(x - w / 2, y + th / 2), col);
    };
    tri(cx, cy - h / 4, s / 2);
    tri(cx - s / 4, cy + h / 4, s / 2);
    tri(cx + s / 4, cy + h / 4, s / 2);
}

inline void IconMajora(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    // Spikes
    for (int i = 0; i < 10; i++) {
        float ang = 3.14159f * (0.05f + 0.9f * i / 9.0f) + 3.14159f;
        float bx = cx + cosf(ang) * s * 0.36f, by = cy + sinf(ang) * s * 0.30f;
        float tx = cx + cosf(ang) * s * 0.62f, ty = cy + sinf(ang) * s * 0.55f;
        float px = -sinf(ang) * s * 0.06f, py = cosf(ang) * s * 0.06f;
        dl->AddTriangleFilled(pg.P(bx + px, by + py), pg.P(tx, ty), pg.P(bx - px, by - py), Col(236, 196, 84));
    }
    for (int i = 0; i < 2; i++) {
        float side = i == 0 ? -1.0f : 1.0f;
        dl->AddTriangleFilled(pg.P(cx + side * s * 0.12f, cy + s * 0.25f), pg.P(cx + side * s * 0.42f, cy + s * 0.55f),
                              pg.P(cx + side * s * 0.30f, cy + s * 0.18f), Col(80, 170, 70));
    }
    // Heart-shaped face
    dl->AddCircleFilled(pg.P(cx - s * 0.17f, cy - s * 0.02f), pg.S(s * 0.24f), Col(120, 40, 150));
    dl->AddCircleFilled(pg.P(cx + s * 0.17f, cy - s * 0.02f), pg.S(s * 0.24f), Col(120, 40, 150));
    dl->AddTriangleFilled(pg.P(cx - s * 0.40f, cy + s * 0.04f), pg.P(cx + s * 0.40f, cy + s * 0.04f),
                          pg.P(cx, cy + s * 0.42f), Col(120, 40, 150));
    dl->AddTriangleFilled(pg.P(cx - s * 0.30f, cy + s * 0.12f), pg.P(cx + s * 0.30f, cy + s * 0.12f),
                          pg.P(cx, cy + s * 0.36f), Col(200, 50, 60));
    for (int i = 0; i < 2; i++) {
        float ex = cx + (i == 0 ? -1 : 1) * s * 0.16f;
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.13f), Col(250, 200, 40));
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.085f), Col(240, 110, 30));
        dl->AddCircleFilled(pg.P(ex, cy - s * 0.02f), pg.S(s * 0.04f), Col(60, 170, 60));
    }
}

inline void IconBook(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddQuadFilled(pg.P(cx - s * 0.45f, cy - s * 0.30f), pg.P(cx, cy - s * 0.22f), pg.P(cx, cy + s * 0.34f),
                      pg.P(cx - s * 0.45f, cy + s * 0.26f), Col(246, 232, 196));
    dl->AddQuadFilled(pg.P(cx, cy - s * 0.22f), pg.P(cx + s * 0.45f, cy - s * 0.30f), pg.P(cx + s * 0.45f, cy + s * 0.26f),
                      pg.P(cx, cy + s * 0.34f), Col(232, 214, 172));
    for (int i = 0; i < 4; i++) {
        float y = cy - s * 0.12f + i * s * 0.09f;
        dl->AddLine(pg.P(cx - s * 0.36f, y - s * 0.03f), pg.P(cx - s * 0.06f, y), Col(150, 110, 60), pg.S(1.2f));
        dl->AddLine(pg.P(cx + s * 0.06f, y), pg.P(cx + s * 0.36f, y - s * 0.03f), Col(150, 110, 60), pg.S(1.2f));
    }
    dl->AddLine(pg.P(cx, cy - s * 0.22f), pg.P(cx, cy + s * 0.34f), Col(120, 70, 30), pg.S(2.0f));
    dl->AddRect(pg.P(cx - s * 0.47f, cy - s * 0.32f), pg.P(cx + s * 0.47f, cy + s * 0.36f), Col(140, 80, 30), pg.S(3),
                0, pg.S(2.0f));
}

inline void IconHeart(const Page& pg, float cx, float cy, float s, ImU32 col = Col(220, 40, 70)) {
    ImDrawList* dl = pg.dl;
    float r = s * 0.24f;
    dl->AddCircleFilled(pg.P(cx - r * 0.95f, cy - r * 0.3f), pg.S(r + 2), cGold);
    dl->AddCircleFilled(pg.P(cx + r * 0.95f, cy - r * 0.3f), pg.S(r + 2), cGold);
    dl->AddTriangleFilled(pg.P(cx - r * 2.05f, cy - r * 0.1f), pg.P(cx + r * 2.05f, cy - r * 0.1f),
                          pg.P(cx, cy + s * 0.46f), cGold);
    dl->AddCircleFilled(pg.P(cx - r * 0.95f, cy - r * 0.3f), pg.S(r), col);
    dl->AddCircleFilled(pg.P(cx + r * 0.95f, cy - r * 0.3f), pg.S(r), col);
    dl->AddTriangleFilled(pg.P(cx - r * 1.9f, cy - r * 0.1f), pg.P(cx + r * 1.9f, cy - r * 0.1f),
                          pg.P(cx, cy + s * 0.40f), col);
    dl->AddCircleFilled(pg.P(cx - r * 1.1f, cy - r * 0.55f), pg.S(r * 0.3f), Col(255, 200, 210, 200));
}

inline void IconFairy(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 4; i++) {
        float sx = (i % 2 == 0) ? -1.0f : 1.0f;
        float sy = (i < 2) ? -1.0f : 1.0f;
        ImVec2 c = pg.P(cx + sx * s * 0.24f, cy + sy * s * 0.14f);
        dl->AddEllipseFilled(c, ImVec2(pg.S(s * 0.22f), pg.S(s * (i < 2 ? 0.16f : 0.11f))), Col(230, 240, 255, 170),
                             sx * sy * 0.5f);
        dl->AddEllipse(c, ImVec2(pg.S(s * 0.22f), pg.S(s * (i < 2 ? 0.16f : 0.11f))), Col(255, 255, 255, 220),
                       sx * sy * 0.5f, 0, pg.S(1.0f));
    }
    for (int i = 6; i >= 1; i--) {
        dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.04f * i), Col(255, 240, 140, 30));
    }
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.12f), Col(255, 250, 200));
}

inline void IconNote(const Page& pg, float cx, float cy, float s, ImU32 col = Col(190, 140, 255)) {
    ImDrawList* dl = pg.dl;
    dl->AddEllipseFilled(pg.P(cx - s * 0.14f, cy + s * 0.24f), ImVec2(pg.S(s * 0.16f), pg.S(s * 0.11f)), col, -0.4f);
    dl->AddLine(pg.P(cx + s * 0.0f, cy + s * 0.22f), pg.P(cx + s * 0.0f, cy - s * 0.36f), col, pg.S(s * 0.07f));
    dl->AddBezierCubic(pg.P(cx, cy - s * 0.36f), pg.P(cx + s * 0.25f, cy - s * 0.25f), pg.P(cx + s * 0.32f, cy - s * 0.1f),
                       pg.P(cx + s * 0.20f, cy + s * 0.02f), col, pg.S(s * 0.07f));
}

inline void IconMaskFace(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddEllipseFilled(pg.P(cx, cy), ImVec2(pg.S(s * 0.36f), pg.S(s * 0.42f)), Col(210, 180, 230));
    dl->AddEllipse(pg.P(cx, cy), ImVec2(pg.S(s * 0.36f), pg.S(s * 0.42f)), cGold, 0, 0, pg.S(2.0f));
    dl->AddEllipseFilled(pg.P(cx - s * 0.14f, cy - s * 0.06f), ImVec2(pg.S(s * 0.09f), pg.S(s * 0.07f)), Col(70, 30, 90));
    dl->AddEllipseFilled(pg.P(cx + s * 0.14f, cy - s * 0.06f), ImVec2(pg.S(s * 0.09f), pg.S(s * 0.07f)), Col(70, 30, 90));
    dl->AddBezierCubic(pg.P(cx - s * 0.16f, cy + s * 0.16f), pg.P(cx - s * 0.06f, cy + s * 0.27f),
                       pg.P(cx + s * 0.06f, cy + s * 0.27f), pg.P(cx + s * 0.16f, cy + s * 0.16f), Col(70, 30, 90),
                       pg.S(2.5f));
}

inline void IconEnvelope(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    ImVec2 a = pg.P(cx - s * 0.42f, cy - s * 0.28f), b = pg.P(cx + s * 0.42f, cy + s * 0.28f);
    dl->AddRectFilled(a, b, Col(250, 244, 230), pg.S(3));
    dl->AddRect(a, b, Col(170, 140, 110), pg.S(3), 0, pg.S(1.5f));
    dl->AddLine(pg.P(cx - s * 0.42f, cy - s * 0.26f), pg.P(cx, cy + s * 0.04f), Col(170, 140, 110), pg.S(1.5f));
    dl->AddLine(pg.P(cx + s * 0.42f, cy - s * 0.26f), pg.P(cx, cy + s * 0.04f), Col(170, 140, 110), pg.S(1.5f));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.04f), pg.S(s * 0.09f), Col(190, 30, 40));
}

inline void IconPendant(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->PathArcTo(pg.P(cx, cy - s * 0.22f), pg.S(s * 0.26f), 3.4f, 6.0f);
    dl->PathStroke(Col(90, 80, 70), 0, pg.S(1.8f));
    IconHeart(pg, cx, cy + s * 0.08f, s * 0.62f, Col(140, 60, 190));
}

inline void IconSun(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 12; i++) {
        float a = i * 3.14159f / 6.0f;
        float px = -sinf(a) * s * 0.07f, py = cosf(a) * s * 0.07f;
        dl->AddTriangleFilled(pg.P(cx + cosf(a) * s * 0.26f + px, cy + sinf(a) * s * 0.26f + py),
                              pg.P(cx + cosf(a) * s * 0.46f, cy + sinf(a) * s * 0.46f),
                              pg.P(cx + cosf(a) * s * 0.26f - px, cy + sinf(a) * s * 0.26f - py), Col(240, 170, 40));
    }
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.28f), Col(250, 205, 60));
    dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.28f), Col(200, 120, 20), 0, pg.S(1.5f));
    dl->AddCircleFilled(pg.P(cx - s * 0.09f, cy - s * 0.04f), pg.S(s * 0.035f), Col(140, 70, 10));
    dl->AddCircleFilled(pg.P(cx + s * 0.09f, cy - s * 0.04f), pg.S(s * 0.035f), Col(140, 70, 10));
    dl->AddBezierCubic(pg.P(cx - s * 0.1f, cy + s * 0.08f), pg.P(cx - s * 0.04f, cy + s * 0.14f),
                       pg.P(cx + s * 0.04f, cy + s * 0.14f), pg.P(cx + s * 0.1f, cy + s * 0.08f), Col(140, 70, 10),
                       pg.S(1.5f));
}

inline void IconMoon(const Page& pg, float cx, float cy, float s, ImU32 bg) {
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.40f), Col(235, 235, 255));
    pg.dl->AddCircleFilled(pg.P(cx + s * 0.16f, cy - s * 0.10f), pg.S(s * 0.34f), bg);
}

inline void IconClock(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.42f), Col(250, 244, 230));
    dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.42f), Col(110, 50, 140), 0, pg.S(2.5f));
    dl->AddLine(pg.P(cx, cy), pg.P(cx, cy - s * 0.28f), Col(110, 50, 140), pg.S(2.2f));
    dl->AddLine(pg.P(cx, cy), pg.P(cx + s * 0.2f, cy + s * 0.06f), Col(110, 50, 140), pg.S(2.2f));
}

inline void IconPin(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddTriangleFilled(pg.P(cx - s * 0.26f, cy - s * 0.04f), pg.P(cx + s * 0.26f, cy - s * 0.04f),
                          pg.P(cx, cy + s * 0.45f), Col(130, 50, 170));
    dl->AddCircleFilled(pg.P(cx, cy - s * 0.14f), pg.S(s * 0.29f), Col(130, 50, 170));
    dl->AddCircleFilled(pg.P(cx, cy - s * 0.14f), pg.S(s * 0.11f), Col(250, 244, 230));
}

inline void IconChest(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.12f), pg.P(cx + s * 0.42f, cy + s * 0.32f), Col(150, 90, 40), pg.S(2));
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.34f), pg.P(cx + s * 0.42f, cy - s * 0.10f), Col(176, 110, 50),
                      pg.S(6), ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(pg.P(cx - s * 0.42f, cy - s * 0.13f), pg.P(cx + s * 0.42f, cy - s * 0.07f), cGold);
    dl->AddRectFilled(pg.P(cx - s * 0.08f, cy - s * 0.18f), pg.P(cx + s * 0.08f, cy + s * 0.04f), cGold, pg.S(2));
    dl->AddRect(pg.P(cx - s * 0.42f, cy - s * 0.34f), pg.P(cx + s * 0.42f, cy + s * 0.32f), Col(90, 50, 20), pg.S(3), 0,
                pg.S(1.5f));
}

inline void IconKey(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddCircle(pg.P(cx - s * 0.2f, cy), pg.S(s * 0.16f), cGold, 0, pg.S(s * 0.08f));
    dl->AddLine(pg.P(cx - s * 0.04f, cy), pg.P(cx + s * 0.4f, cy), cGold, pg.S(s * 0.09f));
    dl->AddLine(pg.P(cx + s * 0.26f, cy), pg.P(cx + s * 0.26f, cy + s * 0.14f), cGold, pg.S(s * 0.08f));
    dl->AddLine(pg.P(cx + s * 0.38f, cy), pg.P(cx + s * 0.38f, cy + s * 0.12f), cGold, pg.S(s * 0.08f));
}

inline void IconBottle(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.08f, cy - s * 0.42f), pg.P(cx + s * 0.08f, cy - s * 0.32f), Col(150, 100, 60), pg.S(2));
    dl->AddRectFilled(pg.P(cx - s * 0.1f, cy - s * 0.32f), pg.P(cx + s * 0.1f, cy - s * 0.14f), Col(210, 235, 250, 200));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.12f), pg.S(s * 0.28f), Col(210, 235, 250, 200));
    dl->AddCircleFilled(pg.P(cx, cy + s * 0.16f), pg.S(s * 0.2f), Col(250, 250, 245));
    dl->AddCircle(pg.P(cx, cy + s * 0.12f), pg.S(s * 0.28f), Col(120, 150, 170), 0, pg.S(1.5f));
}

inline void IconScroll(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(cx - s * 0.34f, cy - s * 0.26f), pg.P(cx + s * 0.34f, cy + s * 0.26f), Col(240, 220, 170));
    dl->AddCircleFilled(pg.P(cx - s * 0.34f, cy), pg.S(s * 0.1f), Col(200, 160, 100));
    dl->AddCircleFilled(pg.P(cx + s * 0.34f, cy), pg.S(s * 0.1f), Col(200, 160, 100));
    for (int i = 0; i < 3; i++) {
        dl->AddLine(pg.P(cx - s * 0.2f, cy - s * 0.12f + i * s * 0.12f), pg.P(cx + s * 0.2f, cy - s * 0.12f + i * s * 0.12f),
                    Col(150, 110, 60), pg.S(1.2f));
    }
}

inline void IconCouple(const Page& pg, float cx, float cy, float s, ImU32 col) {
    ImDrawList* dl = pg.dl;
    for (int i = 0; i < 2; i++) {
        float x = cx + (i == 0 ? -1 : 1) * s * 0.17f;
        float y = cy + (i == 0 ? -0.04f : 0.04f) * s;
        dl->AddCircleFilled(pg.P(x, y - s * 0.16f), pg.S(s * 0.14f), col);
        dl->AddEllipseFilled(pg.P(x, y + s * 0.22f), ImVec2(pg.S(s * 0.22f), pg.S(s * 0.2f)), col);
    }
}

inline void IconExclamation(const Page& pg, float cx, float cy, float s) {
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.38f), Col(250, 200, 60));
    pg.dl->AddCircle(pg.P(cx, cy), pg.S(s * 0.38f), Col(160, 90, 20), 0, pg.S(1.5f));
    pg.dl->AddRectFilled(pg.P(cx - s * 0.05f, cy - s * 0.24f), pg.P(cx + s * 0.05f, cy + s * 0.06f), Col(120, 50, 10), pg.S(2));
    pg.dl->AddCircleFilled(pg.P(cx, cy + s * 0.17f), pg.S(s * 0.06f), Col(120, 50, 10));
}

inline void IconTower(const Page& pg, float x, float y, float s, ImU32 col) {
    ImDrawList* dl = pg.dl;
    dl->AddRectFilled(pg.P(x - s * 0.12f, y - s * 0.5f), pg.P(x + s * 0.12f, y), col);
    dl->AddTriangleFilled(pg.P(x - s * 0.16f, y - s * 0.5f), pg.P(x, y - s * 0.78f), pg.P(x + s * 0.16f, y - s * 0.5f), col);
    dl->AddRectFilled(pg.P(x - s * 0.5f, y - s * 0.22f), pg.P(x - s * 0.18f, y), col);
    dl->AddTriangleFilled(pg.P(x - s * 0.54f, y - s * 0.22f), pg.P(x - s * 0.34f, y - s * 0.42f), pg.P(x - s * 0.14f, y - s * 0.22f), col);
    dl->AddRectFilled(pg.P(x + s * 0.18f, y - s * 0.3f), pg.P(x + s * 0.46f, y), col);
    dl->AddTriangleFilled(pg.P(x + s * 0.14f, y - s * 0.3f), pg.P(x + s * 0.32f, y - s * 0.5f), pg.P(x + s * 0.5f, y - s * 0.3f), col);
    dl->AddCircleFilled(pg.P(x, y - s * 0.34f), pg.S(s * 0.06f), Col(255, 220, 120));
}

enum class StepIcon { Envelope, Pendant, Sun, Mask, Heart, Key, Bottle, Book, Scroll, Couple, Exclamation };

inline StepIcon IconForText(const std::string& text, bool last) {
    auto has = [&](const char* word) { return text.find(word) != std::string::npos; };
    if (has("Letter") || has("Mail") || has("carta")) return StepIcon::Envelope;
    if (has("Pendant")) return StepIcon::Pendant;
    if (has("Sun")) return StepIcon::Sun;
    if (has("Piece of Heart")) return StepIcon::Heart;
    if (has("Mask") || has("Hat") || has("Hood")) return StepIcon::Mask;
    if (has("Key")) return StepIcon::Key;
    if (has("Milk") || has("garrafa") || has("Chateau")) return StepIcon::Bottle;
    if (has("Notebook")) return StepIcon::Book;
    if (has("código")) return StepIcon::Scroll;
    return last ? StepIcon::Couple : StepIcon::Exclamation;
}

// The game's own icon for a step, when it has one; it comes from the player's files, so an HD texture pack
// replaces it too.
inline const char* GameIconPath(StepIcon icon) {
    switch (icon) {
        case StepIcon::Envelope: return (const char*)gItemIcons[ITEM_LETTER_TO_KAFEI];
        case StepIcon::Pendant: return (const char*)gItemIcons[ITEM_PENDANT_OF_MEMORIES];
        case StepIcon::Heart: return (const char*)gItemIcons[ITEM_HEART_PIECE];
        case StepIcon::Key: return (const char*)gItemIcons[ITEM_ROOM_KEY];
        case StepIcon::Bottle: return (const char*)gItemIcons[ITEM_BOTTLE];
        case StepIcon::Book: return (const char*)gItemIcons[ITEM_BOMBERS_NOTEBOOK];
        case StepIcon::Scroll: return (const char*)gItemIcons[ITEM_DEED_LAND];
        default: return nullptr;
    }
}

inline bool GameIcon(const Page& pg, const char* path, float cx, float cy, float s) {
    ImTextureID id = Texture(path);
    if (id == nullptr) {
        return false;
    }
    pg.dl->AddImage(id, pg.P(cx - s / 2, cy - s / 2), pg.P(cx + s / 2, cy + s / 2));
    return true;
}

inline void DrawStepIcon(const Page& pg, StepIcon icon, float cx, float cy, float s, ImU32 tint = Col(120, 60, 160)) {
    if (GameIcon(pg, GameIconPath(icon), cx, cy, s * 0.95f)) {
        return;
    }
    switch (icon) {
        case StepIcon::Envelope: IconEnvelope(pg, cx, cy, s); break;
        case StepIcon::Pendant: IconPendant(pg, cx, cy, s); break;
        case StepIcon::Sun: IconSun(pg, cx, cy, s); break;
        case StepIcon::Mask: IconMaskFace(pg, cx, cy, s); break;
        case StepIcon::Heart: IconHeart(pg, cx, cy, s); break;
        case StepIcon::Key: IconKey(pg, cx, cy, s); break;
        case StepIcon::Bottle: IconBottle(pg, cx, cy, s); break;
        case StepIcon::Book: IconBook(pg, cx, cy, s); break;
        case StepIcon::Scroll: IconScroll(pg, cx, cy, s); break;
        case StepIcon::Couple: IconCouple(pg, cx, cy, s, tint); break;
        case StepIcon::Exclamation: IconExclamation(pg, cx, cy, s); break;
    }
}

inline void Tick(const Page& pg, float cx, float cy, float s) {
    ImDrawList* dl = pg.dl;
    ImVec2 a = pg.P(cx - s * 0.36f, cy), b = pg.P(cx - s * 0.08f, cy + s * 0.3f), c = pg.P(cx + s * 0.42f, cy - s * 0.34f);
    dl->AddLine(a, b, Col(20, 70, 20), pg.S(s * 0.26f));
    dl->AddLine(b, c, Col(20, 70, 20), pg.S(s * 0.26f));
    dl->AddLine(a, b, cGreen, pg.S(s * 0.16f));
    dl->AddLine(b, c, cGreen, pg.S(s * 0.16f));
}

inline void CheckSquare(const Page& pg, float x, float y, float s, bool done) {
    pg.dl->AddRectFilled(pg.P(x, y), pg.P(x + s, y + s), Col(30, 14, 40, 200), pg.S(3));
    pg.dl->AddRect(pg.P(x, y), pg.P(x + s, y + s), Col(230, 220, 200), pg.S(3), 0, pg.S(2));
    if (done) {
        Tick(pg, x + s * 0.55f, y + s * 0.45f, s * 1.1f);
    }
}

inline void Picture(const Page& pg, const char* path, float x, float y, float s, float rounding, float alpha = 1.0f) {
    ImTextureID id = Texture(path);
    if (id == nullptr) {
        pg.dl->AddRectFilled(pg.P(x, y), pg.P(x + s, y + s), Col(90, 60, 110), pg.S(rounding));
        return;
    }
    pg.dl->AddImageRounded(id, pg.P(x, y), pg.P(x + s, y + s), ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, (int)(alpha * 255)), pg.S(rounding));
}

// An invisible button over a page rectangle; returns true when clicked.
inline bool Hit(const Page& pg, const char* id, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGui::SetCursorScreenPos(pg.P(x, y));
    bool clicked = ImGui::InvisibleButton(id, ImVec2(pg.S(w), pg.S(h)));
    if (hovered) {
        *hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    }
    return clicked;
}

// ----- PNG art from assets/custom/overlays/questmenu, loaded once through the GUI texture manager.

// Loads overlays/questmenu/<name>.png from the game's archives on first use. Returns nullptr when the file is
// missing (an old 2ship.o2r), so callers fall back to the drawn shapes.
ImTextureID Asset(const char* name);
ImVec2 AssetSize(const char* name);

// Draws a PNG as a nine-slice box: corners keep their size (border, in texture pixels, drawn at `border` page
// units), edges and centre stretch.
bool NineSlice(const Page& pg, const char* name, ImVec2 a, ImVec2 b, float texBorder, float border,
               ImU32 tint = IM_COL32_WHITE);

// Draws a PNG stretched over a rectangle, or tiled when `tile` is set (tile size in page units).
bool Png(const Page& pg, const char* name, ImVec2 a, ImVec2 b, ImU32 tint = IM_COL32_WHITE, float tile = 0.0f);

} // namespace QuestUi

#endif // QUEST_MENU_RENDERER_H
