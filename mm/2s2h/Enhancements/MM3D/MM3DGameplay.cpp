#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/Saving/SavingEnhancements.h"
#include "MM3D.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern Input* sPlayerControlInput;
void Interface_DrawAutosaveIcon(PlayState* play, u16 alpha);
}

// Small gameplay changes Majora's Mask 3D made to the N64 game.

#define CVAR_OWL_SAVE_NAME "gEnhancements.MM3D.OwlStatueSaveAndContinue"
#define CVAR_GORON_ROLL_NAME "gEnhancements.MM3D.GoronRollWithoutHolding"
#define CVAR_SCARECROW_NAME "gEnhancements.MM3D.KeepScarecrowSong"

// Owl Statues: saving keeps the game going, and the save is not deleted when it is loaded. This writes the same
// kind of save as the pause menu and autosave options.

static uint32_t sOwlSaveIconTimer = 0;

static void OwlStatueSave() {
    Audio_PlaySfx(NA_SE_SY_PIECE_OF_HEART);
    if (gSaveContext.fileNum == 0xFF || !gSaveContext.flashSaveAvailable) {
        return;
    }

    // Persist this in case the user is 0th daying
    bool currentOwlSaveState = gSaveContext.save.isOwlSave;

    gSaveContext.save.isOwlSave = true;
    SavingEnhancements_PersistSaveEntranceInfo();
    SavingEnhancements_AdvancePlaytime();
    Play_SaveCycleSceneFlags(gPlayState);
    gSaveContext.save.saveInfo.playerData.savedSceneId = gPlayState->sceneId;
    func_8014546C(&gPlayState->sramCtx);
    Sram_SetFlashPagesOwlSave(&gPlayState->sramCtx,
                              gFlashOwlSaveStartPages[gSaveContext.fileNum * FLASH_SAVE_MAIN_MULTIPLIER],
                              gFlashOwlSaveNumPages[gSaveContext.fileNum * FLASH_SAVE_MAIN_MULTIPLIER]);
    Sram_StartWriteToFlashOwlSave(&gPlayState->sramCtx);
    gSaveContext.save.isOwlSave = currentOwlSaveState;
    SavingEnhancements_ClearSaveEntranceInfo();

    sOwlSaveIconTimer = 60;
}

void RegisterOwlStatueSaveAndContinue() {
    COND_VB_SHOULD(VB_OWL_STATUE_SAVE_AND_QUIT, CVarGetInteger(CVAR_OWL_SAVE_NAME, 0), {
        *should = false;
        OwlStatueSave();
    });

    COND_HOOK(OnGameStateDrawFinish, CVarGetInteger(CVAR_OWL_SAVE_NAME, 0), []() {
        if (gPlayState == nullptr || sOwlSaveIconTimer == 0) {
            return;
        }
        u16 alpha = sOwlSaveIconTimer < 20 ? (sOwlSaveIconTimer * 255) / 20 : 255;
        Interface_DrawAutosaveIcon(gPlayState, alpha);
        sOwlSaveIconTimer--;
    });
}

// Goron rolling: A starts the roll and a second press of A stops it, instead of having to hold A the whole time.
// Spikes still come from speed and magic as before.

static bool sRollReleasedA = false;
static bool sRollStopRequested = false;
static u32 sRollLastFrame = 0;

void RegisterGoronRollWithoutHolding() {
    COND_VB_SHOULD(VB_GORON_ROLL_STOP, CVarGetInteger(CVAR_GORON_ROLL_NAME, 0), {
        Player* player = va_arg(args, Player*);

        // A new roll: forget the presses of the previous one.
        if (gPlayState->gameplayFrames - sRollLastFrame > 1) {
            sRollReleasedA = false;
            sRollStopRequested = false;
        }
        sRollLastFrame = gPlayState->gameplayFrames;

        if (!CHECK_BTN_ALL(sPlayerControlInput->cur.button, BTN_A)) {
            sRollReleasedA = true;
        } else if (sRollReleasedA && CHECK_BTN_ALL(sPlayerControlInput->press.button, BTN_A)) {
            sRollStopRequested = true;
        }

        *should = ((player->unk_B86[1] == 0) && sRollStopRequested) ||
                  ((player->av1.actionVar1 == 3) && (player->actor.velocity.y < 0.0f));
        if (*should) {
            sRollReleasedA = false;
            sRollStopRequested = false;
        }
    });

    // Without holding A, the spikes only stop when the roll is being stopped, magic runs out or the roll slows down.
    COND_VB_SHOULD(VB_GORON_ROLL_DISABLE_SPIKE_MODE, CVarGetInteger(CVAR_GORON_ROLL_NAME, 0), {
        Player* player = GET_PLAYER(gPlayState);
        bool magicRanOut = (gSaveContext.save.saveInfo.playerData.magic == 0) &&
                           !CVarGetInteger("gEnhancements.Masks.GoronRollingIgnoresMagic", 0);
        *should = (player->stateFlags3 & PLAYER_STATE3_80000) &&
                  (sRollStopRequested || magicRanOut || ((player->av1.actionVar1 == 4) && (player->unk_B08 < 12.0f)));
    });
}

// The Scarecrow's Song is kept when the Song of Time takes Link back to the first day.

static bool sScarecrowSongWasSet = false;

void RegisterKeepScarecrowSong() {
    COND_HOOK(BeforeEndOfCycleSave, CVarGetInteger(CVAR_SCARECROW_NAME, 0),
              []() { sScarecrowSongWasSet = gSaveContext.save.saveInfo.scarecrowSpawnSongSet; });
    COND_HOOK(AfterEndOfCycleSave, CVarGetInteger(CVAR_SCARECROW_NAME, 0), []() {
        if (sScarecrowSongWasSet) {
            gSaveContext.save.saveInfo.scarecrowSpawnSongSet = true;
        }
    });
}

static RegisterShipInitFunc initOwlSave(RegisterOwlStatueSaveAndContinue, { CVAR_OWL_SAVE_NAME });
static RegisterShipInitFunc initGoronRoll(RegisterGoronRollWithoutHolding, { CVAR_GORON_ROLL_NAME });
static RegisterShipInitFunc initScarecrow(RegisterKeepScarecrowSong, { CVAR_SCARECROW_NAME });
