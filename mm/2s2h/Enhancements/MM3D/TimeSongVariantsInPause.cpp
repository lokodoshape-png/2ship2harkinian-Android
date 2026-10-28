#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/FrameInterpolation/FrameInterpolation.h"
#include "MM3D.h"

#include <cstring>

extern "C" {
#include <z64.h>
#include <macros.h>
#include "variables.h"
#include "functions.h"
#include "overlays/kaleido_scope/ovl_kaleido_scope/z_kaleido_scope.h"

extern PlayState* gPlayState;
}

// Like the Majora's Mask 3D song list, the Song of Time slot on the Quest Status page can also show the
// Inverted Song of Time, the Song of Double Time and the Scarecrow's Song. L or C-Down cycles between them while
// the cursor is on the Song of Time, and A plays back the selected one as usual.

typedef enum {
    TIME_VARIANT_TIME,
    TIME_VARIANT_INVERTED,
    TIME_VARIANT_DOUBLE,
    TIME_VARIANT_SCARECROW,
    TIME_VARIANT_MAX,
} TimeSongVariant;

static s32 sVariant = TIME_VARIANT_TIME;

static const u8 sVariantSongs[TIME_VARIANT_MAX] = {
    OCARINA_SONG_TIME,
    OCARINA_SONG_INVERTED_TIME,
    OCARINA_SONG_DOUBLE_TIME,
    OCARINA_SONG_SCARECROW_SPAWN,
};

static const char* sVariantNames[TIME_VARIANT_MAX] = {
    "Song of Time",
    "Inverted Song of Time",
    "Song of Double Time",
    "Scarecrow's Song",
};

static bool IsEnabled() {
    return CVarGetInteger("gEnhancements.MM3D.TimeSongVariantsInPause", 1);
}

static bool IsVariantAvailable(s32 variant) {
    if (variant == TIME_VARIANT_SCARECROW) {
        return gSaveContext.save.saveInfo.scarecrowSpawnSongSet &&
               (gOcarinaSongButtons[OCARINA_SONG_SCARECROW_SPAWN].numButtons == 8);
    }
    return true;
}

static bool IsCursorOnSongOfTime(PauseContext* pauseCtx) {
    return (pauseCtx->pageIndex == PAUSE_QUEST) && (pauseCtx->cursorSpecialPos == 0) &&
           (pauseCtx->cursorPoint[PAUSE_QUEST] == QUEST_SONG_TIME) && CHECK_QUEST_ITEM(QUEST_SONG_TIME);
}

extern "C" int TimeSongVariantsInPause_GetSongIndex(int defaultSongIndex, int cursor) {
    if (!IsEnabled() || (cursor != QUEST_SONG_TIME) || !IsVariantAvailable(sVariant)) {
        return defaultSongIndex;
    }
    return sVariantSongs[sVariant];
}

static void OnKaleidoUpdate(PauseContext* pauseCtx) {
    if (!IsEnabled() || !IsCursorOnSongOfTime(pauseCtx) ||
        (pauseCtx->mainState != PAUSE_MAIN_STATE_IDLE_CURSOR_ON_SONG) || (gPlayState->msgCtx.msgLength != 0)) {
        return;
    }

    Input* input = CONTROLLER1(&gPlayState->state);
    if (!CHECK_BTN_ANY(input->press.button, BTN_L | BTN_CDOWN)) {
        return;
    }

    do {
        sVariant = (sVariant + 1) % TIME_VARIANT_MAX;
    } while (!IsVariantAvailable(sVariant));

    pauseCtx->ocarinaSongIndex = sVariantSongs[sVariant];
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

static void AfterKaleidoDrawPage(PauseContext* pauseCtx, u16 pauseIndex) {
    if (!IsEnabled() || (pauseIndex != PAUSE_QUEST) || !IsCursorOnSongOfTime(pauseCtx) ||
        (pauseCtx->state != PAUSE_STATE_MAIN)) {
        return;
    }

    char text[64];
    snprintf(text, sizeof(text), "< %s >", sVariantNames[sVariant]);
    const char* hint = "L ou C-baixo: trocar a musica";

    OPEN_DISPS(gPlayState->state.gfxCtx);
    Gfx_SetupDL39_Overlay(gPlayState->state.gfxCtx);
    OPEN_PRINTER(OVERLAY_DISP);

    GfxPrint_SetColor(&printer, 255, 255, 120, 255);
    GfxPrint_SetPos(&printer, 20 - ((s32)strlen(text) / 2), 4);
    GfxPrint_Printf(&printer, "%s", text);
    GfxPrint_SetColor(&printer, 255, 255, 255, 255);
    GfxPrint_SetPos(&printer, 20 - ((s32)strlen(hint) / 2), 5);
    GfxPrint_Printf(&printer, "%s", hint);

    CLOSE_PRINTER(printer, OVERLAY_DISP);
    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

void RegisterTimeSongVariantsInPause() {
    COND_HOOK(OnKaleidoUpdate, IsEnabled(), OnKaleidoUpdate);
    COND_HOOK(AfterKaleidoDrawPage, IsEnabled(), AfterKaleidoDrawPage);
}

static RegisterShipInitFunc initFunc(RegisterTimeSongVariantsInPause, { "gEnhancements.MM3D.TimeSongVariantsInPause" });
