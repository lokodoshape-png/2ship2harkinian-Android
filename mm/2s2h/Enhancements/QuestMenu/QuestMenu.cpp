#include "QuestMenu.h"

#include "QuestData.h"
#include "QuestRenderer.h"
#include "QuestState.h"

#include <algorithm>
#include <cmath>
#include <SDL2/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h>

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/Controls/ButtonPrompts.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

using namespace QuestUi;

namespace QuestMenu {

namespace {

constexpr int kCategories = (int)Category::Count;

// ----- Layout of the 1100x700 page.

constexpr float kSideX0 = 28, kSideX1 = 262, kSideY0 = 108, kSideButtonH = 64, kSideGap = 8;
constexpr float kListX0 = 276, kListX1 = 606, kListY0 = 104, kListY1 = 616;
constexpr float kRowsY0 = 166, kRowsY1 = 602, kRowH = 54;
constexpr float kDetX0 = 620, kDetX1 = 1072, kDetY0 = 104, kDetY1 = 616;
constexpr float kDetContentY0 = 124, kDetContentY1 = 560;

// ----- State. The game thread (input hook) and the GUI draw run one after the other on the same thread.

bool sOpen = false;
int sPendingOpen = 0;      // frames left to wait for the pause menu after RequestOpen
bool sStartInjected = false;
float sAnim = 0.0f;        // 0 closed .. 1 open
float sTime = 0.0f;
int sCategory = 0;
int sSelected[kCategories] = {};
float sListScroll[kCategories] = {};
float sCursorY = -1.0f;    // highlight position, follows the selection
bool sDetailFocus = false;
float sDetailScroll = 0.0f, sDetailScrollTarget = 0.0f, sDetailContentH = 0.0f;
float sDetailFade = 1.0f;
int sRepeatDir = 0, sRepeatTimer = 0;
bool sBackHeld = false;

int QuestCount(int category) {
    return (int)Quests((Category)category).size();
}

void SelectionChanged() {
    sDetailScroll = sDetailScrollTarget = 0.0f;
    sDetailFade = 0.0f;
}

void ChangeCategory(int delta) {
    sCategory = (sCategory + delta + kCategories) % kCategories;
    sDetailFocus = false;
    sCursorY = -1.0f;
    SelectionChanged();
    Audio_PlaySfx(NA_SE_SY_WIN_SCROLL_LEFT);
}

void Move(int delta) {
    if (sDetailFocus) {
        sDetailScrollTarget += delta * 46.0f;
        return;
    }
    int count = QuestCount(sCategory);
    if (count == 0) {
        return;
    }
    int next = std::clamp(sSelected[sCategory] + delta, 0, count - 1);
    if (next != sSelected[sCategory]) {
        sSelected[sCategory] = next;
        SelectionChanged();
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
}

bool PauseReady() {
    if (gPlayState == nullptr || !SaveLoaded()) {
        return false;
    }
    PauseContext* pauseCtx = &gPlayState->pauseCtx;
    return pauseCtx->state == PAUSE_STATE_MAIN && pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE &&
           pauseCtx->debugEditor == DEBUG_EDITOR_NONE;
}

void Open() {
    sOpen = true;
    sPendingOpen = 0;
    sDetailFocus = false;
    sCursorY = -1.0f;
    SelectionChanged();
    Audio_PlaySfx(NA_SE_SY_DECIDE);
}

// Reads the game's controller while the menu is open. The presses are then cleared so that nothing behind the menu
// reacts to them (the pause menu is also held still through VB_KALEIDO_UPDATE).
void HandlePad(Input* input) {
    u16 press = input->press.button;
    s8 stickY = input->cur.stick_y, stickX = input->rel.stick_x;

    if (CHECK_BTN_ANY(press, BTN_START)) {
        Close();
        return;
    }
    if (CHECK_BTN_ANY(press, BTN_B)) {
        if (sDetailFocus) {
            sDetailFocus = false;
            Audio_PlaySfx(NA_SE_SY_CANCEL);
        } else {
            Close();
        }
        return;
    }
    if (CHECK_BTN_ANY(press, BTN_A) && !sDetailFocus && QuestCount(sCategory) > 0) {
        sDetailFocus = true;
        Audio_PlaySfx(NA_SE_SY_DECIDE);
    }
    if (CHECK_BTN_ANY(press, BTN_L | BTN_Z)) {
        ChangeCategory(-1);
    } else if (CHECK_BTN_ANY(press, BTN_R)) {
        ChangeCategory(1);
    } else if (!sDetailFocus && (CHECK_BTN_ANY(press, BTN_DLEFT) || stickX < -30)) {
        ChangeCategory(-1);
    } else if (!sDetailFocus && (CHECK_BTN_ANY(press, BTN_DRIGHT) || stickX > 30)) {
        ChangeCategory(1);
    }

    int dir = 0;
    if (stickY > 30 || CHECK_BTN_ANY(input->cur.button, BTN_DUP)) {
        dir = -1;
    } else if (stickY < -30 || CHECK_BTN_ANY(input->cur.button, BTN_DDOWN)) {
        dir = 1;
    }
    if (dir == 0) {
        sRepeatDir = 0;
    } else if (sDetailFocus) {
        sDetailScrollTarget += dir * 14.0f; // smooth scroll while held
    } else if (dir != sRepeatDir) {
        sRepeatDir = dir;
        sRepeatTimer = 8;
        Move(dir);
    } else if (--sRepeatTimer <= 0) {
        sRepeatTimer = 3;
        Move(dir);
    }
}

void ClearInput(Input* input) {
    input->press.button = 0;
    input->rel.button = 0;
    input->rel.stick_x = input->rel.stick_y = 0;
    input->press.stick_x = input->press.stick_y = 0;
}

void OnGameFrame() {
    if (gPlayState == nullptr) {
        sOpen = false;
        sPendingOpen = 0;
        return;
    }
    Input* input = CONTROLLER1(&gPlayState->state);
    PauseContext* pauseCtx = &gPlayState->pauseCtx;

    if (sPendingOpen > 0) {
        sPendingOpen--;
        if (PauseReady()) {
            Open();
        } else if (pauseCtx->state == PAUSE_STATE_OFF && !sStartInjected) {
            // Pause the game the way the player would, so all of the game's own checks apply.
            input->press.button |= BTN_START;
            sStartInjected = true;
        }
    }
    if (!sOpen) {
        return;
    }
    if (pauseCtx->state != PAUSE_STATE_MAIN) {
        sOpen = false;
        return;
    }
    HandlePad(input);
    ClearInput(input);
}

// N on the keyboard, or Back / View / Select / - on a controller.
bool ShortcutPressed() {
    bool pressed = false;
    if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        pressed = true;
    }
    // With controller navigation of the menus on, Back opens the settings menu instead.
    bool held = false;
    if (!CVarGetInteger(CVAR_IMGUI_CONTROLLER_NAV, 0)) {
        auto gamepads = Ship::Context::GetInstance()
                            ->GetControlDeck()
                            ->GetConnectedPhysicalDeviceManager()
                            ->GetConnectedSDLGamepadsForPort(0);
        for (const auto& [id, gamepad] : gamepads) {
            held = held || SDL_GameControllerGetButton(gamepad, SDL_CONTROLLER_BUTTON_BACK);
        }
    }
    pressed = pressed || (held && !sBackHeld);
    sBackHeld = held;
    return pressed;
}

// ----- Drawing.

float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

ImU32 Alpha(ImU32 col, float a) {
    int alpha = (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
    return (col & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}

// A button prompt: the icon of the physical button when the button icons are on, otherwise an N64-style badge.
void Prompt(const Page& pg, ButtonPrompt prompt, float cx, float cy, float s) {
    const char* tex = ButtonPrompts_Icon(prompt);
    if (tex != nullptr && GameIcon(pg, tex, cx, cy, s)) {
        return;
    }
    ImU32 face = Col(120, 120, 130);
    const char* label = "";
    switch (prompt) {
        case BUTTON_PROMPT_A: face = Col(60, 90, 220); label = "A"; break;
        case BUTTON_PROMPT_B: face = Col(40, 160, 60); label = "B"; break;
        case BUTTON_PROMPT_L: label = "L"; break;
        case BUTTON_PROMPT_R: label = "R"; break;
        case BUTTON_PROMPT_Z: label = "Z"; break;
        case BUTTON_PROMPT_START: face = Col(200, 40, 40); label = "S"; break;
        case BUTTON_PROMPT_STICK: face = Col(150, 150, 160); label = "+"; break;
        default: break;
    }
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.5f), Col(20, 10, 20, 200));
    pg.dl->AddCircleFilled(pg.P(cx, cy), pg.S(s * 0.44f), face);
    float size = s * 0.62f;
    TextCentered(pg, cx, cy - size * 0.55f, size, Col(255, 255, 255), label, false);
}

void ProgressBar(const Page& pg, float x0, float y0, float x1, float y1, float fraction, ImU32 fill) {
    float r = (y1 - y0) * 0.5f;
    pg.dl->AddRectFilled(pg.P(x0, y0), pg.P(x1, y1), Col(30, 14, 40, 200), pg.S(r));
    if (fraction > 0.0f) {
        float xf = x0 + (x1 - x0) * std::clamp(fraction, 0.0f, 1.0f);
        pg.dl->AddRectFilled(pg.P(x0, y0), pg.P(std::max(xf, x0 + r * 2), y1), fill, pg.S(r));
    }
    pg.dl->AddRect(pg.P(x0, y0), pg.P(x1, y1), Col(150, 105, 40, 200), pg.S(r), 0, pg.S(1));
}

// A quest's picture: the game's icon (from the player's files), our PNG, or the category's.
void QuestIcon(const Page& pg, const Quest& quest, float cx, float cy, float s, float alpha) {
    ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255));
    if (quest.icon != nullptr) {
        ImTextureID id = Texture(quest.icon);
        if (id != nullptr) {
            pg.dl->AddImage(id, pg.P(cx - s / 2, cy - s / 2), pg.P(cx + s / 2, cy + s / 2), ImVec2(0, 0),
                            ImVec2(1, 1), tint);
            return;
        }
    }
    const char* asset = quest.iconAsset != nullptr ? quest.iconAsset : CategoryAsset((Category)sCategory);
    Png(pg, asset, ImVec2(cx - s / 2, cy - s / 2), ImVec2(cx + s / 2, cy + s / 2), tint);
}

