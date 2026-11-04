#include "TitleMenu.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <imgui.h>
#include <imgui_internal.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>
#include <ship/window/gui/resource/GuiTexture.h>
#include <fast/Fast3dWindow.h>
#include <fast/interpreter.h>
#include <fast/backends/gfx_rendering_api.h>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <ship/port/mobile/MobileImpl.h>
#endif

#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "2s2h/BenGui/Notification.h"
#include "2s2h/Enhancements/Controls/ButtonPrompts.h"
#include "2s2h/Enhancements/QuestMenu/QuestMenu.h"
#include "2s2h/Enhancements/QuestMenu/QuestRenderer.h"

extern "C" {
#include <z64.h>
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "build.h"
#include "overlays/gamestates/ovl_file_choose/z_file_select.h"
#include "overlays/gamestates/ovl_opening/z_opening.h"
extern FileSelectState* gFileSelectState;
extern PlayState* gPlayState;
}

#define CVAR_ENABLED "gEnhancements.TitleMenu.Enabled"
#define CVAR_MUSIC "gEnhancements.TitleMenu.Music"
#define CVAR_LAST_SLOT "gEnhancements.TitleMenu.LastSlot"
#define CVAR_EFFECTS "gEnhancements.TitleMenu.Effects"

using QuestUi::Col;
using QuestUi::Page;

