#include <libultraship/bridge.h>
#include "Cutscenes.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Holding B for half a second during a cutscene runs the game several logic frames per displayed frame,
// so the cutscene plays through quickly with all of its script commands, actor cues and flags intact.
#define FAST_FORWARD_HOLD_FRAMES 10

extern "C" s32 FastForwardCutscenes_GetExtraFrames(GameState* gameState) {
    static s32 sHeldFrames = 0;

    if (!CVarGetInteger("gEnhancements.Cutscenes.HoldBToFastForward", 0) || gPlayState == NULL ||
        &gPlayState->state != gameState) {
        sHeldFrames = 0;
        return 0;
    }

    bool inCutscene = (gPlayState->csCtx.state != CS_STATE_IDLE) || (CutsceneManager_GetCurrentCsId() != CS_ID_NONE);
    if (!inCutscene || !CHECK_BTN_ALL(gameState->input[0].cur.button, BTN_B)) {
        sHeldFrames = 0;
        return 0;
    }

    if (sHeldFrames < FAST_FORWARD_HOLD_FRAMES) {
        sHeldFrames++;
        return 0;
    }

    return CVarGetInteger("gEnhancements.Cutscenes.FastForwardSpeed", 5) - 1;
}