ImU32 StatusColor(Status status) {
    switch (status) {
        case Status::Locked: return Col(150, 140, 150);
        case Status::Available: return Col(255, 226, 140);
        case Status::InProgress: return Col(110, 190, 255);
        case Status::Done: return cGreen;
    }
    return cGold;
}

void DrawBackdrop(const Page& pg, float a) {
    if (!Png(pg, "background", ImVec2(0, 0), ImVec2(kPageW, kPageH), Alpha(IM_COL32_WHITE, a), 128.0f)) {
        Panel(pg, ImVec2(0, 0), ImVec2(kPageW, kPageH), Alpha(cPurple, a), Alpha(cPurpleDeep, a), 18);
    }
    if (!NineSlice(pg, "frame", ImVec2(0, 0), ImVec2(kPageW, kPageH), 64, 44, Alpha(IM_COL32_WHITE, a))) {
        GoldFrame(pg, ImVec2(6, 6), ImVec2(kPageW - 6, kPageH - 6), 18, 5);
    }
}

void DrawBanner(const Page& pg, float a) {
    if (!Png(pg, "banner", ImVec2(166, 4), ImVec2(934, 94), Alpha(IM_COL32_WHITE, a))) {
        Panel(pg, ImVec2(230, 16), ImVec2(870, 84), cWine, cPurpleDeep, 12);
        GoldFrame(pg, ImVec2(230, 16), ImVec2(870, 84), 12, 3);
    }
    TextCentered(pg, kPageW / 2, 24, 36, Alpha(cGold, a), "MISSÕES");
    // Category switch prompts, also clickable.
    Prompt(pg, BUTTON_PROMPT_L, 110, 50, 40);
    Prompt(pg, BUTTON_PROMPT_R, 990, 50, 40);
    if (Hit(pg, "##qm_prev", 86, 26, 48, 48)) {
        ChangeCategory(-1);
    }
    if (Hit(pg, "##qm_next", 966, 26, 48, 48)) {
        ChangeCategory(1);
    }
}

