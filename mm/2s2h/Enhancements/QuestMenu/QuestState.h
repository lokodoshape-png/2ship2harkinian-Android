#ifndef QUEST_MENU_STATE_H
#define QUEST_MENU_STATE_H

// Where a quest stands in the loaded save. Everything here is read from the game's own flags and items through the
// checks in QuestData; nothing is stored by the menu.

#include "QuestData.h"

namespace QuestMenu {

enum class Status { Locked, Available, InProgress, Done };

struct QuestProgress {
    Status status = Status::Locked;
    int done = 0;   // checked objectives done (plus the counter, counted as one)
    int total = 0;  // checked objectives (plus the counter)
    std::vector<bool> objectiveDone; // one per objective; unchecked ones follow the whole quest
    int counterValue = 0;
};

// True while a save is loaded and being played, so the checks can read gSaveContext.
bool SaveLoaded();
QuestProgress Evaluate(const Quest& quest);
const char* StatusName(Status status);
const char* StatusAsset(Status status);

} // namespace QuestMenu

#endif // QUEST_MENU_STATE_H