namespace TitleMenu {
namespace {

// ----- Layout of the 1920x1080 page (the picture's own size after make_assets.py).

constexpr float kW = 1920.0f, kH = 1080.0f;
constexpr float kRow0 = 478, kRowStep = 66;
constexpr float kPlateX0 = 111, kPlateW = 664, kPlateH = 66, kGlow = 26;
constexpr float kPlateScaleY = 0.78f; // highlight.png is drawn for 86 px rows
constexpr float kIconX = 199, kIconSize = 42, kLabelX = 258, kLabelScale = 0.86f;
// The moon layer (moon.png): its left edge on the page and the moon's circle.
constexpr float kMoonX0 = 1022, kMoonCX = 1492, kMoonCY = -43;
const ImVec2 kMoonEyes[] = { ImVec2(1296, 163), ImVec2(1662, 18) };
const ImVec2 kFairy(1170, 445);

float RowY(int i) {
    return kRow0 + kRowStep * i;
}

// ----- Music, from the game's own soundtrack.

struct Track {
    const char* label; // PNG with the name
    u16 seqId;
};
const Track kTracks[] = {
    { "music_majora", NA_BGM_MAJORAS_THEME },    { "music_final_hours", NA_BGM_FINAL_HOURS },
    { "music_clock_tower", NA_BGM_CLOCK_TOWER }, { "music_ikana_castle", NA_BGM_IKANA_CASTLE },
    { "music_file_select", NA_BGM_FILE_SELECT },
};
constexpr int kTrackCount = (int)(sizeof(kTracks) / sizeof(kTracks[0]));

enum MainItem {
    ITEM_CONTINUE,
    ITEM_NEW,
    ITEM_LOAD,
    ITEM_BOMBERS,
    ITEM_QUESTS,
    ITEM_INVENTORY,
    ITEM_OPTIONS,
    ITEM_EXTRAS,
    ITEM_COUNT
};
enum ExtrasItem { EXTRA_MUSIC, EXTRA_SOUND, EXTRA_OPENING, EXTRA_CREDITS, EXTRA_VERSION, EXTRA_BACK, EXTRA_COUNT };
enum class Panel { Main, Extras };
enum class Modal { None, ConfirmNew, Credits, Version };
enum class Pending { None, Continue, NewGame, Load, GameOptions };
// What to open once a save chosen from the menu is playing.
enum class After { None, BombersNotebook, Quests, Inventory };

// ----- State. The game thread (the input hook) and the GUI draw run one after the other on the same thread.

FileSelectState* sFileSelect = nullptr; // the file select the menu belongs to
bool sActive = false;                   // drawn and driving input
bool sLeaving = false;                  // a save is loading: fading out with the file select
bool sAllowOriginalTitle = false;       // Extras > original title screen: let TitleSetup run once
Panel sPanel = Panel::Main;
Modal sModal = Modal::None;
bool sConfirmYes = true;
int sSelected = 0;
int sExtrasSelected = 0;
float sCursorY = -1.0f;
float sFade = 0.0f;   // 0 hidden .. 1 shown
float sPanelT = 1.0f; // 0 .. 1 after a panel change
float sModalT = 0.0f; // 0 .. 1 while a window is open
float sTime = 0.0f;
Pending sPending = Pending::None;
int sPendingSlot = 0;
After sAfter = After::None;
int sAfterFrames = 0;
int sInjectA = 0; // frames left in which to press A for the file select
int sRepeatY = 0, sRepeatYTimer = 0, sRepeatX = 0, sRepeatXTimer = 0;
float sKeyboardNavTime = 0.0f; // seconds left in which the keyboard, not the controller, drives the menu
std::string sToast;
float sToastTime = 0.0f;
// The footer shows touch hints after a tap, and the controller's (or keyboard's) buttons after a press. On Android
// the menu starts in touch mode; the first press on a controller switches it.
#ifdef __ANDROID__
bool sTouchMode = true;
#else
bool sTouchMode = false;
#endif
bool sPointerInputs = false; // taps and clicks reach the menu this frame
bool sTapTaken = false;      // one tap triggers one button

bool Enabled() {
    return CVarGetInteger(CVAR_ENABLED, 1) != 0;
}

bool Slot3Enabled() {
    return CVarGetInteger("gEnhancements.Saving.FileSlot3", true) != 0;
}

int SlotCount() {
    return Slot3Enabled() ? 3 : 2;
}

bool SlotOccupied(FileSelectState* fs, int slot) {
    if (!gSaveContext.flashSaveAvailable) {
        SramContext* sramCtx = &fs->sramCtx;
        return NO_FLASH_SLOT_OCCUPIED(sramCtx, slot);
    }
    return SLOT_OCCUPIED(fs, slot);
}

// The file played last, or the first one that exists; -1 when there is none.
int ContinueSlot(FileSelectState* fs) {
    if (fs == nullptr) {
        return -1;
    }
    int last = CVarGetInteger(CVAR_LAST_SLOT, -1);
    if (last >= 0 && last < SlotCount() && SlotOccupied(fs, last)) {
        return last;
    }
    for (int i = 0; i < SlotCount(); i++) {
        if (SlotOccupied(fs, i)) {
            return i;
        }
    }
    return -1;
}

int EmptySlot(FileSelectState* fs) {
    for (int i = 0; fs != nullptr && i < SlotCount(); i++) {
        if (!SlotOccupied(fs, i)) {
            return i;
        }
    }
    return -1;
}

// The file select is on its main screen and waiting for the player.
bool FileSelectReady(FileSelectState* fs) {
    return fs != nullptr && fs->menuMode == FS_MENU_MODE_CONFIG && fs->configMode == CM_MAIN_MENU;
}

int RowCount() {
    return sPanel == Panel::Main ? (int)ITEM_COUNT : (int)EXTRA_COUNT;
}

int& Selection() {
    return sPanel == Panel::Main ? sSelected : sExtrasSelected;
}

void Toast(const char* text) {
    sToast = text;
    sToastTime = 3.5f;
}

void PlayMenuMusic() {
    int index = std::clamp(CVarGetInteger(CVAR_MUSIC, 0), 0, kTrackCount - 1);
    SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, kTracks[index].seqId);
}

void ChangeMusic(int delta) {
    int index = (std::clamp(CVarGetInteger(CVAR_MUSIC, 0), 0, kTrackCount - 1) + delta + kTrackCount) % kTrackCount;
    CVarSetInteger(CVAR_MUSIC, index);
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    Audio_PlaySfx(NA_SE_SY_CURSOR);
    PlayMenuMusic();
}

bool SettingsMenuOpen() {
    auto menu = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetMenu();
    return menu != nullptr && menu->IsVisible();
}

void OpenSettingsMenu() {
    auto menu = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetMenu();
    if (menu != nullptr && !menu->IsVisible()) {
        menu->ToggleVisibility();
    }
}

void SetPanel(Panel panel) {
    sPanel = panel;
    sCursorY = -1.0f;
    sPanelT = 0.0f;
}

void Show() {
    sActive = true;
    sLeaving = false;
    sModal = Modal::None;
    sPending = Pending::None;
    SetPanel(Panel::Main);
    sSelected = ContinueSlot(sFileSelect) >= 0 ? ITEM_CONTINUE : ITEM_NEW;
}

void OpenModal(Modal modal) {
    sModal = modal;
    sModalT = 0.0f;
    sConfirmYes = true;
}

// ----- Actions.

// Loads the file played last; `after` opens one of the game's screens once it is playing.
void StartContinue(After after) {
    int slot = ContinueSlot(sFileSelect);
    if (slot < 0) {
        Audio_PlaySfx(NA_SE_SY_ERROR);
        Toast("Nenhum jogo salvo ainda. Escolha Novo Jogo.");
        return;
    }
    Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
    sPending = Pending::Continue;
    sPendingSlot = slot;
    sAfter = after;
    sAfterFrames = 0;
}

void Activate(int item) {
    FileSelectState* fs = sFileSelect;
    switch (item) {
        case ITEM_CONTINUE:
            StartContinue(After::None);
            break;
        case ITEM_NEW:
            if (EmptySlot(fs) < 0) {
                Audio_PlaySfx(NA_SE_SY_ERROR);
                Toast("Todos os arquivos estão em uso. Apague um em Carregar Jogo.");
                return;
            }
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            OpenModal(Modal::ConfirmNew);
            break;
        case ITEM_LOAD:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            sPending = Pending::Load;
            sPendingSlot = std::max(ContinueSlot(fs), 0);
            break;
        case ITEM_BOMBERS:
            StartContinue(After::BombersNotebook);
            break;
        case ITEM_QUESTS:
            StartContinue(After::Quests);
            break;
        case ITEM_INVENTORY:
            StartContinue(After::Inventory);
            break;
        case ITEM_OPTIONS:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            OpenSettingsMenu();
            break;
        case ITEM_EXTRAS:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            SetPanel(Panel::Extras);
            sExtrasSelected = 0;
            break;
    }
}

void ActivateExtra(int item) {
    switch (item) {
        case EXTRA_MUSIC:
            ChangeMusic(1);
            break;
        case EXTRA_SOUND:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            sPending = Pending::GameOptions;
            break;
        case EXTRA_OPENING:
            if (sFileSelect != nullptr) {
                Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                sAllowOriginalTitle = true;
                sActive = false;
                gSaveContext.gameMode = GAMEMODE_TITLE_SCREEN;
                STOP_GAMESTATE(&sFileSelect->state);
                SET_NEXT_GAMESTATE(&sFileSelect->state, TitleSetup_Init, sizeof(TitleSetupState));
            }
            break;
        case EXTRA_CREDITS:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            OpenModal(Modal::Credits);
            break;
        case EXTRA_VERSION:
            Audio_PlaySfx(NA_SE_SY_DECIDE);
            OpenModal(Modal::Version);
            break;
        case EXTRA_BACK:
            Audio_PlaySfx(NA_SE_SY_CANCEL);
            SetPanel(Panel::Main);
            break;
    }
}

void CloseModal() {
    sModal = Modal::None;
    Audio_PlaySfx(NA_SE_SY_CANCEL);
}

void ConfirmNewGame(bool yes) {
    sModal = Modal::None;
    int slot = EmptySlot(sFileSelect);
    if (!yes || slot < 0) {
        Audio_PlaySfx(NA_SE_SY_CANCEL);
        return;
    }
    Audio_PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
    sPending = Pending::NewGame;
    sPendingSlot = slot;
}

void Back() {
    if (sModal != Modal::None) {
        CloseModal();
    } else if (sPanel == Panel::Extras) {
        ActivateExtra(EXTRA_BACK);
    }
}

void Confirm() {
    if (sModal == Modal::ConfirmNew) {
        ConfirmNewGame(sConfirmYes);
    } else if (sModal != Modal::None) {
        CloseModal();
    } else if (sPanel == Panel::Main) {
        Activate(sSelected);
    } else {
        ActivateExtra(sExtrasSelected);
    }
}

void MoveVertical(int delta) {
    if (sModal == Modal::ConfirmNew) {
        sConfirmYes = !sConfirmYes;
        Audio_PlaySfx(NA_SE_SY_CURSOR);
        return;
    }
    if (sModal != Modal::None) {
        return;
    }
    int& sel = Selection();
    sel = (sel + delta + RowCount()) % RowCount();
    Audio_PlaySfx(NA_SE_SY_CURSOR);
}

void MoveHorizontal(int delta) {
    if (sModal == Modal::ConfirmNew) {
        sConfirmYes = !sConfirmYes;
        Audio_PlaySfx(NA_SE_SY_CURSOR);
    } else if (sModal == Modal::None && sPanel == Panel::Extras && sExtrasSelected == EXTRA_MUSIC) {
        ChangeMusic(delta);
    }
}

// Runs a chosen action once the file select is on its main screen (it is still fading in for a moment after boot).
void RunPending(FileSelectState* fs) {
    if (sPending == Pending::None || !FileSelectReady(fs)) {
        return;
    }
    Pending pending = sPending;
    sPending = Pending::None;
    switch (pending) {
        case Pending::Continue:
            // The same steps as confirming a file on the game's own screen, without the screens in between.
            fs->buttonIndex = fs->selectedFileIndex = sPendingSlot;
            fs->menuMode = FS_MENU_MODE_SELECT;
            fs->selectMode = SM_FADE_OUT;
            fs->confirmButtonIndex = FS_BTN_CONFIRM_YES;
            Audio_MuteAllSeqExceptSystemAndOcarina(15);
            sLeaving = true;
            break;
        case Pending::NewGame:
            // Press A on the empty file, so the game opens its own name entry and makes the new file as usual.
            fs->buttonIndex = sPendingSlot;
            fs->warningLabel = FS_WARNING_NONE;
            sActive = false;
            sInjectA = 30;
            break;
        case Pending::Load:
            fs->buttonIndex = sPendingSlot;
            sActive = false;
            break;
        case Pending::GameOptions:
            fs->buttonIndex = FS_BTN_MAIN_OPTIONS;
            fs->warningLabel = FS_WARNING_NONE;
            sActive = false;
            sInjectA = 30;
            break;
        default:
            break;
    }
}

void ClearInput(Input* input) {
    input->press.button = 0;
    input->rel.button = 0;
    input->rel.stick_x = input->rel.stick_y = 0;
    input->press.stick_x = input->press.stick_y = 0;
    input->cur.button = 0;
    input->cur.stick_x = input->cur.stick_y = 0;
}

// A held direction moves once, then repeats after a pause.
void Repeat(int dir, int& last, int& timer, void (*move)(int)) {
    if (dir == 0) {
        last = 0;
    } else if (dir != last) {
        last = dir;
        timer = 10;
        move(dir);
    } else if (--timer <= 0) {
        timer = 4;
        move(dir);
    }
}

// The game's controller: the stick, the d-pad, A / Start and B (the keyboard's game keys and the touch buttons
// arrive here too). While the arrows, Enter or Backspace are in use the keyboard is read on the GUI side instead
// (KeyboardNav), so a key that is also mapped to the controller does not count twice.
void HandlePad(Input* input) {
    if (sKeyboardNavTime > 0.0f) {
        return;
    }
    u16 press = input->press.button, held = input->cur.button;
    s8 stickX = input->cur.stick_x, stickY = input->cur.stick_y;
    if (press != 0 || std::abs(stickX) > 30 || std::abs(stickY) > 30) {
        sTouchMode = false;
    }

    if (CHECK_BTN_ANY(press, BTN_A | BTN_START)) {
        Confirm();
        return;
    }
    if (CHECK_BTN_ANY(press, BTN_B)) {
        Back();
        return;
    }
    int dy = 0, dx = 0;
    if (stickY > 30 || CHECK_BTN_ANY(held, BTN_DUP)) {
        dy = -1;
    } else if (stickY < -30 || CHECK_BTN_ANY(held, BTN_DDOWN)) {
        dy = 1;
    }
    if (stickX < -30 || CHECK_BTN_ANY(held, BTN_DLEFT)) {
        dx = -1;
    } else if (stickX > 30 || CHECK_BTN_ANY(held, BTN_DRIGHT)) {
        dx = 1;
    }
    Repeat(dy, sRepeatY, sRepeatYTimer, MoveVertical);
    Repeat(dx, sRepeatX, sRepeatXTimer, MoveHorizontal);
}

// The arrows, Enter and Backspace, read once per GUI frame.
void KeyboardNav(float dt) {
    sKeyboardNavTime = std::max(0.0f, sKeyboardNavTime - dt);
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
    const ImGuiKey keys[] = { ImGuiKey_UpArrow, ImGuiKey_DownArrow,   ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
                              ImGuiKey_Enter,   ImGuiKey_KeypadEnter, ImGuiKey_Backspace };
    bool any = false;
    for (ImGuiKey key : keys) {
        any = any || ImGui::IsKeyDown(key);
    }
    if (!any) {
        return;
    }
    sKeyboardNavTime = 0.3f;
    sTouchMode = false;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        MoveVertical(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        MoveVertical(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        MoveHorizontal(-1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        MoveHorizontal(1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        Confirm();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        Back();
    }
}

// ----- After loading: open the Bombers' Notebook, the quest menu or the inventory of the save that was loaded.

bool CanPause(PlayState* play) {
    return play->transitionTrigger == TRANS_TRIGGER_OFF && play->transitionMode == TRANS_MODE_OFF &&
           !Play_InCsMode(play) && play->msgCtx.msgMode == MSGMODE_NONE && GET_PLAYER(play) != nullptr &&
           play->pauseCtx.state == PAUSE_STATE_OFF;
}

void Notify(const char* text) {
    Notification::Emit({ .message = text, .remainingTime = 6.0f });
}

void RunAfterLoad() {
    PlayState* play = gPlayState;
    if (play == nullptr || gGameState != &play->state) {
        return; // still loading (or the day screen)
    }
    // Give up if the game never becomes pausable (a cutscene that runs on, for example).
    if (++sAfterFrames > 20 * 30) {
        sAfter = After::None;
        return;
    }
    PauseContext* pauseCtx = &play->pauseCtx;
    if (sAfterFrames < 20) {
        return;
    }
    if (sAfter == After::Quests) {
        if (CanPause(play)) {
            QuestMenu::RequestOpen(); // pauses the game itself
            sAfter = After::None;
        }
        return;
    }
    if (pauseCtx->state == PAUSE_STATE_OFF) {
        if (CanPause(play)) {
            // Pause the way the player does, on the page the pause menu should open at.
            pauseCtx->pageIndex = sAfter == After::Inventory ? PAUSE_ITEM : PAUSE_QUEST;
            CONTROLLER1(&play->state)->press.button |= BTN_START;
        }
        return;
    }
    if (sAfter == After::Inventory) {
        sAfter = After::None;
        return;
    }
    // The Bombers' Notebook opens from the quest page, as with A on the notebook there.
    if (pauseCtx->state == PAUSE_STATE_MAIN && pauseCtx->mainState == PAUSE_MAIN_STATE_IDLE &&
        pauseCtx->pageIndex == PAUSE_QUEST) {
        if (CHECK_QUEST_ITEM(QUEST_BOMBERS_NOTEBOOK)) {
            pauseCtx->cursorPoint[PAUSE_QUEST] = QUEST_BOMBERS_NOTEBOOK;
            pauseCtx->bombersNotebookOpen = true;
            pauseCtx->mainState = PAUSE_MAIN_STATE_BOMBERS_NOTEBOOK_OPEN;
            Audio_PlaySfx(NA_SE_SY_DECIDE);
        } else {
            Notify("Este save ainda não tem o Caderno dos Bombers.");
        }
        sAfter = After::None;
    }
}

void OnGameFrame() {
    if (sAfter != After::None && !sLeaving) {
        RunAfterLoad();
    }

    FileSelectState* fs = gFileSelectState;
    if (fs == nullptr || gGameState != &fs->state) {
        if (sLeaving) {
            sFade = 0.0f; // the game is loading behind a black screen; do not show the menu again on the way out
        }
        sFileSelect = nullptr;
        sActive = false;
        sLeaving = false;
        sPending = Pending::None;
        sInjectA = 0;
        return;
    }
    if (fs != sFileSelect) {
        // A new file select: after boot, after B on the original title screen, after "save and quit"...
        sFileSelect = fs;
        sInjectA = 0;
        sAfter = After::None;
        if (!Enabled()) {
            sActive = false;
            return;
        }
        Show();
        sFade = 0.0f;
        PlayMenuMusic();
    }

    Input* input = CONTROLLER1(&fs->state);
    if (sInjectA > 0) {
        if (FileSelectReady(fs)) {
            input->press.button |= BTN_A;
            sInjectA = 0;
        } else {
            sInjectA--;
        }
    }
    if (!sActive) {
        return;
    }
    if (!sLeaving && !SettingsMenuOpen()) {
        HandlePad(input);
    }
    RunPending(fs);
    // Whatever the menu read this frame stays with the menu, even when it just handed over to the file select.
    ClearInput(input);
}

// ----- Textures. Loaded once from the archives with smooth (linear) filtering, so the art stays clean when the
// screen is not 1080p and the moon can move by fractions of a pixel.

struct Tex {
    bool tried = false;
    ImTextureID id = nullptr;
    ImVec2 size = ImVec2(0, 0);
};
std::map<std::string, Tex> sTextures;

const Tex& Load(const char* file) {
    Tex& tex = sTextures[file];
    if (tex.tried) {
        return tex;
    }
    tex.tried = true;
    std::string path = std::string("overlays/titlemenu/") + file;
    auto context = Ship::Context::GetInstance();
    auto archives = context->GetResourceManager()->GetArchiveManager();
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(context->GetWindow());
    if (archives == nullptr || !archives->HasFile(path) || window == nullptr) {
        SPDLOG_WARN("Title menu: missing {}", path);
        return tex;
    }
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE);
    initData->ResourceVersion = 0;
    initData->Path = path;
    auto image =
        std::static_pointer_cast<Ship::GuiTexture>(context->GetResourceManager()->LoadResource(path, false, initData));
    auto interpreter = window->GetInterpreterWeak().lock();
    if (image == nullptr || image->Data == nullptr || interpreter == nullptr) {
        SPDLOG_WARN("Title menu: could not read {}", path);
        return tex;
    }
    Fast::GfxRenderingAPI* api = interpreter->GetCurrentRenderingAPI();
    // The renderers only give a texture linear filtering while the game's filter is linear, so switch it for the
    // moment of the upload.
    Fast::FilteringMode filter = api->GetTextureFilter();
    if (filter != Fast::FILTER_LINEAR) {
        api->SetTextureFilter(Fast::FILTER_LINEAR);
    }
    uint32_t id = api->NewTexture();
    api->SelectTexture(0, id);
    api->SetSamplerParameters(0, true, G_TX_CLAMP, G_TX_CLAMP);
    api->UploadTexture(image->Data, image->Metadata.Width, image->Metadata.Height);
    if (filter != Fast::FILTER_LINEAR) {
        api->SetTextureFilter(filter);
    }
    tex.id = api->GetTextureById(id);
    tex.size = ImVec2((float)image->Metadata.Width, (float)image->Metadata.Height);
    context->GetResourceManager()->UnloadResource(path); // the pixels now live on the GPU
    return tex;
}

// Draws a PNG at its own size times `scale` (page units are pixels at 1080p), its left edge at x and centred on cy.
// Returns the drawn width.
float Image(const Page& pg, const char* file, float x, float cy, ImU32 tint = IM_COL32_WHITE, float scale = 1.0f,
            float scaleY = -1.0f) {
    const Tex& tex = Load(file);
    if (tex.id == nullptr) {
        return 0.0f;
    }
    float w = tex.size.x * scale, h = tex.size.y * (scaleY > 0 ? scaleY : scale);
    pg.dl->AddImage(tex.id, pg.P(x, cy - h / 2), pg.P(x + w, cy + h / 2), ImVec2(0, 0), ImVec2(1, 1), tint);
    return w;
}

ImVec2 Size(const char* file) {
    return Load(file).size;
}

// ----- Drawing.

ImU32 Alpha(ImU32 col, float a) {
    int alpha = (int)(((col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(a, 0.0f, 1.0f));
    return (col & ~IM_COL32_A_MASK) | ((ImU32)alpha << IM_COL32_A_SHIFT);
}

float Ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

bool GameRect(ImVec2& pos, ImVec2& size) {
    ImGuiWindow* gameWindow = ImGui::FindWindowByName("Main Game");
    if (gameWindow == nullptr || gameWindow->Hidden) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        pos = vp->Pos;
        size = vp->Size;
    } else {
        pos = gameWindow->InnerRect.Min;
        size = gameWindow->InnerRect.GetSize();
    }
    return size.x > 0 && size.y > 0;
}

bool Effects() {
    return CVarGetInteger(CVAR_EFFECTS, 1) != 0;
}

// A soft glow: rings of fading colour.
void Glow(const Page& pg, ImVec2 c, float r, ImU32 col, float strength) {
    for (int i = 5; i >= 0; i--) {
        float rr = r * (0.35f + i * 0.25f);
        pg.dl->AddCircleFilled(pg.P(c.x, c.y), pg.S(rr), Alpha(col, strength * (0.10f + (5 - i) * 0.05f)), 32);
    }
}

// The picture fits the screen; on wider or taller screens a blurred copy fills the rest. The moon drifts and
// breathes slowly, its eyes and Tatl glow, and motes of light rise through the air.
void DrawBackground(ImDrawList* dl, ImVec2 pos, ImVec2 size, const Page& pg, float a) {
    ImU32 white = Alpha(IM_COL32_WHITE, a);
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), Alpha(Col(10, 4, 20), a));
    const Tex& blur = Load("background_blur.jpg");
    if (blur.id != nullptr) {
        float cover = std::max(size.x / kW, size.y / kH);
        ImVec2 c(pos.x + size.x / 2, pos.y + size.y / 2);
        dl->AddImage(blur.id, ImVec2(c.x - kW * cover / 2, c.y - kH * cover / 2),
                     ImVec2(c.x + kW * cover / 2, c.y + kH * cover / 2), ImVec2(0, 0), ImVec2(1, 1),
                     Alpha(Col(150, 140, 160), a));
    }
    ImVec2 p0 = pg.P(0, 0), p1 = pg.P(kW, kH);
    const Tex& bg = Load("background.jpg");
    if (bg.id != nullptr) {
        dl->AddImage(bg.id, p0, p1, ImVec2(0, 0), ImVec2(1, 1), white);
    }

    bool fx = Effects();
    float t = sTime;
    // The moon: always a little larger than the painted one under it, so the painted edge never shows.
    float scale = 1.009f + 0.005f * std::sin(t * 0.45f);
    float dx = 1.2f * std::sin(t * 0.23f);
    float dy = -1.5f + 1.5f * std::sin(t * 0.31f);
    auto moonPoint = [&](ImVec2 p) {
        return ImVec2(kMoonCX + (p.x - kMoonCX) * scale + dx, kMoonCY + (p.y - kMoonCY) * scale + dy);
    };
    dl->PushClipRect(p0, p1, true);
    const Tex& moon = Load("moon.png");
    if (fx && moon.id != nullptr) {
        ImVec2 m0 = moonPoint(ImVec2(kMoonX0, 0)), m1 = moonPoint(ImVec2(kMoonX0 + moon.size.x, moon.size.y));
        dl->AddImage(moon.id, pg.P(m0.x, m0.y), pg.P(m1.x, m1.y), ImVec2(0, 0), ImVec2(1, 1), white);
    }
    if (fx) {
        // The eyes smoulder, one a little after the other.
        for (int i = 0; i < 2; i++) {
            float pulse = 0.55f + 0.45f * std::sin(t * 1.6f + i * 1.3f);
            Glow(pg, moon.id != nullptr ? moonPoint(kMoonEyes[i]) : kMoonEyes[i], 46.0f, Col(255, 110, 30), a * pulse);
        }
        float flutter = 0.6f + 0.25f * std::sin(t * 5.0f) + 0.15f * std::sin(t * 13.0f);
        Glow(pg, kFairy, 40.0f, Col(255, 244, 200), a * flutter);
        // Motes of light drifting up, each on its own loop.
        for (int i = 0; i < 42; i++) {
            float seed = std::fmod(i * 0.61803f, 1.0f);
            float speed = 18.0f + 30.0f * std::fmod(i * 0.377f, 1.0f);
            float life = std::fmod(t * speed / kH + seed, 1.0f);
            float x = std::fmod(seed * 1931.0f + i * 97.0f, kW) + 26.0f * std::sin(t * 0.7f + i);
            float y = kH + 20 - life * (kH + 40);
            float fade = std::sin(life * 3.14159f);
            ImU32 col = (i % 3 == 0) ? Col(255, 170, 90) : Col(200, 150, 255);
            float r = 1.4f + 2.2f * std::fmod(i * 0.29f, 1.0f);
            dl->AddCircleFilled(pg.P(x, y), pg.S(r * 2.4f), Alpha(col, a * fade * 0.12f), 12);
            dl->AddCircleFilled(pg.P(x, y), pg.S(r), Alpha(col, a * fade * 0.75f), 12);
        }
    }
    // DEFINITIVE EDITION under the logo, with a slow shimmer.
    float shimmer = fx ? 0.85f + 0.15f * std::sin(t * 1.1f) : 1.0f;
    float ew = Size("edition.png").x * 0.82f;
    Image(pg, "edition.png", 635 - ew / 2, 384, Alpha(IM_COL32_WHITE, a * shimmer), 0.82f);
    dl->PopClipRect();

    // Soft seams where the blurred copy shows.
    float feather = pg.S(40);
    ImU32 dark = Alpha(Col(10, 4, 20, 200), a), clear = Col(10, 4, 20, 0);
    if (p0.x > pos.x + 1) {
        dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(p0.x + feather, p1.y), dark, clear, clear, dark);
        dl->AddRectFilledMultiColor(ImVec2(p1.x - feather, p0.y), ImVec2(p1.x, p1.y), clear, dark, dark, clear);
    }
    if (p0.y > pos.y + 1) {
        dl->AddRectFilledMultiColor(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y + feather), dark, dark, clear, clear);
        dl->AddRectFilledMultiColor(ImVec2(p0.x, p1.y - feather), ImVec2(p1.x, p1.y), clear, clear, dark, dark);
    }
}

