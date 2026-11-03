#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/Enhancements/Saving/SavingEnhancements.h"
#include "MM3D.h"
#include "2s2h/Rando/Rando.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
extern Input* sPlayerControlInput;
void Interface_DrawAutosaveIcon(PlayState* play, u16 alpha);
#include "overlays/actors/ovl_En_Elfgrp/z_en_elfgrp.h"
#include "overlays/actors/ovl_Bg_Dy_Yoseizo/z_bg_dy_yoseizo.h"
}

// Small gameplay changes Majora's Mask 3D made to the N64 game.

#define CVAR_OWL_SAVE_NAME "gEnhancements.MM3D.OwlStatueSaveAndContinue"
#define CVAR_GORON_ROLL_NAME "gEnhancements.MM3D.GoronRollWithoutHolding"
#define CVAR_SCARECROW_NAME "gEnhancements.MM3D.KeepScarecrowSong"
#define CVAR_FAIRIES_NAME "gEnhancements.MM3D.SwapGreatFairyRewards"
#define CVAR_KOUME_DAMPE_NAME "gEnhancements.MM3D.SwapKoumeDampeRewards"

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

// Great Fairies: as in Majora's Mask 3D, the Woodfall fairy doubles the magic meter and the Snowhead fairy gives the
// Great Spin Attack (the N64 game has them the other way round). Their spoken lines are unchanged.

static void GiveDoubleMagic() {
    if (gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired != true) {
        gSaveContext.save.saveInfo.playerData.isDoubleMagicAcquired = true;
        gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
        gSaveContext.save.saveInfo.playerData.magicLevel = 0;
    }
}

void RegisterSwapGreatFairyRewards() {
    bool enabled = CVarGetInteger(CVAR_FAIRIES_NAME, 0) && !IS_RANDO;

    // The stray fairy group gives the Great Spin Attack (Woodfall) when the fountain's cutscene starts.
    COND_VB_SHOULD(VB_GIVE_ITEM_FROM_STRAY_FAIRY_MANAGER, enabled, {
        EnElfgrp* elfgrp = va_arg(args, EnElfgrp*);
        if (elfgrp->type == ENELFGRP_TYPE_POWER) {
            *should = false;
            GiveDoubleMagic();
        } else if (elfgrp->type == ENELFGRP_TYPE_WISDOM) {
            *should = false;
            SET_WEEKEVENTREG(WEEKEVENTREG_RECEIVED_GREAT_SPIN_ATTACK);
        }
    });

    // The Great Fairy herself gives the double magic meter (Snowhead).
    COND_VB_SHOULD(VB_GIVE_ITEM_FROM_GREAT_FAIRY, enabled, {
        BgDyYoseizo* fairy = va_arg(args, BgDyYoseizo*);
        if (GREAT_FAIRY_GET_TYPE(&fairy->actor) == GREAT_FAIRY_TYPE_WISDOM) {
            *should = false;
        }
    });
}

// Koume's boat archery gives an empty bottle and the chest Dampé's Big Poe leaves gives a Piece of Heart, the other
// way round from the N64 game. Chests offer their item as a negative id.

void RegisterSwapKoumeDampeRewards() {
    COND_VB_SHOULD(VB_GIVE_ITEM_FROM_OFFER, CVarGetInteger(CVAR_KOUME_DAMPE_NAME, 0) && !IS_RANDO, {
        GetItemId* item = va_arg(args, GetItemId*);
        Actor* actor = va_arg(args, Actor*);

        if (actor->id == ACTOR_EN_DNH && *item == GI_HEART_PIECE) {
            *item = GI_BOTTLE;
        } else if (actor->id == ACTOR_EN_BOX && gPlayState->sceneId == SCENE_DANPEI2TEST && *item == -GI_BOTTLE) {
            *item = (GetItemId)-GI_HEART_PIECE;
        }
    });
}

static RegisterShipInitFunc initOwlSave(RegisterOwlStatueSaveAndContinue, { CVAR_OWL_SAVE_NAME });
static RegisterShipInitFunc initGoronRoll(RegisterGoronRollWithoutHolding, { CVAR_GORON_ROLL_NAME });
static RegisterShipInitFunc initScarecrow(RegisterKeepScarecrowSong, { CVAR_SCARECROW_NAME });
static RegisterShipInitFunc initFairies(RegisterSwapGreatFairyRewards, { CVAR_FAIRIES_NAME, "IS_RANDO" });
static RegisterShipInitFunc initKoumeDampe(RegisterSwapKoumeDampeRewards, { CVAR_KOUME_DAMPE_NAME, "IS_RANDO" });
