#ifndef MM3D_BOMBERS_NOTEBOOK_GUIDE_H
#define MM3D_BOMBERS_NOTEBOOK_GUIDE_H

#include <ship/window/gui/GuiWindow.h>

// Bombers' Notebook guide: for every person, what to do on each day of the cycle, when and where. A person's
// tips stay hidden until Link has met them, as in the notebook. While the game's notebook is open, the selected
// person's steps are drawn on the notebook itself as sticky notes grouped by day (yellow to do, green once
// recorded). The full list also opens as a window from the menu.
class BombersNotebookGuideWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

#endif // MM3D_BOMBERS_NOTEBOOK_GUIDE_H