void DrawSidebar(const Page& pg, float a) {
    float pulse = 0.5f + 0.5f * sinf(sTime * 4.0f);
    for (int c = 0; c < kCategories; c++) {
        float y0 = kSideY0 + c * (kSideButtonH + kSideGap), y1 = y0 + kSideButtonH;
        bool selected = c == sCategory;
        bool hovered = false;
        ImGui::PushID(c);
        if (Hit(pg, "##qm_cat", kSideX0, y0, kSideX1 - kSideX0, kSideButtonH, &hovered) && !selected) {
            ChangeCategory(c - sCategory);
        }
        ImGui::PopID();

        float x0 = kSideX0 + (selected ? 0.0f : 8.0f);
        if (selected) {
            for (int i = 3; i >= 1; i--) {
                pg.dl->AddRect(pg.P(x0 - i * 2, y0 - i * 2), pg.P(kSideX1 + i * 2, y1 + i * 2),
                               Col(255, 210, 90, (int)((20 + 30 * pulse) * (4 - i) * a)), pg.S(12 + i * 2), 0,
                               pg.S(3));
            }
        }
        if (!NineSlice(pg, selected ? "button_selected" : "button", ImVec2(x0, y0), ImVec2(kSideX1, y1), 28, 20,
                       Alpha(hovered && !selected ? Col(255, 240, 220) : IM_COL32_WHITE, a))) {
            Panel(pg, ImVec2(x0, y0), ImVec2(kSideX1, y1), selected ? cWine : cPurpleLight, cPurpleDeep, 10);
            GoldFrame(pg, ImVec2(x0, y0), ImVec2(kSideX1, y1), 10, selected ? 3.0f : 1.5f);
        }
        Png(pg, CategoryAsset((Category)c), ImVec2(x0 + 10, y0 + 8), ImVec2(x0 + 58, y0 + 56),
              Alpha(IM_COL32_WHITE, a));
        Text(pg, x0 + 66, y0 + 12, 17, Alpha(selected ? Col(255, 236, 170) : cParchment, a),
             CategoryName((Category)c), kSideX1 - x0 - 74);

        // How many are done in this category.
        if (SaveLoaded()) {
            int done = 0;
            const auto& quests = Quests((Category)c);
            for (const Quest& q : quests) {
                done += Evaluate(q).status == Status::Done ? 1 : 0;
            }
            char count[24];
            snprintf(count, sizeof(count), "%d / %d", done, (int)quests.size());
            Text(pg, x0 + 66, y0 + 38, 13, Alpha(Col(220, 190, 140), a), count);
        }
    }
}