// A dark veil behind the buttons, fading to the right and at the top and bottom.
void DrawVeil(const Page& pg, float a) {
    ImU32 dark = Alpha(Col(12, 4, 24, 160), a), clear = Col(12, 4, 24, 0);
    float x0 = 70, x1 = 900, y0 = 440, y1 = 980;
    pg.dl->AddRectFilledMultiColor(pg.P(x0, y0), pg.P(x1, y1), dark, clear, clear, dark);
    pg.dl->AddRectFilledMultiColor(pg.P(x0, y0 - 60), pg.P(x1, y0), clear, clear, clear, dark);
    pg.dl->AddRectFilledMultiColor(pg.P(x0, y1), pg.P(x1, y1 + 60), dark, clear, clear, clear);
}

void DrawSeparator(const Page& pg, float y, float a) {
    ImU32 line = Alpha(Col(205, 175, 225, 70), a), clear = Col(205, 175, 225, 0);
    pg.dl->AddRectFilledMultiColor(pg.P(172, y), pg.P(420, y + 1.5f), clear, line, line, clear);
    pg.dl->AddRectFilledMultiColor(pg.P(420, y), pg.P(680, y + 1.5f), line, clear, clear, line);
}

// The highlight follows the selection; it glows and breathes.
void DrawCursor(const Page& pg, int selected, float dt, float a) {
    float target = RowY(selected);
    if (sCursorY < 0) {
        sCursorY = target;
    }
    sCursorY += (target - sCursorY) * std::min(1.0f, dt * 16.0f);
    float pulse = 0.82f + 0.18f * std::sin(sTime * 3.0f);
    if (Image(pg, "highlight.png", kPlateX0 - kGlow, sCursorY, Alpha(IM_COL32_WHITE, a * pulse), 1.0f, kPlateScaleY) ==
        0.0f) {
        QuestUi::Panel(pg, ImVec2(kPlateX0, sCursorY - kPlateH / 2), ImVec2(kPlateX0 + kPlateW, sCursorY + kPlateH / 2),
                       Alpha(Col(140, 60, 200), a), Alpha(Col(80, 30, 130), a), 10);
    }
}

