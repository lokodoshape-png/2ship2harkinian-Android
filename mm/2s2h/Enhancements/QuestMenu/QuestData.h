#ifndef QUEST_MENU_DATA_H
#define QUEST_MENU_DATA_H

// What the quest menu lists. Each quest is plain data: texts, an icon, and small checks that read the save. A check
// is only written where the game keeps a real flag or item for it; steps the game does not record have no check
// and count as done only once the whole quest is (see QuestState). New quests are added to the tables in
// QuestData.cpp.

#include <functional>
#include <string>
#include <vector>

namespace QuestMenu {

enum class Category { Main, Side, Masks, People, Places, Items, Records, Count };

struct Objective {
    std::string text;
    std::function<bool()> done; // empty when the game keeps no flag for this step
    const char* icon = nullptr; // item icon for the step, filled from its text (see QuestData.cpp)
};

// A number the game keeps, shown as "n / max" (stray fairies, hearts, skulltulas...).
struct Counter {
    std::function<int()> value;
    int max = 0;
};

struct Quest {
    std::string id;          // unique, stable: "mask.deku", "main.woodfall"...
    std::string name;
    std::string description;
    std::string region;
    std::string when;        // days and hours, empty when it doesn't matter
    std::string reward;
    const char* icon = nullptr;     // texture name: a game item icon or notebook photo (from the player's files)
    const char* iconAsset = nullptr; // or one of our PNGs in overlays/questmenu, when there is no game icon
    std::function<bool()> unlocked;  // empty: always open
    std::string lockedHint;          // what opens it
    std::vector<Objective> objectives;
    Counter counter;                 // optional
    std::function<bool()> complete;  // empty: when every checked objective (and the counter) is done
};

const char* CategoryName(Category category);
const char* CategoryAsset(Category category);
const char* CategorySubtitle(Category category); // heading of the list page
const char* CategoryHint(Category category);     // line on the plaque under the book
const std::vector<Quest>& Quests(Category category);

} // namespace QuestMenu

#endif // QUEST_MENU_DATA_H
