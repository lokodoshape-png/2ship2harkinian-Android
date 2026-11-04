#ifndef UPDATER_H
#define UPDATER_H

#include <ship/window/gui/GuiWindow.h>

// PC updater (Windows): when the game opens it asks GitHub for the newest PC release. If it is newer than this
// build, a window offers to update; on yes it downloads and unpacks the Windows zip, closes the game, copies the new
// files over the game folder (leaving mm.o2r, saves, mods, settings and the shader cache alone) and opens it again.
// Off with the CVar below. Builds made outside the release CI don't know their number and never update.
#define CVAR_CHECK_FOR_UPDATES "gSettings.CheckForUpdates"

class UpdaterWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

#endif // UPDATER_H