ImU32 RowTint(bool selected, float a) {
    return Alpha(selected ? Col(255, 255, 255) : Col(222, 212, 228), a);
}

// A tap or click on a rectangle of the page. The pointer is read directly rather than through ImGui's hovered
// window, so the game's own full-screen window can never end up in front of the menu and swallow the taps.
bool Tap(const Page& pg, float x, float y, float w, float h, bool* hovered = nullptr) {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 p0 = pg.P(x, y), p1 = pg.P(x + w, y + h);
    auto inside = [&](ImVec2 m) { return m.x >= p0.x && m.x < p1.x && m.y >= p0.y && m.y < p1.y; };
    bool over = sPointerInputs && ImGui::IsMousePosValid(&io.MousePos) && inside(io.MousePos);
    if (hovered != nullptr) {
        *hovered = over;
    }
    if (over && !sTapTaken && io.MouseReleased[0] && inside(io.MouseClickedPos[0])) {
        sTapTaken = true;
        return true;
    }
    return false;
}

// One row: icon and label, and the area that takes a tap or click. Returns true when tapped.
bool Row(const Page& pg, int index, const char* id, const char* icon, const char* label, float a, float dim = 1.0f) {
    float cy = RowY(index);
    bool selected = Selection() == index;
    ImU32 tint = RowTint(selected, a * dim);
    // The selected row leans slightly to the right.
    float shift = selected ? 6.0f : 0.0f;
    if (icon != nullptr) {
        Image(pg, icon, kIconX - kIconSize / 2 + shift, cy, tint, kIconSize / std::max(Size(icon).x, 1.0f));
    }
    if (label != nullptr) {
        Image(pg, label, kLabelX + shift, cy - 2, tint, kLabelScale);
    }
    bool hovered = false;
    bool inputs = sModal == Modal::None;
    bool clicked = inputs && Tap(pg, kPlateX0, cy - kPlateH / 2, kPlateW, kPlateH, &hovered);
    ImGuiIO& io = ImGui::GetIO();
    if (inputs && hovered && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0 || clicked)) {
        if (Selection() != index) {
            Selection() = index;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
    }
    return clicked;
}

