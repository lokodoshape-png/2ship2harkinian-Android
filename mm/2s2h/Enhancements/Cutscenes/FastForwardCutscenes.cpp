#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/BenPort.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"
#include "2s2h/Enhancements/Controls/ButtonPrompts.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
int16_t OTRGetRectDimensionFromRightEdge(float v);
}

// Holding B for half a second during a cutscene runs the game several logic frames per displayed frame,
// so the cutscene plays through quickly with all of its script commands, actor cues and flags intact.
#define FAST_FORWARD_HOLD_FRAMES 10

#define CVAR_NAME "gEnhancements.Cutscenes.HoldBToFastForward"
#define CVAR_HINT "gEnhancements.Cutscenes.FastForwardHint"

// The hint in the corner appears once a cutscene has run this long, so it stays out of quick camera cuts.
#define HINT_DELAY_FRAMES 20
#define HINT_FADE_FRAMES 10

static s32 sHeldFrames = 0;
static s32 sCutsceneFrames = 0;
static bool sFastForwarding = false;

extern "C" s32 FastForwardCutscenes_GetExtraFrames(GameState* gameState) {
    sFastForwarding = false;

    if (!CVarGetInteger(CVAR_NAME, 0) || gPlayState == NULL || &gPlayState->state != gameState) {
        sHeldFrames = 0;
        sCutsceneFrames = 0;
        return 0;
    }

    bool inCutscene = (gPlayState->csCtx.state != CS_STATE_IDLE) || (CutsceneManager_GetCurrentCsId() != CS_ID_NONE);
    if (!inCutscene) {
        sHeldFrames = 0;
        sCutsceneFrames = 0;
        return 0;
    }
    sCutsceneFrames++;

    if (!CHECK_BTN_ALL(gameState->input[0].cur.button, BTN_B)) {
        sHeldFrames = 0;
        return 0;
    }

    if (sHeldFrames < FAST_FORWARD_HOLD_FRAMES) {
        sHeldFrames++;
        return 0;
    }

    sFastForwarding = true;
    return CVarGetInteger("gEnhancements.Cutscenes.FastForwardSpeed", 5) - 1;
}

// "Hold [B] to speed up" in the bottom right corner, over the letterbox.
static void DrawFastForwardHint() {
    if (gPlayState == NULL || !CVarGetInteger(CVAR_HINT, 1) || sCutsceneFrames < HINT_DELAY_FRAMES ||
        IS_PAUSED(&gPlayState->pauseCtx)) {
        return;
    }
    const char* texture = ButtonPrompts::GetCutsceneHint(sFastForwarding);
    if (texture == nullptr) {
        return;
    }
    s32 alpha = std::min(255, (sCutsceneFrames - HINT_DELAY_FRAMES) * 255 / HINT_FADE_FRAMES);

    // 128x16 texture drawn at 96x12.
    const s16 width = 96, height = 12;
    s16 left = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH - 6 - width);
    s16 top = SCREEN_HEIGHT - 6 - height;

    OPEN_DISPS(gPlayState->state.gfxCtx);
    Gfx_SetupDL39_Overlay(gPlayState->state.gfxCtx);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, alpha);
    gDPLoadTextureBlock(OVERLAY_DISP++, texture, G_IM_FMT_IA, G_IM_SIZ_8b, 128, 16, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    gSPWideTextureRectangle(OVERLAY_DISP++, left << 2, top << 2, (left + width) << 2, (top + height) << 2,
                            G_TX_RENDERTILE, 0, 0, (128 << 10) / width, (16 << 10) / height);
    gDPPipeSync(OVERLAY_DISP++);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

static RegisterShipInitFunc sRegisterFastForwardHint(
    []() { COND_HOOK(OnGameStateDrawFinish, CVarGetInteger(CVAR_NAME, 0), DrawFastForwardHint); },
    { CVAR_NAME });
