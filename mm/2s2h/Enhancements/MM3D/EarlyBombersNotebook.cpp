#include <libultraship/bridge/consolevariablebridge.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "MM3D.h"

extern "C" {
#include <z64.h>
#include "variables.h"
}

// Like in Majora's Mask 3D, the Bombers' Notebook is available from the first visit to Clock Town instead of
// requiring the Bombers' hide-and-seek game.

static bool IsClockTownScene(s16 sceneId) {
    switch (sceneId) {
        case SCENE_CLOCKTOWER:
        case SCENE_TOWN:
        case SCENE_ICHIBA:
        case SCENE_BACKTOWN:
        case SCENE_ALLEY:
            return true;
        default:
            return false;
    }
}

#define CVAR_NAME "gEnhancements.MM3D.EarlyBombersNotebook"

void RegisterEarlyBombersNotebook() {
    COND_HOOK(OnSceneInit, CVarGetInteger(CVAR_NAME, 0), [](s16 sceneId, s8 spawnNum) {
        if (IsClockTownScene(sceneId) && !CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK)) {
            SET_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK);
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterEarlyBombersNotebook, { CVAR_NAME });
