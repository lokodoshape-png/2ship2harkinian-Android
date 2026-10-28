#ifndef MM3D_BOMBERS_NOTEBOOK_GUIDE_H
#define MM3D_BOMBERS_NOTEBOOK_GUIDE_H

#include <ship/window/gui/GuiWindow.h>

// Guide window for the Bombers' Notebook: for every person it lists the reward, when and where to go,
// and the mask or condition needed. When enabled, it opens by itself while the notebook is open and
// highlights the person under the notebook cursor.
class BombersNotebookGuideWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;

  private:
    bool mAutoShown = false;
};

#endif // MM3D_BOMBERS_NOTEBOOK_GUIDE_H
