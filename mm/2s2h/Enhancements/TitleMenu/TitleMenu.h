#ifndef TITLE_MENU_H
#define TITLE_MENU_H

// The title menu: replaces the game's start screen (the title scene with "Press Start" and the file select's own
// main screen) with the Definitive Edition's own menu, over a sinister track from the game's own soundtrack.
//
// It runs on top of the game's file select, which keeps doing the real work underneath: after the boot logo the
// game goes straight to the file select, and the menu covers it. The menu never enters the game to show something;
// each item opens a screen of its own, drawn here, with a title, the way here, a back button, pages, help and hints:
//  - Iniciar Jogo asks first, then opens the game's name entry on the first empty file and starts it once named.
//  - Continuar loads the file played last (or the first that exists). Carregar Jogo and Arquivos de Jogo list the
//    files read from the saves (name, hearts, masks, remains, songs, rupees, day and time, cycles): play, start a
//    new game in an empty file, or erase one after a question (the same steps as the game's own Erase).
//  - Configurações: Video, Resolution, Audio, Language, Controls, Accessibility and Definitive Edition tabs, each
//    row a real setting (CVar) applied at once, plus the full settings menu.
//  - Controles: every button and what it does, the button combinations, and the control settings / input editor.
//  - Extras: gallery, concept art, edition info, additional content, the menu music, the original title screen.
//  - Créditos: the Definitive Edition, the original developers and the 2 Ship 2 Harkinian contributors.
//  - Sair asks, then closes the game.
// The game's controller drives it (stick / d-pad, A or Start, B, L / Z and R for pages), so a Bluetooth controller,
// the keyboard's game keys and the touch buttons all work; the arrows, Enter, Backspace and Page Up / Down work
// directly, and the mouse and touch work on every button. Nothing here changes the game once a file is loaded.
// The moon drifts and breathes, its eyes and Tatl glow and motes of light rise (gEnhancements.TitleMenu.Effects).
//
// The art is in assets/custom/overlays/titlemenu (made by make_assets.py from Rafael's pictures). Turned off with
// gEnhancements.TitleMenu.Enabled, the game's own title screen and file select come back.

#include <imgui.h>
#include <ship/window/gui/GuiWindow.h>

namespace TitleMenu {
// The menu's picture as it is drawn now, for the opening's moon (IntroVideo.cpp), which starts close up on the moon
// and draws back to exactly this. Points are on the 1920 x 1080 page; on screen, origin + point * k.
struct Scene {
    ImVec2 origin;
    float k;
    ImTextureID backgroundTex;
    ImTextureID moonTex;
    ImVec2 moon0, moon1, eyes[2];
};
bool MenuScene(Scene& out);
} // namespace TitleMenu

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