void DrawMain(const Page& pg, float dt, float a) {
    struct Item {
        const char* icon;
        const char* label;
        const char* id;
    };
    const Item items[ITEM_COUNT] = {
        { "icon_triforce.png", "label_continue.png", "##tm_continue" },
        { "icon_triforce.png", "label_new.png", "##tm_new" },
        { "icon_folder.png", "label_load.png", "##tm_load" },
        { "icon_book.png", "label_bombers.png", "##tm_bombers" },
        { "icon_scroll.png", "label_quests.png", "##tm_quests" },
        { "icon_bag.png", "label_inventory.png", "##tm_inventory" },
        { "icon_gear.png", "label_options.png", "##tm_options" },
        { "icon_mask.png", "label_extras.png", "##tm_extras" },
    };
    for (int i = 1; i < ITEM_COUNT; i++) {
        DrawSeparator(pg, RowY(i) - kRowStep / 2, a * 0.8f);
    }
    DrawCursor(pg, sSelected, dt, a);
    // Without a save, the items that need one are dimmed.
    bool hasSave = ContinueSlot(sFileSelect) >= 0;
    for (int i = 0; i < ITEM_COUNT; i++) {
        bool needsSave = i == ITEM_CONTINUE || i == ITEM_BOMBERS || i == ITEM_QUESTS || i == ITEM_INVENTORY;
        float dim = (needsSave && !hasSave) ? 0.45f : 1.0f;
        if (Row(pg, i, items[i].id, items[i].icon, items[i].label, a, dim)) {
            Activate(i);
        }
    }
}

