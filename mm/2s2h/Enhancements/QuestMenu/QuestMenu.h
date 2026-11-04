#ifndef QUEST_MENU_H
#define QUEST_MENU_H

// The quest menu: a screen inside the pause menu that lists the main quests, side quests, masks, people, places,
// items and records, with each one's state and objectives read from the save.
//
// It opens while the game is paused (L on the pause screen, N on the keyboard, Back / View / Select / - on a
// controller, the QUESTS scroll between the quiver and the bomb bag on the Quest Status page, or the button in the menu, which
// pauses the game first). While it is open the pause menu stands still and the game's controller drives it: L / Z and R
// change the category, the stick or d-pad moves through the list, A reads the details, B goes back and Start closes.
// The mouse and touch work on every part of it.
//
// The parts: QuestData (what is listed), QuestState (where each quest stands), QuestRenderer (drawing) and this
// window (input, layout and animation).

#include <ship/window/gui/GuiWindow.h>

namespace QuestMenu {

// Opens the menu now when the pause menu is ready for it, otherwise pauses the game and opens it then.
void RequestOpen();
void Close();
bool IsOpen();

} // namespace QuestMenu

class QuestMenuWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

#endif // QUEST_MENU_H
