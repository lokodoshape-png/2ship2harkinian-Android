#include <libultraship/libultraship.h>
#include "2s2h/GameInteractor/GameInteractor.h"
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

void RegisterEarlyBombersNotebook() {
    static HOOK_ID onSceneInitHookId = 0;
    GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnSceneInit>(onSceneInitHookId);
    onSceneInitHookId = 0;

    if (!CVarGetInteger("gEnhancements.MM3D.EarlyBombersNotebook", 0)) {
        return;
    }

    onSceneInitHookId =
        GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>([](s8 sceneId, s8 spawnNum) {
            if (IsClockTownScene(sceneId) && !CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK)) {
                SET_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK);
            }
        });
}