void Arrow(const Page& pg, float cx, float cy, float dir, ImU32 col) {
    pg.dl->AddTriangleFilled(pg.P(cx - 7 * dir, cy - 11), pg.P(cx + 8 * dir, cy), pg.P(cx - 7 * dir, cy + 11), col);
}

void DrawExtras(const Page& pg, float dt, float a) {
    for (int i = 1; i < EXTRA_COUNT; i++) {
        DrawSeparator(pg, RowY(i) - kRowStep / 2, a * 0.8f);
    }
    DrawCursor(pg, sExtrasSelected, dt, a);

    // Music: the label and the track, with arrows to change it.
    float cy = RowY(EXTRA_MUSIC);
    ImU32 tint = RowTint(sExtrasSelected == EXTRA_MUSIC, a);
    QuestUi::IconNote(pg, kIconX, cy, 36, tint);
    bool clicked = Row(pg, EXTRA_MUSIC, "##tm_music", nullptr, "label_music.png", a);
    int track = std::clamp(CVarGetInteger(CVAR_MUSIC, 0), 0, kTrackCount - 1);
    float vx = 445, vw = 285;
    Arrow(pg, vx, cy, -1, tint);
    Arrow(pg, vx + vw, cy, 1, tint);
    std::string file = std::string(kTracks[track].label) + ".png";
    ImVec2 size = Size(file.c_str());
    float scale = size.x > vw - 40 ? (vw - 40) / size.x : 1.0f;
    Image(pg, file.c_str(), vx + vw / 2 - size.x * scale / 2, cy - 2, tint, scale);
    if (clicked) {
        ImVec2 mouse = ImGui::GetIO().MousePos;
        ChangeMusic(mouse.x < pg.P(vx + vw / 2, cy).x ? -1 : 1);
    }

    struct Item {
        int index;
        const char* icon;
        const char* label;
        const char* id;
    };
    const Item items[] = { { EXTRA_SOUND, "icon_gear.png", "label_sound.png", "##tm_sound" },
                           { EXTRA_OPENING, "icon_triforce.png", "label_opening.png", "##tm_opening" },
                           { EXTRA_CREDITS, "icon_scroll.png", "label_credits.png", "##tm_credits" },
                           { EXTRA_VERSION, "icon_book.png", "label_version.png", "##tm_version" } };
    for (const Item& item : items) {
        if (Row(pg, item.index, item.id, item.icon, item.label, a)) {
            ActivateExtra(item.index);
        }
    }
    cy = RowY(EXTRA_BACK);
    tint = RowTint(sExtrasSelected == EXTRA_BACK, a);
    Arrow(pg, kIconX - 4, cy, -1, tint);
    pg.dl->AddRectFilled(pg.P(kIconX - 4, cy - 4), pg.P(kIconX + 16, cy + 4), tint);
    if (Row(pg, EXTRA_BACK, "##tm_back", nullptr, "label_back.png", a)) {
        ActivateExtra(EXTRA_BACK);
    }
}

// A button hint like the picture's: a ring with the letter, or the controller's own icon when button icons are on.
// The whole hint also takes a tap or click. Returns the right edge.
float Hint(const Page& pg, ButtonPrompt prompt, const char* letter, const char* label, float x, float cy, float a,
           void (*action)()) {
    float x0 = x;
    const char* tex = ButtonPrompts_Icon(prompt);
    ImU32 col = Alpha(Col(235, 228, 240), a);
    if (tex == nullptr || !QuestUi::GameIcon(pg, tex, x + 18, cy, 38)) {
        pg.dl->AddCircle(pg.P(x + 18, cy), pg.S(17), col, 0, pg.S(2.5f));
        QuestUi::TextCentered(pg, x + 18, cy - 12, 24, col, letter, false);
    }
    x += 44;
    x += Image(pg, label, x, cy, col);
    if (Tap(pg, x0 - 8, cy - 28, x - x0 + 16, 56)) {
        action();
    }
    return x;
}

// A rounded button for the touch footer. Returns true when tapped.
bool Pill(const Page& pg, const char* text, float x, float cy, float a) {
    float size = 28, w = QuestUi::TextWidth(pg, size, text) + 70, h = 56;
    ImVec2 p0(x, cy - h / 2), p1(x + w, cy + h / 2);
    bool hovered = false;
    bool tapped = Tap(pg, p0.x, p0.y, w, h, &hovered);
    bool down = hovered && ImGui::IsMouseDown(0);
    QuestUi::Panel(pg, p0, p1, Alpha(down ? Col(150, 60, 200) : Col(60, 24, 90), a * 0.92f),
                   Alpha(down ? Col(90, 30, 140) : Col(30, 12, 48), a * 0.92f), 28);
    pg.dl->AddRect(pg.P(p0.x, p0.y), pg.P(p1.x, p1.y), Alpha(Col(215, 170, 255), a), pg.S(28), 0, pg.S(2));
    ImU32 col = Alpha(Col(245, 238, 250), a);
    Arrow(pg, x + 30, cy, -1, col);
    QuestUi::Text(pg, x + 48, cy - size * 0.62f, size, col, text);
    return tapped;
}

void DrawFooter(const Page& pg, float a) {
    bool canGoBack = sModal != Modal::None || sPanel == Panel::Extras;
    if (sTouchMode) {
        // Touch: no buttons to name, so say what to do, and give B a button of its own.
        const char* hint = "Toque numa opção para escolher";
        float size = 28, tw = QuestUi::TextWidth(pg, size, hint);
        float right = 1860;
        if (canGoBack) {
            float pw = QuestUi::TextWidth(pg, size, "Voltar") + 70;
            if (Pill(pg, "Voltar", right - pw, 1028, a)) {
                Back();
            }
            right -= pw + 34;
        }
        QuestUi::Text(pg, right - tw, 1028 - size * 0.62f, size, Alpha(Col(235, 228, 240), a * 0.9f), hint);
        return;
    }
    // Right-aligned like the picture: (A) Selecionar   (B) Voltar, with the icons of the controller in use.
    float wSel = Size("hint_select.png").x, wBack = Size("hint_back.png").x;
    float x = 1860 - (44 + wSel + 34 + 44 + wBack);
    x = Hint(pg, BUTTON_PROMPT_A, "A", "hint_select.png", x, 1032, a, Confirm);
    Hint(pg, BUTTON_PROMPT_B, "B", "hint_back.png", x + 34, 1032, a, Back);
}

