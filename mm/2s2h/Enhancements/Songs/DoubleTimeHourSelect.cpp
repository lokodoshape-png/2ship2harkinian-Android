#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"
#include "Songs.h"

#include <cstring>

extern "C" {
#include <z64.h>
#include <macros.h>
#include "variables.h"
#include "functions.h"

extern PlayState* gPlayState;
}

// Lets the player pick which hour the Song of Double Time advances to, like in Majora's Mask 3D.
// Only hours up to the next dawn/dusk are offered, so the day/night transition logic is untouched:
// picking the furthest hour is identical to vanilla, any earlier hour stays within the same half day.

#define STICK_THRESHOLD 30
#define REPEAT_DELAY_FRAMES 12
#define REPEAT_RATE_FRAMES 4

static bool sActive = false;
static s32 sStartHour = 0;
static s32 sOffset = 0;
static s32 sMaxOffset = 0;
static s32 sRepeatTimer = 0;
static s32 sHeldDirection = 0;

static s32 GetTargetHour() {
    return (sStartHour + sOffset) % 24;
}

extern "C" void DoubleTimeHourSelect_Init(void) {
    sActive = false;

    if (!CVarGetInteger("gEnhancements.Songs.DoubleTimeHourSelect", 0)) {
        return;
    }

    s32 curHour = (s32)TIME_TO_MINUTES_F(gSaveContext.save.time) / 60;
    s32 boundaryHour = gSaveContext.save.isNight ? 6 : 18;
    s32 hoursUntilBoundary = (boundaryHour - curHour + 24) % 24;

    // Nothing to choose between, or the clock is in an unexpected spot: keep vanilla behavior
    if (hoursUntilBoundary < 2 || hoursUntilBoundary > 12) {
        return;
    }

    sStartHour = curHour;
    sMaxOffset = hoursUntilBoundary;
    sOffset = sMaxOffset;
    sRepeatTimer = 0;
    sHeldDirection = 0;
    sActive = true;
}

extern "C" void DoubleTimeHourSelect_Update(struct Input* input) {
    if (!sActive) {
        return;
    }

    s32 direction = 0;
    if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
        direction = -1;
        sRepeatTimer = 0;
    } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
        direction = 1;
        sRepeatTimer = 0;
    } else {
        s32 held = 0;
        if (input->rel.stick_x < -STICK_THRESHOLD) {
            held = -1;
        } else if (input->rel.stick_x > STICK_THRESHOLD) {
            held = 1;
        }

        if (held == 0) {
            sHeldDirection = 0;
        } else if (held != sHeldDirection) {
            sHeldDirection = held;
            sRepeatTimer = REPEAT_DELAY_FRAMES;
            direction = held;
        } else if (--sRepeatTimer <= 0) {
            sRepeatTimer = REPEAT_RATE_FRAMES;
            direction = held;
        }
    }

    s32 newOffset = CLAMP(sOffset + direction, 1, sMaxOffset);
    if (newOffset != sOffset) {
        sOffset = newOffset;
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    }
}

extern "C" void DoubleTimeHourSelect_Apply(void) {
    if (sActive && (sOffset != sMaxOffset)) {
        gSaveContext.save.time = CLOCK_TIME(GetTargetHour(), 0);
    }
    sActive = false;
}

extern "C" void DoubleTimeHourSelect_Cancel(void) {
    sActive = false;
}

static void DrawTargetHour() {
    if (!sActive || gPlayState == nullptr || gPlayState->msgCtx.ocarinaMode != OCARINA_MODE_PROCESS_DOUBLE_TIME) {
        return;
    }

    s32 hour = GetTargetHour();
    bool use24Hours = CVarGetInteger("gEnhancements.Graphics.24HoursClock", 0);
    char text[24];

    if (use24Hours) {
        snprintf(text, sizeof(text), "%s %02d:00 %s", (sOffset > 1) ? "<" : " ", hour,
                 (sOffset < sMaxOffset) ? ">" : " ");
    } else {
        s32 displayHour = hour % 12;
        if (displayHour == 0) {
            displayHour = 12;
        }
        snprintf(text, sizeof(text), "%s %d:00 %s %s", (sOffset > 1) ? "<" : " ", displayHour,
                 (hour >= 12) ? "pm" : "am", (sOffset < sMaxOffset) ? ">" : " ");
    }

    OPEN_DISPS(gPlayState->state.gfxCtx);
    Gfx_SetupDL39_Overlay(gPlayState->state.gfxCtx);
    OPEN_PRINTER(OVERLAY_DISP);

    GfxPrint_SetColor(&printer, 255, 255, 120, 255);
    GfxPrint_SetPos(&printer, 20 - ((s32)strlen(text) / 2), 6);
    GfxPrint_Printf(&printer, "%s", text);

    CLOSE_PRINTER(printer, OVERLAY_DISP);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

void RegisterDoubleTimeHourSelect() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateDrawFinish>([]() { DrawTargetHour(); });
}