void DrawList(const Page& pg, float a, const std::vector<QuestProgress>& progress) {
    if (!NineSlice(pg, "frame", ImVec2(kListX0, kListY0), ImVec2(kListX1, kListY1), 64, 30, Alpha(IM_COL32_WHITE, a))) {
        GoldFrame(pg, ImVec2(kListX0, kListY0), ImVec2(kListX1, kListY1), 12, 3);
    }
    pg.dl->AddRectFilled(pg.P(kListX0 + 14, kListY0 + 14), pg.P(kListX1 - 14, kListY1 - 14),
                         Col(20, 8, 30, (int)(150 * a)), pg.S(8));
    TextShadow(pg, kListX0 + 22, kListY0 + 18, 21, Alpha(cGold, a), CategoryName((Category)sCategory));
    Png(pg, "divider", ImVec2(kListX0 + 16, kListY0 + 48), ImVec2(kListX1 - 16, kListY0 + 60), Alpha(IM_COL32_WHITE, a));

    const auto& quests = Quests((Category)sCategory);
    int count = (int)quests.size();
    if (count == 0) {
        TextCentered(pg, (kListX0 + kListX1) / 2, 300, 16, cParchment, "Nada por aqui ainda.");
        return;
    }
    int& selected = sSelected[sCategory];
    selected = std::clamp(selected, 0, count - 1);

    // Keep the selection in view, scrolling smoothly.
    float viewH = kRowsY1 - kRowsY0;
    float maxScroll = std::max(0.0f, count * kRowH - viewH);
    float& scroll = sListScroll[sCategory];
    float want = scroll;
    float selTop = selected * kRowH, selBottom = selTop + kRowH;
    if (selTop < want + kRowH * 0.5f) {
        want = selTop - kRowH * 0.5f;
    } else if (selBottom > want + viewH - kRowH * 0.5f) {
        want = selBottom - viewH + kRowH * 0.5f;
    }
    want = std::clamp(want, 0.0f, maxScroll);
    float dt = ImGui::GetIO().DeltaTime;
    scroll += (want - scroll) * std::min(1.0f, dt * 14.0f);

    // Mouse wheel over the list moves the selection.
    if (ImGui::IsMouseHoveringRect(pg.P(kListX0, kRowsY0), pg.P(kListX1, kRowsY1)) && ImGui::GetIO().MouseWheel != 0) {
        Move(ImGui::GetIO().MouseWheel > 0 ? -1 : 1);
    }

    pg.dl->PushClipRect(pg.P(kListX0 + 14, kRowsY0), pg.P(kListX1 - 14, kRowsY1), true);
    float targetY = kRowsY0 + selTop - scroll;
    if (sCursorY < 0) {
        sCursorY = targetY;
    }
    sCursorY += (targetY - sCursorY) * std::min(1.0f, dt * 18.0f);
    float pulse = 0.5f + 0.5f * sinf(sTime * 5.0f);
    float hx0 = kListX0 + 18, hx1 = kListX1 - 18;
    pg.dl->AddRectFilled(pg.P(hx0, sCursorY + 2), pg.P(hx1, sCursorY + kRowH - 2),
                         Col(255, 200, 90, (int)((40 + 30 * pulse) * a)), pg.S(8));
    pg.dl->AddRect(pg.P(hx0, sCursorY + 2), pg.P(hx1, sCursorY + kRowH - 2),
                   Col(255, 220, 120, (int)((150 + 100 * pulse) * a)), pg.S(8), 0, pg.S(sDetailFocus ? 1.5f : 2.5f));

    int first = std::max(0, (int)(scroll / kRowH));
    int last = std::min(count - 1, (int)((scroll + viewH) / kRowH) + 1);
    for (int i = first; i <= last; i++) {
        const Quest& q = quests[i];
        const QuestProgress& p = progress[i];
        float y = kRowsY0 + i * kRowH - scroll;
        bool locked = p.status == Status::Locked;
        float rowAlpha = a * (locked ? 0.55f : 1.0f);

        ImGui::PushID(i);
        if (y + kRowH > kRowsY0 && y < kRowsY1 &&
            Hit(pg, "##qm_row", hx0, std::max(y, kRowsY0), hx1 - hx0, std::min(kRowH, kRowsY1 - std::max(y, kRowsY0)))) {
            if (selected == i) {
                sDetailFocus = true;
            } else {
                selected = i;
                SelectionChanged();
            }
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
        ImGui::PopID();

        QuestIcon(pg, q, kListX0 + 44, y + kRowH / 2, 38, rowAlpha);
        Text(pg, kListX0 + 72, y + 8, 16, Alpha(i == selected ? Col(255, 244, 210) : cParchment, rowAlpha),
             q.name.c_str(), 0);
        if (p.total > 0 && !locked) {
            ProgressBar(pg, kListX0 + 72, y + 34, kListX1 - 70, y + 41, (float)p.done / p.total,
                        Alpha(StatusColor(p.status), a));
        } else if (locked) {
            Text(pg, kListX0 + 72, y + 30, 13, Alpha(Col(200, 180, 200), a), "Bloqueada");
        }
        Png(pg, StatusAsset(p.status), ImVec2(kListX1 - 58, y + 13), ImVec2(kListX1 - 30, y + 41),
              Alpha(IM_COL32_WHITE, a));
    }
    pg.dl->PopClipRect();

    // Scroll bar.
    if (maxScroll > 0) {
        float bx = kListX1 - 20, h = viewH * viewH / (count * kRowH);
        float by = kRowsY0 + (viewH - h) * (scroll / maxScroll);
        pg.dl->AddRectFilled(pg.P(bx, kRowsY0), pg.P(bx + 4, kRowsY1), Col(0, 0, 0, (int)(90 * a)), pg.S(2));
        pg.dl->AddRectFilled(pg.P(bx, by), pg.P(bx + 4, by + h), Alpha(cGold, a), pg.S(2));
    }
}

// The details are laid out twice per frame at most: once to measure (draw = false), then drawn with the scroll.
float DetailBody(const Page& pg, const Quest& q, const QuestProgress& p, float y, bool draw, float a) {
    const float x0 = kDetX0 + 26, x1 = kDetX1 - 26, w = x1 - x0;
    ImU32 ink = Alpha(cInk, a), soft = Alpha(Col(110, 72, 40), a), wine = Alpha(cWine, a);
    bool locked = p.status == Status::Locked;

    // Header: picture, name, region and time.
    if (draw) {
        pg.dl->AddCircleFilled(pg.P(x0 + 34, y + 34), pg.S(36), Col(60, 30, 20, (int)(40 * a)));
        QuestIcon(pg, q, x0 + 34, y + 34, 60, a * (locked ? 0.5f : 1.0f));
    }
    float tx = x0 + 82, tw = x1 - tx;
    float ty = y + 2;
    if (draw) {
        Text(pg, tx, ty, 24, ink, q.name.c_str(), tw);
    }
    ty += WrappedHeight(pg, 24, tw, q.name.c_str()) + 2;
    if (!q.region.empty()) {
        if (draw) {
            IconPin(pg, tx + 7, ty + 9, 14);
            Text(pg, tx + 20, ty, 15, soft, q.region.c_str(), tw - 20);
        }
        ty += WrappedHeight(pg, 15, tw - 20, q.region.c_str()) + 2;
    }
    if (!q.when.empty()) {
        if (draw) {
            IconClock(pg, tx + 7, ty + 9, 14);
            Text(pg, tx + 20, ty, 15, soft, q.when.c_str(), tw - 20);
        }
        ty += WrappedHeight(pg, 15, tw - 20, q.when.c_str()) + 2;
    }
    y = std::max(y + 76, ty + 6);
    if (draw) {
        Png(pg, "divider", ImVec2(x0 - 6, y), ImVec2(x1 + 6, y + 12), Alpha(Col(150, 100, 60), a));
    }
    y += 20;

    if (locked) {
        const char* hint = q.lockedHint.empty() ? "Continue a aventura para liberar." : q.lockedHint.c_str();
        float h = WrappedHeight(pg, 16, w - 60, hint) + 20;
        if (draw) {
            pg.dl->AddRectFilled(pg.P(x0, y), pg.P(x1, y + h), Col(90, 40, 60, (int)(40 * a)), pg.S(8));
            pg.dl->AddRect(pg.P(x0, y), pg.P(x1, y + h), Alpha(Col(140, 90, 60), a), pg.S(8), 0, pg.S(1.5f));
            Png(pg, "state_locked", ImVec2(x0 + 8, y + h / 2 - 18), ImVec2(x0 + 44, y + h / 2 + 18),
                  Alpha(IM_COL32_WHITE, a));
            Text(pg, x0 + 52, y + 10, 16, wine, hint, w - 60);
        }
        y += h + 14;
    }

    if (!q.description.empty()) {
        if (draw) {
            Text(pg, x0, y, 16, ink, q.description.c_str(), w);
        }
        y += WrappedHeight(pg, 16, w, q.description.c_str()) + 14;
    }

    if (!q.objectives.empty()) {
        char head[48];
        if (p.total > 0) {
            snprintf(head, sizeof(head), "Objetivos  (%d / %d)", std::min(p.done, p.total), p.total);
        } else {
            snprintf(head, sizeof(head), "Objetivos");
        }
        if (draw) {
            Text(pg, x0, y, 18, wine, head);
        }
        y += 26;
        for (size_t i = 0; i < q.objectives.size(); i++) {
            const Objective& o = q.objectives[i];
            bool done = p.objectiveDone[i];
            float h = std::max(26.0f, WrappedHeight(pg, 15, w - 36, o.text.c_str()) + 8);
            if (draw) {
                if (!Png(pg, done ? "mark_done" : "mark_todo", ImVec2(x0, y), ImVec2(x0 + 24, y + 24),
                           Alpha(IM_COL32_WHITE, a))) {
                    CheckSquare(pg, x0 + 2, y + 2, 20, done);
                }
                Text(pg, x0 + 34, y + 3, 15, done ? Alpha(Col(70, 110, 50), a) : ink, o.text.c_str(), w - 36);
            }
            y += h;
        }
        y += 8;
    }

    if (q.counter.value) {
        char line[48];
        snprintf(line, sizeof(line), "Progresso: %d / %d", locked ? 0 : p.counterValue, q.counter.max);
        if (draw) {
            Text(pg, x0, y, 16, wine, line);
            ProgressBar(pg, x0, y + 24, x1, y + 34, locked ? 0.0f : (float)p.counterValue / q.counter.max,
                        Alpha(StatusColor(p.status == Status::Done ? Status::Done : Status::InProgress), a));
        }
        y += 46;
    }

    if (!q.reward.empty()) {
        std::string line = "Recompensa: " + q.reward;
        float h = WrappedHeight(pg, 15, w - 34, line.c_str());
        if (draw) {
            IconChest(pg, x0 + 12, y + 10, 22);
            Text(pg, x0 + 34, y, 15, Alpha(Col(120, 70, 20), a), line.c_str(), w - 34);
        }
        y += h + 10;
    }
    return y;
}

void DrawDetail(const Page& pg, float a, const std::vector<QuestProgress>& progress) {
    float pulse = 0.5f + 0.5f * sinf(sTime * 4.0f);
    if (sDetailFocus) {
        for (int i = 4; i >= 1; i--) {
            pg.dl->AddRect(pg.P(kDetX0 - i * 3, kDetY0 - i * 3), pg.P(kDetX1 + i * 3, kDetY1 + i * 3),
                           Col(255, 210, 90, (int)((18 + 26 * pulse) * (5 - i) * a)), pg.S(14 + i * 3), 0, pg.S(3));
        }
    }
    if (!Png(pg, "parchment", ImVec2(kDetX0 + 8, kDetY0 + 8), ImVec2(kDetX1 - 8, kDetY1 - 8),
               Alpha(IM_COL32_WHITE, a))) {
        Panel(pg, ImVec2(kDetX0, kDetY0), ImVec2(kDetX1, kDetY1), cParchment, cParchmentDark, 12);
    }
    if (!NineSlice(pg, "frame", ImVec2(kDetX0, kDetY0), ImVec2(kDetX1, kDetY1), 64, 30, Alpha(IM_COL32_WHITE, a))) {
        GoldFrame(pg, ImVec2(kDetX0, kDetY0), ImVec2(kDetX1, kDetY1), 12, 3);
    }
    Png(pg, "corner", ImVec2(kDetX1 - 70, kDetY0 + 4), ImVec2(kDetX1 - 4, kDetY0 + 70), Alpha(IM_COL32_WHITE, a));

    const auto& quests = Quests((Category)sCategory);
    if (quests.empty()) {
        return;
    }
    if (Hit(pg, "##qm_detail", kDetX0, kDetContentY0, kDetX1 - kDetX0, kDetContentY1 - kDetContentY0)) {
        sDetailFocus = true;
    }
    int index = sSelected[sCategory];
    const Quest& q = quests[index];
    const QuestProgress& p = progress[index];

    float dt = ImGui::GetIO().DeltaTime;
    sDetailFade = std::min(1.0f, sDetailFade + dt * 6.0f);
    float fa = a * Ease(sDetailFade);
    float slide = (1.0f - Ease(sDetailFade)) * 16.0f;

    // Scroll: wheel, the stick while focused, or touch drag.
    float viewH = kDetContentY1 - kDetContentY0;
    if (ImGui::IsMouseHoveringRect(pg.P(kDetX0, kDetContentY0), pg.P(kDetX1, kDetContentY1))) {
        sDetailScrollTarget -= ImGui::GetIO().MouseWheel * 50.0f;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            sDetailScrollTarget -= ImGui::GetIO().MouseDelta.y / pg.k;
            sDetailScroll = sDetailScrollTarget;
        }
    }
    float maxScroll = std::max(0.0f, sDetailContentH - viewH);
    sDetailScrollTarget = std::clamp(sDetailScrollTarget, 0.0f, maxScroll);
    sDetailScroll += (sDetailScrollTarget - sDetailScroll) * std::min(1.0f, dt * 14.0f);

    pg.dl->PushClipRect(pg.P(kDetX0 + 12, kDetContentY0), pg.P(kDetX1 - 12, kDetContentY1), true);
    float top = kDetContentY0 + 4 - sDetailScroll + slide;
    float end = DetailBody(pg, q, p, top, true, fa);
    sDetailContentH = end - top + 8;
    pg.dl->PopClipRect();
    if (maxScroll > 0) {
        float bx = kDetX1 - 20, h = viewH * viewH / sDetailContentH;
        float by = kDetContentY0 + (viewH - h) * (sDetailScroll / maxScroll);
        pg.dl->AddRectFilled(pg.P(bx, kDetContentY0), pg.P(bx + 4, kDetContentY1), Col(80, 50, 30, (int)(60 * a)),
                             pg.S(2));
        pg.dl->AddRectFilled(pg.P(bx, by), pg.P(bx + 4, by + h), Alpha(Col(150, 100, 50), a), pg.S(2));
    }

    // Status strip.
    float sy0 = kDetContentY1 + 10, sy1 = kDetY1 - 20;
    pg.dl->AddRectFilled(pg.P(kDetX0 + 22, sy0), pg.P(kDetX1 - 22, sy1), Col(60, 26, 40, (int)(200 * a)), pg.S(10));
    pg.dl->AddRect(pg.P(kDetX0 + 22, sy0), pg.P(kDetX1 - 22, sy1), Alpha(cGoldDark, a), pg.S(10), 0, pg.S(1.5f));
    float cy = (sy0 + sy1) / 2;
    Png(pg, StatusAsset(p.status), ImVec2(kDetX0 + 30, cy - 15), ImVec2(kDetX0 + 60, cy + 15),
          Alpha(IM_COL32_WHITE, a));
    Text(pg, kDetX0 + 68, cy - 10, 18, Alpha(StatusColor(p.status), a), StatusName(p.status));
    if (p.total > 0 && p.status != Status::Locked) {
        ProgressBar(pg, kDetX0 + 230, cy - 5, kDetX1 - 90, cy + 5, (float)p.done / p.total,
                    Alpha(StatusColor(p.status), a));
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", p.done * 100 / p.total);
        Text(pg, kDetX1 - 80, cy - 9, 16, Alpha(cParchment, a), pct);
    }
}