void DrawToast(const Page& pg, float dt, float a) {
    if (sToastTime <= 0) {
        return;
    }
    sToastTime -= dt;
    float t = std::clamp(sToastTime / 0.4f, 0.0f, 1.0f) * a;
    float size = 26, w = QuestUi::TextWidth(pg, size, sToast.c_str()) + 48;
    float x = 820, y = 990;
    QuestUi::Panel(pg, ImVec2(x, y), ImVec2(x + w, y + 52), Alpha(Col(60, 24, 90, 230), t),
                   Alpha(Col(30, 10, 50, 230), t), 12);
    pg.dl->AddRect(pg.P(x, y), pg.P(x + w, y + 52), Alpha(Col(200, 150, 255), t), pg.S(12), 0, pg.S(1.5f));
    QuestUi::Text(pg, x + 24, y + 12, size, Alpha(Col(255, 255, 255), t), sToast.c_str());
}

// A framed card in the middle of the screen, over a dimmed menu.
void Card(const Page& pg, float w, float h, float a, ImVec2& at) {
    pg.dl->AddRectFilled(pg.P(0, 0), pg.P(kW, kH), Alpha(Col(6, 2, 12, 150), a));
    at = ImVec2(kW / 2 - w / 2, kH / 2 - h / 2 + (1.0f - a) * 30.0f);
    ImVec2 b(at.x + w, at.y + h);
    float glow = 0.6f + 0.2f * std::sin(sTime * 2.0f);
    for (int i = 4; i >= 1; i--) {
        pg.dl->AddRect(pg.P(at.x - i * 3, at.y - i * 3), pg.P(b.x + i * 3, b.y + i * 3),
                       Alpha(Col(190, 90, 255), a * glow * 0.12f), pg.S(18 + i * 3), 0, pg.S(3));
    }
    QuestUi::Panel(pg, at, b, Alpha(Col(46, 18, 70, 245), a), Alpha(Col(20, 8, 34, 245), a), 16);
    pg.dl->AddRect(pg.P(at.x, at.y), pg.P(b.x, b.y), Alpha(Col(215, 170, 255), a), pg.S(16), 0, pg.S(2));
    pg.dl->AddRect(pg.P(at.x + 8, at.y + 8), pg.P(b.x - 8, b.y - 8), Alpha(Col(236, 214, 160, 120), a), pg.S(11), 0,
                   pg.S(1));
}

// Lines of text in a card; a line that starts with '~' is smaller and dimmer.
void CardText(const Page& pg, ImVec2 at, float w, const char* const* lines, int count, float a) {
    float y = at.y + 110;
    for (int i = 0; i < count; i++) {
        bool dim = lines[i][0] == '~';
        const char* text = dim ? lines[i] + 1 : lines[i];
        float size = dim ? 22.0f : 27.0f;
        ImU32 col = dim ? Col(200, 186, 214) : Col(245, 238, 250);
        QuestUi::Text(pg, at.x + 48, y, size, Alpha(col, a), text, w - 96);
        y += QuestUi::WrappedHeight(pg, size, w - 96, text) + (dim ? 10 : 14);
    }
}

void CardTitle(const Page& pg, ImVec2 at, float w, const char* label, float a) {
    float lw = Size(label).x * 0.9f;
    ImU32 gold = Alpha(Col(236, 214, 160, 160), a);
    Image(pg, label, at.x + w / 2 - lw / 2, at.y + 52, Alpha(Col(236, 214, 160), a), 0.9f);
    pg.dl->AddRectFilledMultiColor(pg.P(at.x + 60, at.y + 88), pg.P(at.x + w / 2, at.y + 89.5f), 0, gold, gold, 0);
    pg.dl->AddRectFilledMultiColor(pg.P(at.x + w / 2, at.y + 88), pg.P(at.x + w - 60, at.y + 89.5f), gold, 0, 0, gold);
}

// A small button in a card; returns true when clicked.
bool CardButton(const Page& pg, const char* id, const char* label, float cx, float cy, bool selected, float a,
                bool* hovered) {
    float w = 220, h = 64;
    ImVec2 p0(cx - w / 2, cy - h / 2), p1(cx + w / 2, cy + h / 2);
    if (selected) {
        float pulse = 0.8f + 0.2f * std::sin(sTime * 3.0f);
        for (int i = 3; i >= 1; i--) {
            pg.dl->AddRect(pg.P(p0.x - i * 3, p0.y - i * 3), pg.P(p1.x + i * 3, p1.y + i * 3),
                           Alpha(Col(200, 110, 255), a * pulse * 0.18f), pg.S(14 + i * 3), 0, pg.S(3));
        }
        QuestUi::Panel(pg, p0, p1, Alpha(Col(150, 60, 200), a), Alpha(Col(90, 30, 140), a), 12);
        pg.dl->AddRect(pg.P(p0.x, p0.y), pg.P(p1.x, p1.y), Alpha(Col(230, 185, 255), a), pg.S(12), 0, pg.S(2.5f));
    } else {
        QuestUi::Panel(pg, p0, p1, Alpha(Col(40, 16, 60), a), Alpha(Col(26, 10, 40), a), 12);
        pg.dl->AddRect(pg.P(p0.x, p0.y), pg.P(p1.x, p1.y), Alpha(Col(150, 110, 180), a), pg.S(12), 0, pg.S(1.5f));
    }
    float lw = Size(label).x * 0.8f;
    Image(pg, label, cx - lw / 2, cy - 2, RowTint(selected, a), 0.8f);
    (void)id;
    return Tap(pg, p0.x, p0.y, w, h, hovered);
}

void DrawModal(const Page& pg, float dt, float a) {
    sModalT = std::min(1.0f, sModalT + dt * 6.0f);
    float t = Ease(sModalT) * a;
    ImVec2 at;
    if (sModal == Modal::ConfirmNew) {
        float w = 760, h = 360;
        Card(pg, w, h, t, at);
        CardTitle(pg, at, w, "label_new.png", t);
        char line[160];
        snprintf(line, sizeof(line), "O jogo novo começa no Arquivo %d.", EmptySlot(sFileSelect) + 1);
        const char* lines[] = { line, "~Os seus outros saves continuam guardados." };
        CardText(pg, at, w, lines, 2, t);
        bool hovYes = false, hovNo = false;
        ImGuiIO& io = ImGui::GetIO();
        bool moved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
        if (CardButton(pg, "##tm_yes", "label_yes.png", at.x + w / 2 - 140, at.y + h - 70, sConfirmYes, t, &hovYes)) {
            ConfirmNewGame(true);
        } else if (CardButton(pg, "##tm_no", "label_no.png", at.x + w / 2 + 140, at.y + h - 70, !sConfirmYes, t,
                              &hovNo)) {
            ConfirmNewGame(false);
        } else if (moved && (hovYes || hovNo) && sConfirmYes != hovYes) {
            sConfirmYes = hovYes;
            Audio_PlaySfx(NA_SE_SY_CURSOR);
        }
        return;
    }

    bool credits = sModal == Modal::Credits;
    float w = 900, h = credits ? 560 : 470;
    Card(pg, w, h, t, at);
    CardTitle(pg, at, w, credits ? "label_credits.png" : "label_version.png", t);
    if (credits) {
        const char* lines[] = {
            "Majora's Mask Android Definitive Edition",
            "~Projeto e imagem da tela inicial: Rafael (lokodoshape-png).",
            "~Baseado no 2 Ship 2 Harkinian, da Harbour Masters, e na libultraship.",
            "~Feito sobre a decompilação de Majora's Mask da comunidade zeldaret.",
            "~The Legend of Zelda: Majora's Mask é da Nintendo. Este projeto não traz a ROM nem arquivos do jogo; "
            "eles são gerados da sua própria cópia.",
        };
        CardText(pg, at, w, lines, 5, t);
    } else {
        char base[96], commit[160], date[96];
        std::string hash(gGitCommitHash);
        snprintf(base, sizeof(base), "~Base 2 Ship 2 Harkinian %s", gBuildVersion);
        snprintf(commit, sizeof(commit), "~Commit %s (branch %s)", hash.substr(0, 7).c_str(), gGitBranch);
        snprintf(date, sizeof(date), "~Compilado em %s", gBuildDate);
        const char* lines[] = { "Majora's Mask Android Definitive Edition", base, commit, date,
                                "~Tela inicial nova: versão 1.0" };
        CardText(pg, at, w, lines, 5, t);
    }
    bool hovered = false;
    if (CardButton(pg, "##tm_close", "label_back.png", at.x + w / 2, at.y + h - 64, true, t, &hovered)) {
        CloseModal();
    }
}

