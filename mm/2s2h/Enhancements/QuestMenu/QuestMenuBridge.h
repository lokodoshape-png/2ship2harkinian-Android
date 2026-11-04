#ifndef QUEST_MENU_BRIDGE_H
#define QUEST_MENU_BRIDGE_H

// What the game's C code needs from the quest menu: the pause menu's quest page shows a MISSÕES entry in the unused
// slot under the Bombers' Notebook, and A on it opens the quest menu.

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool QuestMenu_PauseEntryShown(void);
void QuestMenu_OpenFromPause(void);

#ifdef __cplusplus
}
#endif

#endif // QUEST_MENU_BRIDGE_H