void DrawFooter(const Page& pg, float a) {
    struct Hint {
        ButtonPrompt prompt;
        const char* text;
    };
    Hint hints[] = { { BUTTON_PROMPT_R, "Categoria" },
                     { BUTTON_PROMPT_STICK, sDetailFocus ? "Rolar" : "Escolher" },
                     { BUTTON_PROMPT_A, "Detalhes" },
                     { BUTTON_PROMPT_B, sDetailFocus ? "Voltar" : "Fechar" } };
    float total = 0;
    for (const Hint& h : hints) {
        total += 34 + TextWidth(pg, 16, h.text) + 30;
    }
    total += 34;
    float x = kPageW / 2 - total / 2, cy = 656;
    pg.dl->AddRectFilled(pg.P(x - 20, cy - 22), pg.P(x + total + 4, cy + 22), Col(20, 8, 30, (int)(170 * a)),
                         pg.S(22));
    Prompt(pg, BUTTON_PROMPT_L, x + 12, cy, 28);
    x += 34;
    for (const Hint& h : hints) {
        Prompt(pg, h.prompt, x + 12, cy, 28);
        Text(pg, x + 32, cy - 10, 16, Alpha(cParchment, a), h.text);
        x += 34 + TextWidth(pg, 16, h.text) + 30;
    }
    // Touch: a close button in the corner.
    pg.dl->AddCircleFilled(pg.P(1040, 52), pg.S(20), Col(60, 20, 30, (int)(220 * a)));
    pg.dl->AddCircle(pg.P(1040, 52), pg.S(20), Alpha(cGold, a), 0, pg.S(2));
    pg.dl->AddLine(pg.P(1032, 44), pg.P(1048, 60), Alpha(cGold, a), pg.S(3));
    pg.dl->AddLine(pg.P(1048, 44), pg.P(1032, 60), Alpha(cGold, a), pg.S(3));
    if (Hit(pg, "##qm_close", 1018, 30, 44, 44)) {
        Close();
    }
}