void SyncTouchControls(bool covering) {
#ifdef __ANDROID__
    // The touch buttons are hidden while the menu is up, so taps reach its buttons. The settings menu does the same
    // while it is open, and turns them back on when it closes.
    static bool sHidden = false;
    static bool sMenuWasOpen = false;
    bool menuOpen = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetMenuOrMenubarVisible();
    if (covering && !menuOpen && (!sHidden || sMenuWasOpen)) {
        Ship::Mobile::DisableTouchArea();
        sHidden = true;
    } else if (!covering && sHidden) {
        if (!menuOpen) {
            Ship::Mobile::EnableTouchArea();
        }
        sHidden = false;
    }
    sMenuWasOpen = menuOpen;
#else
    (void)covering;
#endif
}

void DrawMenu() {
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    sTime += dt;
    bool shown = sActive && sFileSelect != nullptr;
    sFade = std::clamp(sFade + (shown ? dt * 4.0f : -dt * 6.0f), 0.0f, 1.0f);
    SyncTouchControls(shown && !sLeaving);
    if (sFade <= 0.0f) {
        return;
    }
    ImVec2 pos, size;
    if (!GameRect(pos, size)) {
        return;
    }
    float a = sFade;
    float k = std::min(size.x / kW, size.y / kH);
    ImVec2 origin(pos.x + (size.x - kW * k) / 2, pos.y + (size.y - kH * k) / 2);

    bool inputs = shown && !sLeaving && !SettingsMenuOpen();
    if (inputs) {
        KeyboardNav(dt);
    }
    ImGuiIO& io = ImGui::GetIO();
    sPointerInputs = inputs && sFade >= 0.6f;
    sTapTaken = false;
    if (sPointerInputs && io.MouseClicked[0]) {
#ifdef __ANDROID__
        sTouchMode = true;
#else
        sTouchMode = io.MouseSource == ImGuiMouseSource_TouchScreen;
#endif
    }
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##TitleMenu", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollWithMouse | (inputs ? 0 : ImGuiWindowFlags_NoInputs));
    if (inputs) {
        // Keep the menu in front of the game's window (and so its taps away from the game) while it is in use.
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Page pg{ dl, origin, k };

    DrawBackground(dl, pos, size, pg, a);
    DrawVeil(pg, a);
    // Switching between the main list and Extras slides the rows in.
    sPanelT = std::min(1.0f, sPanelT + dt * 5.0f);
    float pt = Ease(sPanelT);
    Page rows{ dl, ImVec2(origin.x - (1.0f - pt) * 40.0f * k, origin.y), k };
    if (sPanel == Panel::Main) {
        DrawMain(rows, dt, a * pt);
    } else {
        DrawExtras(rows, dt, a * pt);
    }
    DrawFooter(pg, a);
    DrawToast(pg, dt, a);
    if (sModal != Modal::None) {
        DrawModal(pg, dt, a);
    }

    // A save is loading: fade to black with the file select.
    if (sLeaving && sFileSelect != nullptr) {
        int fill = std::clamp((int)sFileSelect->screenFillAlpha, 0, 255);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, fill));
    }
    // Fade in from black when the menu appears.
    if (shown && a < 1.0f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, (int)((1.0f - a) * 255)));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace
} // namespace TitleMenu

void TitleMenuWindow::Draw() {
    TitleMenu::DrawMenu();
}

void TitleMenuWindow::InitElement() {
}

void TitleMenuWindow::DrawElement() {
}

void TitleMenuWindow::UpdateElement() {
}

static void RegisterTitleMenu() {
    // Instead of the title screen, go to the file select, where the menu takes over. Normally the title scene sets
    // these up; this is what the "Skip to File Select" option does after the boot logo.
    COND_VB_SHOULD(VB_START_TITLE_SCREEN, CVarGetInteger(CVAR_ENABLED, 1), {
        if (TitleMenu::sAllowOriginalTitle) {
            TitleMenu::sAllowOriginalTitle = false;
        } else {
            *should = false;
            Rand_Seed(osGetTime());
            gSaveContext.seqId = NA_BGM_DISABLED;
            gSaveContext.ambienceId = AMBIENCE_ID_DISABLED;
            Sram_InitNewSave();
            gSaveContext.save.time = CLOCK_TIME(8, 0);
            gSaveContext.save.day = 1;
            gSaveContext.save.playerForm = PLAYER_FORM_HUMAN;
            gSaveContext.gameMode = GAMEMODE_FILE_SELECT;
            STOP_GAMESTATE(gGameState);
            SET_NEXT_GAMESTATE(gGameState, FileSelect_Init, sizeof(FileSelectState));
        }
    });
    // B on the file select comes back to the menu instead of the title screen.
    COND_VB_SHOULD(VB_FILE_SELECT_EXIT_TO_TITLE, CVarGetInteger(CVAR_ENABLED, 1), {
        if (TitleMenu::sFileSelect != nullptr) {
            *should = false;
            Audio_PlaySfx(NA_SE_SY_FSEL_CLOSE);
            TitleMenu::Show();
        }
    });
    COND_HOOK(OnGameStateMainStart, true, TitleMenu::OnGameFrame);
    // Remember the file that was played, for Continuar.
    COND_HOOK(OnSaveLoad, true, [](s16 fileNum) {
        if (gSaveContext.gameMode == GAMEMODE_NORMAL && fileNum >= 0 && fileNum < 3 &&
            CVarGetInteger(CVAR_LAST_SLOT, -1) != fileNum) {
            CVarSetInteger(CVAR_LAST_SLOT, fileNum);
            Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
    });
}

static RegisterShipInitFunc sTitleMenuInit(RegisterTitleMenu, { CVAR_ENABLED });
