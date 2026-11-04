#ifndef TITLE_MENU_H
#define TITLE_MENU_H

// The title menu: replaces the game's start screen (the title scene with "Press Start" and the file select's own
// main screen) with one painted screen over a sinister track from the game's own soundtrack.
//
// It runs on top of the game's file select, which keeps doing the real work underneath: after the boot logo the
// game goes straight to the file select, and the menu covers it.
//  - Continuar loads the file that was played last (or the first one that exists).
//  - Novo Jogo asks first, then opens the game's name entry on the first empty file; other files are untouched.
//  - Carregar Jogo shows the game's file select (files with hearts, masks and day); B there comes back here.
//  - Bombers Book, Missões and Inventário load that same file and then open, in the game, the Bombers' Notebook
//    (from the quest page of the pause menu), the quest menu or the pause menu's item page.
//  - Opções opens the settings menu (graphics, resolution, audio, controls...).
//  - Extras: the menu's music, the game's own sound and Z-targeting options, the original title screen, credits and
//    version.
// The game's controller drives it (stick / d-pad, A or Start, B), so a Bluetooth controller, the keyboard and the
// touch buttons all work; the arrows, Enter and Backspace work directly, and the mouse and touch work on every button.
// The moon drifts and breathes, its eyes and Tatl glow and motes of light rise (gEnhancements.TitleMenu.Effects).
//
// The art is in assets/custom/overlays/titlemenu (made by make_assets.py from Rafael's picture). Turned off with
// gEnhancements.TitleMenu.Enabled, the game's own title screen and file select come back.

#include <ship/window/gui/GuiWindow.h>

class TitleMenuWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override;
};

#endif // TITLE_MENU_H