bool GameRect(ImVec2& pos, ImVec2& size) {
    ImGuiWindow* gameWindow = ImGui::FindWindowByName("Main Game");
    if (gameWindow == nullptr || gameWindow->Hidden) {
        return false;
    }
    pos = gameWindow->InnerRect.Min;
    size = gameWindow->InnerRect.GetSize();
    return size.x > 0 && size.y > 0;
}

// The QUESTS button on the pause screen, for touch and mouse.
void DrawPauseButton() {
    if (!PauseReady() || sOpen) {
        return;
    }
    ImVec2 pos, size;
    if (!GameRect(pos, size)) {
        return;
    }
    float h = std::clamp(size.y * 0.075f, 28.0f, 80.0f), w = h * 3.4f;
    ImVec2 at(pos.x + size.x - w - h * 0.4f, pos.y + h * 0.4f);
    ImGui::SetNextWindowPos(at);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##QuestMenuButton", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoMove);
    Page pg{ ImGui::GetWindowDrawList(), at, h / 64.0f };
    float pw = w / pg.k;
    bool hovered = false;
    if (Hit(pg, "##qm_open", 0, 0, pw, 64, &hovered)) {
        Open();
    }
    if (!NineSlice(pg, hovered ? "button_selected" : "button", ImVec2(0, 0), ImVec2(pw, 64), 28, 20)) {
        Panel(pg, ImVec2(0, 0), ImVec2(pw, 64), cWine, cPurpleDeep, 10);
        GoldFrame(pg, ImVec2(0, 0), ImVec2(pw, 64), 10, 2);
    }
    Png(pg, "cat_main", ImVec2(8, 8), ImVec2(56, 56));
    TextShadow(pg, 62, 20, 22, cGold, "MISSÕES");
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void DrawMenu() {
    float dt = ImGui::GetIO().DeltaTime;
    sTime += dt;
    sAnim = std::clamp(sAnim + (sOpen ? dt * 6.0f : -dt * 8.0f), 0.0f, 1.0f);
    if (sAnim <= 0.0f) {
        return;
    }
    ImVec2 pos, size;
    if (!GameRect(pos, size)) {
        return;
    }
    float a = Ease(sAnim);
    float k = std::min(size.x / kPageW, size.y / kPageH) * (0.92f + 0.06f * a);
    ImVec2 origin(pos.x + (size.x - kPageW * k) / 2, pos.y + (size.y - kPageH * k) / 2);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##QuestMenu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
                     (sOpen ? 0 : ImGuiWindowFlags_NoInputs));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(8, 2, 14, (int)(170 * a)));
    Page pg{ dl, origin, k };

    const auto& quests = Quests((Category)sCategory);
    std::vector<QuestProgress> progress;
    progress.reserve(quests.size());
    for (const Quest& q : quests) {
        progress.push_back(Evaluate(q));
    }

    DrawBackdrop(pg, a);
    DrawBanner(pg, a);
    DrawSidebar(pg, a);
    DrawList(pg, a, progress);
    DrawDetail(pg, a, progress);
    DrawFooter(pg, a);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace

void RequestOpen() {
    if (sOpen) {
        return;
    }
    if (PauseReady()) {
        Open();
        return;
    }
    if (gPlayState != nullptr && SaveLoaded()) {
        sPendingOpen = 90;
        sStartInjected = false;
    }
}

void Close() {
    if (!sOpen) {
        return;
    }
    sOpen = false;
    sPendingOpen = 0;
    Audio_PlaySfx(NA_SE_SY_CANCEL);
}

bool IsOpen() {
    return sOpen;
}

} // namespace QuestMenu

// The window is only a door: showing it (from the menu) opens the quest menu, and it draws the menu every frame.
void QuestMenuWindow::Draw() {
    if (IsVisible()) {
        ToggleVisibility();
        QuestMenu::RequestOpen();
    }
    if (QuestMenu::ShortcutPressed()) {
        if (QuestMenu::IsOpen()) {
            QuestMenu::Close();
        } else {
            QuestMenu::RequestOpen();
        }
    }
    QuestMenu::DrawPauseButton();
    QuestMenu::DrawMenu();
}

void QuestMenuWindow::InitElement() {
}

void QuestMenuWindow::DrawElement() {
}

void QuestMenuWindow::UpdateElement() {
}

static void RegisterQuestMenu() {
    COND_VB_SHOULD(VB_KALEIDO_UPDATE, true, {
        if (QuestMenu::IsOpen()) {
            *should = false;
        }
    });
    COND_HOOK(OnGameStateMainStart, true, QuestMenu::OnGameFrame);
}

static RegisterShipInitFunc sQuestMenuInit(RegisterQuestMenu, {});
