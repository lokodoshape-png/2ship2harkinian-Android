#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/DeveloperTools/DeveloperTools.h"
#include "2s2h/Enhancements/GfxPatcher/AuthenticGfxPatches.h"
#include "UIWidgets.hpp"
#include "BenMenuBar.h"
#include "Notification.h"
#include "macros.h"
#include "variables.h"
#include <variant>
#include <tuple>

extern "C" {
#include "functions.h"
extern PlayState* gPlayState;
}

typedef enum {
    COLOR_WHITE,
    COLOR_GRAY,
    COLOR_DARK_GRAY,
    COLOR_INDIGO,
    COLOR_RED,
    COLOR_DARK_RED,
    COLOR_LIGHT_GREEN,
    COLOR_GREEN,
    COLOR_DARK_GREEN,
    COLOR_YELLOW,
} ColorOption;

typedef enum {
    DISABLE_FOR_CAMERAS_OFF,
    DISABLE_FOR_DEBUG_CAM_ON,
    DISABLE_FOR_DEBUG_CAM_OFF,
    DISABLE_FOR_FREE_LOOK_ON,
    DISABLE_FOR_FREE_LOOK_OFF,
    DISABLE_FOR_GYRO_OFF,
    DISABLE_FOR_GYRO_ON,
    DISABLE_FOR_RIGHT_STICK_OFF,
    DISABLE_FOR_AUTO_SAVE_OFF,
    DISABLE_FOR_NULL_PLAY_STATE,
    DISABLE_FOR_DEBUG_MODE_OFF,
    DISABLE_FOR_NO_VSYNC,
    DISABLE_FOR_NO_WINDOWED_FULLSCREEN,
    DISABLE_FOR_NO_MULTI_VIEWPORT,
    DISABLE_FOR_NOT_DIRECTX,
    DISABLE_FOR_DIRECTX,
    DISABLE_FOR_MATCH_REFRESH_RATE_ON,
    DISABLE_FOR_MOTION_BLUR_MODE,
    DISABLE_FOR_MOTION_BLUR_OFF,
    DISABLE_FOR_FRAME_ADVANCE_OFF,
    DISABLE_FOR_WARP_POINT_NOT_SET,
    DISABLE_FOR_INTRO_SKIP_OFF,
} DisableOption;

struct widgetInfo;
struct disabledInfo;
using VoidFunc = void (*)();
using DisableInfoFunc = bool (*)(disabledInfo&);
using DisableVec = std::vector<DisableOption>;
using WidgetFunc = void (*)(widgetInfo&);
std::string disabledTempTooltip;
const char* disabledTooltip;
bool disabledValue = false;
ColorOption menuThemeIndex = COLOR_INDIGO;
const ImVec4 COLOR_NONE = { 0, 0, 0, 0 };

typedef enum {
    WIDGET_CHECKBOX,
    WIDGET_COMBOBOX,
    WIDGET_SLIDER_INT,
    WIDGET_SLIDER_FLOAT,
    WIDGET_CVAR_CHECKBOX,
    WIDGET_CVAR_COMBOBOX,
    WIDGET_CVAR_SLIDER_INT,
    WIDGET_CVAR_SLIDER_FLOAT,
    WIDGET_BUTTON,
    WIDGET_COLOR_24, // color picker without alpha
    WIDGET_COLOR_32, // color picker with alpha
    WIDGET_SEARCH,
    WIDGET_SEPARATOR,
    WIDGET_SEPARATOR_TEXT,
    WIDGET_TEXT,
    WIDGET_WINDOW_BUTTON,
    WIDGET_AUDIO_BACKEND, // needed because of special operations that can't be handled easily with the normal combobox
                          // widget
    WIDGET_VIDEO_BACKEND, // same as above
    WIDGET_CUSTOM         // drawn entirely by widgetCallback (for sections shared with the menu bar)
} WidgetType;

typedef enum {
    MOTION_BLUR_DYNAMIC,
    MOTION_BLUR_ALWAYS_OFF,
    MOTION_BLUR_ALWAYS_ON,
} MotionBlurOption;

typedef enum {
    DEBUG_LOG_TRACE,
    DEBUG_LOG_DEBUG,
    DEBUG_LOG_INFO,
    DEBUG_LOG_WARN,
    DEBUG_LOG_ERROR,
    DEBUG_LOG_CRITICAL,
    DEBUG_LOG_OFF,
} DebugLogOption;

// holds the widget values for a widget, contains all CVar types available from LUS. int32_t is used for boolean
// evaluation
using CVarVariant = std::variant<int32_t, const char*, float, Color_RGBA8, Color_RGB8>;

// contains various information used to display specific types of widgets
// `min` and `max` are usually only used by sliders
// `defaultVariant` can be used by all widgetTypes, but defaults to 0, "", 0.0f, and white respectively, so only needed
// when you want to change those defaults
// `comboBoxOptions` is the list of dropdown options to be added to a dropdown.
// this needs to be a map of int32_t to const char*, but can also be enum values
// `valuePointer` is used for non-CVar sliders to track current widgetValue
// `size` applies only to buttons, determines whether it stretches to fill its current container or only fits its text
// `color` can be used in any way, depending on the widget. color pickers (COLOR_24 and COLOR_32) will eventually
// use this for storing its current value; WIDGET_SEPARATOR_TEXT uses this to change the text color `windowName`
// is used when using WIDGET_WINDOW_BUTTON, which optionally draws windows, like Stats, in the menu with a button
// to pop the window out of the main menu overlay
// `windowName` is what is displayed and searched for `windowButton` type and window interactions
// `labelPosition` applies to checkbox, combobox and button types. specifies label orientation compared to widget body
// `sameLine` allows widgets to be displayed on the same line as the previously registered widget
// the next three only apply to sliders. `showButtons` shows or hides the +/- buttons on either side of a slider
// `format` specifies how info is displayed within the slider
// `isPercentage` toggles the float slider's value being multiplied by 100 to show percentages instead of direct floats
struct WidgetOptions {
    CVarVariant min;
    CVarVariant max;
    CVarVariant defaultVariant;
    std::unordered_map<int32_t, const char*> comboBoxOptions;
    std::variant<bool*, int32_t*, float*> valuePointer;
    ImVec2 size = UIWidgets::Sizes::Fill;
    ImVec4 color = COLOR_NONE;
    const char* windowName = "";
    UIWidgets::LabelPosition labelPosition = UIWidgets::LabelPosition::None;
    bool sameLine = false;
    bool showButtons = true;
    const char* format = "%f";
    float step = 0.01f;
    bool isPercentage = false;
};

bool operator==(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return l.r == r.r && l.g == r.g && l.b == r.b;
}

bool operator==(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return l.r == r.r && l.g == r.g && l.b == r.b && l.a == r.a;
}

bool operator<(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return (l.r < r.r && l.g <= r.g && l.b <= r.b) || (l.r <= r.r && l.g < r.g && l.b <= r.b) ||
           (l.r <= r.r && l.g <= r.g && l.b < r.b);
}

bool operator<(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return (l.r < r.r && l.g <= r.g && l.b <= r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g < r.g && l.b <= r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g <= r.g && l.b < r.b && l.a <= r.a) ||
           (l.r <= r.r && l.g <= r.g && l.b <= r.b && l.a < r.a);
}

bool operator>(Color_RGB8 const& l, Color_RGB8 const& r) noexcept {
    return (l.r > r.r && l.g >= r.g && l.b >= r.b) || (l.r >= r.r && l.g > r.g && l.b >= r.b) ||
           (l.r >= r.r && l.g >= r.g && l.b > r.b);
}

bool operator>(Color_RGBA8 const& l, Color_RGBA8 const& r) noexcept {
    return (l.r > r.r && l.g >= r.g && l.b >= r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g > r.g && l.b >= r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g >= r.g && l.b > r.b && l.a >= r.a) ||
           (l.r >= r.r && l.g >= r.g && l.b >= r.b && l.a > r.a);
}

std::unordered_map<ColorOption, ImVec4> menuTheme = { { COLOR_WHITE, UIWidgets::Colors::White },
                                                      { COLOR_GRAY, UIWidgets::Colors::Gray },
                                                      { COLOR_DARK_GRAY, UIWidgets::Colors::DarkGray },
                                                      { COLOR_INDIGO, UIWidgets::Colors::Indigo },
                                                      { COLOR_RED, UIWidgets::Colors::Red },
                                                      { COLOR_DARK_RED, UIWidgets::Colors::DarkRed },
                                                      { COLOR_LIGHT_GREEN, UIWidgets::Colors::LightGreen },
                                                      { COLOR_GREEN, UIWidgets::Colors::Green },
                                                      { COLOR_DARK_GREEN, UIWidgets::Colors::DarkGreen },
                                                      { COLOR_YELLOW, UIWidgets::Colors::Yellow } };

// All the info needed for display and search of all widgets in the menu. WidgetName is the label displayed,
// `widgetCVar` is the string representation of the CVar used to store the widget value
// `widgetTooltip` is what is displayed when hovering (except when disabled, more on that later)
// `widgetType` is the WidgetType for the widget, which is what determines how the information is used in the draw
// function. all of the preceding are required parts for every widget except for the special widgets (backend dropdowns,
// separators, etc) various parts of widgetOptions are required depending on what widget type you're using
// `widgetCallback` is a lambda used for running code on widget change
// `preFunc` is a lambda called before drawing code starts. It can be used to determine a widget's status,
// whether disabled or hidden, as well as update pointers for non-CVar widget types.
// `postFunc` is a lambda called after all drawing code is finished, for reacting to states other than
// widgets having been changed, like holding buttons.
// All three lambdas accept a `widgetInfo` pointer in case it needs information on the widget for these operations
// `activeDisables` is a vector of DisableOptions for specifying what reasons a widget is disabled, which are displayed
// in the disabledTooltip for the widget. Can display multiple reasons. Handling the reasons is done in `modifierFunc`.
// It is recommended to utilize `disabledInfo`/`DisableReason` to list out all reasons for disabling and isHidden so
// the info can be shown.
// `isHidden` just prevents the widget from being drawn under whatever circumstances you specify in the `modifierFunc`
struct widgetInfo {
    std::string widgetName; // Used by all widgets
    const char* widgetCVar; // Used by all widgets except
    const char* widgetTooltip;
    WidgetType widgetType;
    WidgetOptions widgetOptions;
    WidgetFunc widgetCallback = nullptr;
    WidgetFunc preFunc = nullptr;
    WidgetFunc postFunc = nullptr;
    DisableVec activeDisables = {};
    bool isHidden = false;
};

// `disabledInfo` holds information on reasons for hiding or disabling a widget, as well as an evaluation lambda that
// is run once per frame to update its status (this is done to prevent dozens of redundant CVarGets in each frame loop)
// `evaluation` returns a bool which can be determined by whatever code you want that changes its status
// `reason` is the text displayed in the disabledTooltip when a widget is disabled by a particular DisableReason
// `active` is what's referenced when determining disabled status for a widget that uses this This can also be used to
// hold reasons to hide widgets so that their evaluations are also only run once per frame
struct disabledInfo {
    DisableInfoFunc evaluation;
    const char* reason;
    bool active = false;
    int32_t value = 0;
};

// Contains the name displayed in the sidebar (label), the number of columns to use in drawing (columnCount; for visual
// separation, 1-3), and nested vectors of the widgets, grouped by column (columnWidgets). The number of widget vectors
// added to the column groups does not need to match the specified columnCount, e.g. you can have one vector added to
// the sidebar, but still separate the window into 3 columns and display only in the first column
struct SidebarEntry {
    std::string label;
    uint32_t columnCount;
    std::vector<std::vector<widgetInfo>> columnWidgets;
};

// Contains entries for what's listed in the header at the top, including the name displayed on the top bar (label),
// a vector of the SidebarEntries for that header entry, and the name of the cvar used to track what sidebar entry is
// the last viewed for that header.
struct MainMenuEntry {
    std::string label;
    std::vector<SidebarEntry> sidebarEntries;
    const char* sidebarCvar;
};
extern std::unordered_map<s16, const char*> warpPointSceneList;
extern void Warp();

namespace BenGui {
extern std::shared_ptr<std::vector<Ship::WindowBackend>> availableWindowBackends;
extern std::unordered_map<Ship::WindowBackend, const char*> availableWindowBackendsMap;
extern Ship::WindowBackend configWindowBackend;
extern void UpdateWindowBackendObjects();
extern void DrawWindowBackendButtons();
extern void DrawLanguageOptions();
extern void DrawModsOptions();

std::vector<MainMenuEntry> menuEntries;
static ImGuiTextFilter menuSearch;
std::vector<SidebarEntry> settingsSidebar;
std::vector<SidebarEntry> enhancementsSidebar;
std::vector<SidebarEntry> devToolsSidebar;
uint8_t searchSidebarIndex = 0;
SidebarEntry searchSidebarEntry = {
    "Buscar", 1, { { { "Tema do Menu", "", "Busca o texto em todos os menus, incluindo as dicas.", WIDGET_SEARCH } } }
};

static std::map<DisableOption, disabledInfo> disabledMap = {
    { DISABLE_FOR_CAMERAS_OFF,
      { [](disabledInfo& info) -> bool {
           return !CVarGetInteger("gEnhancements.Camera.DebugCam.Enable", 0) &&
                  !CVarGetInteger("gEnhancements.Camera.FreeLook.Enable", 0);
       },
        "Câmera de Depuração e Câmera Livre estão desativadas" } },
    { DISABLE_FOR_DEBUG_CAM_ON,
      { [](disabledInfo& info) -> bool { return CVarGetInteger("gEnhancements.Camera.DebugCam.Enable", 0); },
        "Câmera de Depuração está ativada" } },
    { DISABLE_FOR_DEBUG_CAM_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnhancements.Camera.DebugCam.Enable", 0); },
        "Câmera de Depuração está desativada" } },
    { DISABLE_FOR_FREE_LOOK_ON,
      { [](disabledInfo& info) -> bool { return CVarGetInteger("gEnhancements.Camera.FreeLook.Enable", 0); },
        "Câmera Livre está ativada" } },
    { DISABLE_FOR_FREE_LOOK_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnhancements.Camera.FreeLook.Enable", 0); },
        "Câmera Livre está desativada" } },
    { DISABLE_FOR_GYRO_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnhancements.Camera.FirstPerson.GyroEnabled", 0); },
        "Mira por Giroscópio está desativada" } },
    { DISABLE_FOR_GYRO_ON,
      { [](disabledInfo& info) -> bool { return CVarGetInteger("gEnhancements.Camera.FirstPerson.GyroEnabled", 0); },
        "Mira por Giroscópio está ativada" } },
    { DISABLE_FOR_RIGHT_STICK_OFF,
      { [](disabledInfo& info) -> bool {
           return !CVarGetInteger("gEnhancements.Camera.FirstPerson.RightStickEnabled", 0);
       },
        "Mira pelo Analógico Direito está desativada" } },
    { DISABLE_FOR_AUTO_SAVE_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnhancements.Saving.Autosave", 0); },
        "Salvamento Automático está desativado" } },
    { DISABLE_FOR_NULL_PLAY_STATE,
      { [](disabledInfo& info) -> bool { return gPlayState == NULL; }, "Nenhum save carregado" } },
    { DISABLE_FOR_DEBUG_MODE_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gDeveloperTools.DebugEnabled", 0); },
        "Modo de Depuração está desativado" } },
    { DISABLE_FOR_NO_VSYNC,
      { [](disabledInfo& info) -> bool { return !Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync(); },
        "Desativar o VSync não é suportado" } },
    { DISABLE_FOR_NO_WINDOWED_FULLSCREEN,
      { [](disabledInfo& info) -> bool {
           return !Ship::Context::GetInstance()->GetWindow()->SupportsWindowedFullscreen();
       },
        "Tela cheia em janela não é suportada" } },
    { DISABLE_FOR_NO_MULTI_VIEWPORT,
      { [](disabledInfo& info) -> bool {
           return !Ship::Context::GetInstance()->GetWindow()->GetGui()->SupportsViewports();
       },
        "Múltiplas janelas não são suportadas" } },
    { DISABLE_FOR_NOT_DIRECTX,
      { [](disabledInfo& info) -> bool {
           return Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() !=
                  Ship::WindowBackend::FAST3D_DXGI_DX11;
       },
        "Disponível apenas no DirectX" } },
    { DISABLE_FOR_DIRECTX,
      { [](disabledInfo& info) -> bool {
           return Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
                  Ship::WindowBackend::FAST3D_DXGI_DX11;
       },
        "Indisponível no DirectX" } },
    { DISABLE_FOR_MATCH_REFRESH_RATE_ON,
      { [](disabledInfo& info) -> bool { return CVarGetInteger("gMatchRefreshRate", 0); },
        "Igualar Taxa de Atualização está ativado" } },
    { DISABLE_FOR_MOTION_BLUR_MODE,
      { [](disabledInfo& info) -> bool {
           info.value = CVarGetInteger("gEnhancements.Graphics.MotionBlur.Mode", 0);
           return !info.value;
       },
        "Modo de Desfoque de Movimento incompatível" } },
    { DISABLE_FOR_MOTION_BLUR_OFF,
      { [](disabledInfo& info) -> bool { return !R_MOTION_BLUR_ENABLED; }, "Desfoque de Movimento está desativado" } },
    { DISABLE_FOR_FRAME_ADVANCE_OFF,
      { [](disabledInfo& info) -> bool { return !(gPlayState != nullptr && gPlayState->frameAdvCtx.enabled); },
        "Avanço de Quadro está desativado" } },
    { DISABLE_FOR_WARP_POINT_NOT_SET,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger(WARP_POINT_CVAR "Salvo", 0); },
        "Ponto de teletransporte não salvo" } },
    { DISABLE_FOR_INTRO_SKIP_OFF,
      { [](disabledInfo& info) -> bool { return !CVarGetInteger("gEnhancements.Cutscenes.SkipIntroSequence", 0); },
        "Pular Introdução não está selecionado" } }
};

std::unordered_map<int32_t, const char*> menuThemeOptions = {
    { COLOR_WHITE, "Branco" },
    { COLOR_GRAY, "Cinza" },
    { COLOR_DARK_GRAY, "Cinza Escuro" },
    { COLOR_INDIGO, "Anil" },
    { COLOR_RED, "Vermelho" },
    { COLOR_DARK_RED, "Vermelho Escuro" },
    { COLOR_LIGHT_GREEN, "Verde Claro" },
    { COLOR_GREEN, "Verde" },
    { COLOR_DARK_GREEN, "Verde Escuro" },
    { COLOR_YELLOW, "Amarelo" },
};

static const std::unordered_map<int32_t, const char*> alwaysWinDoggyraceOptions = {
    { ALWAYS_WIN_DOGGY_RACE_OFF, "Desligado" },
    { ALWAYS_WIN_DOGGY_RACE_MASKOFTRUTH, "Com a Máscara da Verdade" },
    { ALWAYS_WIN_DOGGY_RACE_ALWAYS, "Sempre" },
};

static const std::unordered_map<int32_t, const char*> cremiaRewardOptions = {
    { CREMIA_REWARD_RANDOM, "Original" },
    { CREMIA_REWARD_ALWAYS_HUG, "Abraço" },
    { CREMIA_REWARD_ALWAYS_RUPEE, "Rupias" },
};

static const std::unordered_map<int32_t, const char*> clockTypeOptions = {
    { CLOCK_TYPE_ORIGINAL, "Original" },
    { CLOCK_TYPE_3DS, "Estilo MM3D" },
    { CLOCK_TYPE_TEXT_BASED, "Só texto" },
};

static const std::unordered_map<int32_t, const char*> textureFilteringMap = {
    { FILTER_THREE_POINT, "Três pontos" },
    { FILTER_LINEAR, "Linear" },
    { FILTER_NONE, "Nenhum" },
};

static const std::unordered_map<int32_t, const char*> motionBlurOptions = {
    { MOTION_BLUR_DYNAMIC, "Dinâmico (padrão)" },
    { MOTION_BLUR_ALWAYS_OFF, "Sempre desligado" },
    { MOTION_BLUR_ALWAYS_ON, "Sempre ligado" },
};

static const std::unordered_map<int32_t, const char*> debugSaveOptions = {
    { DEBUG_SAVE_INFO_COMPLETE, "Save 100\%" },
    { DEBUG_SAVE_INFO_VANILLA_DEBUG, "Save de depuração original" },
    { DEBUG_SAVE_INFO_NONE, "Save vazio" },
};

static const std::unordered_map<int32_t, const char*> logLevels = {
    { DEBUG_LOG_TRACE, "Trace" },   { DEBUG_LOG_DEBUG, "Debug" }, { DEBUG_LOG_INFO, "Info" },
    { DEBUG_LOG_WARN, "Warn" },     { DEBUG_LOG_ERROR, "Error" }, { DEBUG_LOG_CRITICAL, "Critical" },
    { DEBUG_LOG_OFF, "Desligado" },
};

static const std::unordered_map<Ship::AudioBackend, const char*> audioBackendsMap = {
    { Ship::AudioBackend::WASAPI, "Windows Audio Session API" },
    { Ship::AudioBackend::SDL, "SDL" },
};

static std::unordered_map<Ship::WindowBackend, const char*> windowBackendsMap = {
    { Ship::WindowBackend::FAST3D_DXGI_DX11, "DirectX" },
    { Ship::WindowBackend::FAST3D_SDL_OPENGL, "OpenGL" },
    { Ship::WindowBackend::FAST3D_SDL_METAL, "Metal" },
};

static const std::unordered_map<int32_t, const char*> timeStopOptions = {
    { TIME_STOP_OFF, "Desligado" },
    { TIME_STOP_TEMPLES, "Templos" },
    { TIME_STOP_TEMPLES_DUNGEONS, "Templos + Mini Masmorras" },
};

static const std::unordered_map<int32_t, const char*> notificationPosition = {
    { 0, "Superior Esquerdo" }, { 1, "Superior Direito" }, { 2, "Inferior Esquerdo" },
    { 3, "Inferior Direito" },  { 4, "Oculto" },
};

void FreeLookPitchMinMax() {
    f32 maxY = CVarGetFloat("gEnhancements.Camera.FreeLook.MaxPitch", 72.0f);
    f32 minY = CVarGetFloat("gEnhancements.Camera.FreeLook.MinPitch", -49.0f);
    CVarSetFloat("gEnhancements.Camera.FreeLook.MaxPitch", std::max(maxY, minY));
    CVarSetFloat("gEnhancements.Camera.FreeLook.MinPitch", std::min(maxY, minY));
}

void InsertSidebarSearch() {
    menuEntries[0].sidebarEntries.insert(menuEntries[0].sidebarEntries.begin() + searchSidebarIndex,
                                         searchSidebarEntry);
    CVarSetInteger(menuEntries[0].sidebarCvar, CVarGetInteger(menuEntries[0].sidebarCvar, 0) + 1);
}

void AddSettings() {
    // General Settings
    settingsSidebar.push_back(
        { "Geral",
          3,
          { {
              { "Tema do Menu",
                "gSettings.MenuTheme",
                "Muda o tema dos elementos do menu.",
                WIDGET_CVAR_COMBOBOX,
                { .defaultVariant = COLOR_INDIGO, .comboBoxOptions = menuThemeOptions } },
#if not defined(__SWITCH__) and not defined(__WIIU__)
              { "Navegar no Menu com Controle", CVAR_IMGUI_CONTROLLER_NAV,
                "Permite navegar no menu do 2Ship (Configurações, Melhorias...) com o controle.\nATENÇÃO: os comandos "
                "do jogo ficam desativados enquanto o menu estiver aberto.\n\nD-pad para mover entre os itens, A para "
                "selecionar, B para voltar.",
                WIDGET_CVAR_CHECKBOX },
              { "Cursor Sempre Visível",
                "gSettings.CursorVisibility",
                "Deixa o cursor sempre visível, mesmo em tela cheia.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) {
                    Ship::Context::GetInstance()->GetWindow()->SetForceCursorVisibility(
                        CVarGetInteger("gSettings.CursorVisibility", 0));
                } },
#endif
              { "Busca na Barra Lateral",
                "gSettings.SidebarSearch",
                "Mostra a Busca como item da barra lateral em Configurações, em vez de no topo.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) {
                    if (CVarGetInteger("gSettings.SidebarSearch", 0)) {
                        InsertSidebarSearch();
                    } else {
                        std::erase_if(menuEntries[0].sidebarEntries, [](SidebarEntry entry) {
                            return strncmp(entry.label.c_str(), searchSidebarEntry.label.c_str(), 5) == 0;
                        });
                        int32_t sidebarVal = CVarGetInteger(menuEntries[0].sidebarCvar, 0);
                        if (sidebarVal < menuEntries.size()) {
                            sidebarVal -= 1;
                        } else {
                            sidebarVal = menuEntries.size() - 1;
                        }
                        CVarSetInteger(menuEntries[0].sidebarCvar, sidebarVal);
                    }
                } },
              { "Foco Automático na Busca", "gSettings.SearchAutofocus",
                "A caixa de busca recebe o foco ao aparecer. Não afeta os outros elementos.", WIDGET_CVAR_CHECKBOX },
              { .widgetName = "Atalho Tab para Assets Alternativos",
                .widgetCVar = "gEnhancements.Mods.AlternateAssetsHotkey",
                .widgetTooltip = "Permite usar a tecla Tab para alternar os assets alternativos",
                .widgetType = WIDGET_CVAR_CHECKBOX,
                .widgetOptions = { .defaultVariant = true } },
              { "Abrir Pasta de Arquivos do App",
                "",
                "Abre a pasta que contém as pastas de saves, mods etc.",
                WIDGET_BUTTON,
                {},
                [](widgetInfo& info) {
                    std::string filesPath = Ship::Context::GetInstance()->GetAppDirectoryPath();
                    SDL_OpenURL(std::string("file:///" + std::filesystem::absolute(filesPath).string()).c_str());
                } },
          } } });
    // Audio Settings
    settingsSidebar.push_back(
        { "Áudio",
          3,
          { { { "Volume Geral: %.0f%%",
                "gSettings.Audio.MasterVolume",
                "Ajusta o volume geral do som.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true } },
              { "Volume da Música Principal: %.0f%%",
                "gSettings.Audio.MainMusicVolume",
                "Ajusta o volume da música de fundo.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    AudioSeq_SetPortVolumeScale(SEQ_PLAYER_BGM_MAIN,
                                                CVarGetFloat("gSettings.Audio.MainMusicVolume", 1.0f));
                } },
              { "Volume da Música Secundária: %.0f%%",
                "gSettings.Audio.SubMusicVolume",
                "Ajusta o volume da música secundária.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    AudioSeq_SetPortVolumeScale(SEQ_PLAYER_BGM_SUB,
                                                CVarGetFloat("gSettings.Audio.SubMusicVolume", 1.0f));
                } },
              { "Volume dos Efeitos Sonoros: %.0f%%",
                "gSettings.Audio.SoundEffectsVolume",
                "Ajusta o volume dos efeitos sonoros.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    AudioSeq_SetPortVolumeScale(SEQ_PLAYER_SFX,
                                                CVarGetFloat("gSettings.Audio.SoundEffectsVolume", 1.0f));
                } },
              { "Volume das Fanfarras: %.0f%%",
                "gSettings.Audio.FanfareVolume",
                "Ajusta o volume das fanfarras.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    AudioSeq_SetPortVolumeScale(SEQ_PLAYER_FANFARE,
                                                CVarGetFloat("gSettings.Audio.FanfareVolume", 1.0f));
                } },
              { "Volume do Ambiente: %.0f%%",
                "gSettings.Audio.AmbienceVolume",
                "Ajusta o volume dos sons de ambiente.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f,
                  .max = 1.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    AudioSeq_SetPortVolumeScale(SEQ_PLAYER_AMBIENCE,
                                                CVarGetFloat("gSettings.Audio.AmbienceVolume", 1.0f));
                } },
              { "API de Áudio", NULL, "Define a API de áudio usada pelo jogo. É preciso reiniciar para aplicar.",
                WIDGET_AUDIO_BACKEND } } } });
    // Graphics Settings
    static int32_t maxFps;
    const char* tooltip = "";
    if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() == Ship::WindowBackend::FAST3D_DXGI_DX11) {
        maxFps = 360;
        tooltip = "Usa interpolação de matrizes para criar quadros extras e deixar a imagem mais fluida. É puramente "
                  "visual e não afeta a lógica do jogo, glitches etc.\n\nUm FPS acima da taxa de atualização da tela "
                  "desperdiça recursos e pode piorar o resultado.";
    } else {
        maxFps = Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();
        tooltip = "Usa interpolação de matrizes para criar quadros extras e deixar a imagem mais fluida. É puramente "
                  "visual e não afeta a lógica do jogo, glitches etc.";
    }
    settingsSidebar.push_back(
        { "Gráficos",
          3,
          { { { "Alternar Tela Cheia",
                "gSettings.Fullscreen",
                "Liga/desliga a tela cheia.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { Ship::Context::GetInstance()->GetWindow()->ToggleFullscreen(); } },
#ifndef __APPLE__
              { "Resolução Interna: %.0f%%",
                CVAR_INTERNAL_RESOLUTION,
                "Multiplica a resolução de saída pelo valor escolhido. É uma forma de anti-aliasing mais pesada, porém "
                "eficaz.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.5f,
                  .max = 2.0f,
                  .defaultVariant = 1.0f,
                  .showButtons = false,
                  .format = "",
                  .isPercentage = true },
                [](widgetInfo& info) {
                    Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(
                        CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1));
                } },
#endif
#ifndef __WIIU__
              { "Anti-aliasing (MSAA): %d",
                CVAR_MSAA_VALUE,
                "Ativa o MSAA (anti-aliasing por multiamostragem) de 2x a 8x para suavizar as bordas dos "
                "modelos.\nMais amostras deixam as bordas mais suaves, mas podem reduzir o desempenho.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 8, 1 },
                [](widgetInfo& info) {
                    Ship::Context::GetInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger(CVAR_MSAA_VALUE, 1));
                } },
#endif

              { "FPS atual: %d",
                "gInterpolationFPS",
                tooltip,
                WIDGET_CVAR_SLIDER_INT,
                { 20, maxFps, 20 },
                [](widgetInfo& info) {
                    int32_t defaultVariant = std::get<int32_t>(info.widgetOptions.defaultVariant);
                    if (CVarGetInteger(info.widgetCVar, defaultVariant) == defaultVariant) {
                        info.widgetName = "FPS atual: Original (%d)";
                    } else {
                        info.widgetName = "FPS atual: %d";
                    }
                },
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_MATCH_REFRESH_RATE_ON).active)
                        info.activeDisables.push_back(DISABLE_FOR_MATCH_REFRESH_RATE_ON);
                } },
              { "Igualar Taxa de Atualização",
                "",
                "Iguala a interpolação à taxa de atualização da janela do jogo.",
                WIDGET_BUTTON,
                {},
                [](widgetInfo& info) {
                    int hz = Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();
                    if (hz >= 20 && hz <= 360) {
                        CVarSetInteger("gInterpolationFPS", hz);
                        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesOnNextTick();
                    }
                },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NOT_DIRECTX).active; } },
              { "Igualar Taxa de Atualização",
                "gMatchRefreshRate",
                "Iguala a interpolação à taxa de atualização da janela do jogo.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DIRECTX).active; } },
              { "Correção de tremulação: >= % d FPS",
                "gExtraLatencyThreshold",
                "Quando o FPS de interpolação atinge este valor, adiciona um quadro de atraso nos comandos (ex.: 16,6 "
                "ms a 60 FPS) para evitar tremulação. Assim a CPU trabalha em um quadro enquanto a GPU termina o "
                "anterior.\nUse quando o aparelho for lento demais para fazer o trabalho de CPU + GPU a tempo.",
                WIDGET_CVAR_SLIDER_INT,
                { 0, 360, 80 },
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NOT_DIRECTX).active; } },
              { "API de Renderização (requer reinício)", NULL, "Define a API de renderização usada pelo jogo.",
                WIDGET_VIDEO_BACKEND },
              { "Ativar VSync",
                CVAR_VSYNC_ENABLED,
                "Ativa o VSync.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NO_VSYNC).active; } },
              { "Tela Cheia em Janela",
                CVAR_SDL_WINDOWED_FULLSCREEN,
                "Ativa o modo tela cheia em janela.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NO_WINDOWED_FULLSCREEN).active; } },
              { "Permitir Várias Janelas",
                CVAR_ENABLE_MULTI_VIEWPORTS,
                "Permite abrir várias janelas ao mesmo tempo. É preciso reiniciar para aplicar.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NO_MULTI_VIEWPORT).active; } },
              { "Filtro de Textura (requer reinício)",
                CVAR_TEXTURE_FILTER,
                "Define o filtro aplicado às texturas.",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = textureFilteringMap } } } } });
    // Input Editor
    settingsSidebar.push_back({ "Editor de Controles",
                                1,
                                { { { "Abrir Editor de Controles em Janela",
                                      "gWindows.BenInputEditor",
                                      "Abre o Editor de Controles em uma janela separada.",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "2S2H Input Editor" } } } } });

    settingsSidebar.push_back(
        { "Notificações",
          1,
          { {
              { "Posição",
                "gNotifications.Position",
                "Em qual canto da tela as notificações aparecem.",
                WIDGET_CVAR_COMBOBOX,
                { .defaultVariant = 3, .comboBoxOptions = notificationPosition } },
              { "Duração: %.0f segundos",
                "gNotifications.Duration",
                "Por quanto tempo as notificações ficam na tela.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 3.0f, .max = 30.0f, .defaultVariant = 10.0f, .format = "%.1f", .step = 0.1f } },
              { "Opacidade do Fundo: %.0f%%",
                "gNotifications.BgOpacity",
                "Quão opaco é o fundo das notificações.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.0f, .max = 1.0f, .defaultVariant = 0.5f, .format = "%.0f%%", .isPercentage = true } },
              { "Tamanho %.1f",
                "gNotifications.Size",
                "Tamanho das notificações.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 1.0f, .max = 5.0f, .defaultVariant = 1.8f, .format = "%.1f", .step = 0.1f } },
              { "Testar Notificação",
                "",
                "Mostra uma notificação de teste.",
                WIDGET_BUTTON,
                {},
                [](widgetInfo& info) {
                    Notification::Emit({
                        .itemIcon = "__OTR__icon_item_24_static_yar/gQuestIconGoldSkulltulaTex",
                        .prefix = "Isto",
                        .message = "é um",
                        .suffix = "teste.",
                    });
                } },
          } } });

    settingsSidebar.push_back(
        { "Idioma", 1, { { { "Idioma do texto do jogo", "", "", WIDGET_CUSTOM, {}, [](widgetInfo& info) {
                                DrawLanguageOptions();
                            } } } } });
    settingsSidebar.push_back(
        { "Mods", 1, { { { "Mods e pacotes de texturas", "", "", WIDGET_CUSTOM, {}, [](widgetInfo& info) {
                              DrawModsOptions();
                          } } } } });

    if (CVarGetInteger("gSettings.SidebarSearch", 0)) {
        settingsSidebar.insert(settingsSidebar.begin() + searchSidebarIndex, searchSidebarEntry);
    }
}
int32_t motionBlurStrength;

void AddEnhancements() {
    // Camera Snap Fix
    enhancementsSidebar.push_back(
        { "Câmera",
          3,
          { {
                { .widgetName = "Correções", .widgetType = WIDGET_SEPARATOR_TEXT },
                { "Corrigir Salto da Câmera ao Mirar",
                  "gEnhancements.Camera.FixTargettingCameraSnap",
                  "Corrige o salto da câmera que acontece quando você se move e aperta o botão de mirar.",
                  WIDGET_CVAR_CHECKBOX,
                  {} },
                { .widgetName = "Primeira Pessoa", .widgetType = WIDGET_SEPARATOR_TEXT },
                { "Desativar Centralização Automática",
                  "gEnhancements.Camera.FirstPerson.DisableFirstPersonAutoCenterView",
                  "Desativa a centralização automática da câmera em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr,
                  [](widgetInfo& info) {
                      if (disabledMap.at(DISABLE_FOR_GYRO_ON).active)
                          info.activeDisables.push_back(DISABLE_FOR_GYRO_ON);
                  } },
                { "Inverter Eixo X", "gEnhancements.Camera.FirstPerson.InvertX",
                  "Inverte o eixo X da câmera em primeira pessoa.", WIDGET_CVAR_CHECKBOX },
                { "Inverter Eixo Y",
                  "gEnhancements.Camera.FirstPerson.InvertY",
                  "Inverte o eixo Y da câmera em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  { .defaultVariant = true } },
                { "Sensibilidade do Eixo X: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.SensitivityX",
                  "Ajusta a sensibilidade do eixo X em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr },
                { "Sensibilidade do Eixo Y: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.SensitivityY",
                  "Ajusta a sensibilidade do eixo Y em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr },
                { "Mira por Giroscópio",
                  "gEnhancements.Camera.FirstPerson.GyroEnabled",
                  "Ativa a mira por giroscópio em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr },
                { "Inverter Eixo X do Giroscópio",
                  "gEnhancements.Camera.FirstPerson.GyroInvertX",
                  "Inverte o eixo X do giroscópio em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_GYRO_OFF).active; } },
                { "Inverter Eixo Y do Giroscópio",
                  "gEnhancements.Camera.FirstPerson.GyroInvertY",
                  "Inverte o eixo Y do giroscópio em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_GYRO_OFF).active; } },
                { "Sensibilidade X do Giroscópio: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.GyroSensitivityX",
                  "Ajusta a sensibilidade do eixo X do giroscópio em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_GYRO_OFF).active; } },
                { "Sensibilidade Y do Giroscópio: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.GyroSensitivityY",
                  "Ajusta a sensibilidade do eixo Y do giroscópio em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_GYRO_OFF).active; } },
                { "Mira pelo Analógico Direito", "gEnhancements.Camera.FirstPerson.RightStickEnabled",
                  "Ativa a mira pelo analógico direito em primeira pessoa.", WIDGET_CVAR_CHECKBOX },
                { "Mover enquanto mira",
                  "gEnhancements.Camera.FirstPerson.MoveInFirstPerson",
                  "Permite andar com o analógico esquerdo em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr,
                  [](widgetInfo& info) {
                      if (disabledMap.at(DISABLE_FOR_RIGHT_STICK_OFF).active)
                          info.activeDisables.push_back(DISABLE_FOR_RIGHT_STICK_OFF);
                  } },
                { "Inverter Eixo X do Analógico Direito",
                  "gEnhancements.Camera.FirstPerson.RightStickInvertX",
                  "Inverte o eixo X do analógico direito em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  {},
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_RIGHT_STICK_OFF).active; } },
                { "Inverter Eixo Y do Analógico Direito",
                  "gEnhancements.Camera.FirstPerson.RightStickInvertY",
                  "Inverte o eixo Y do analógico direito em primeira pessoa.",
                  WIDGET_CVAR_CHECKBOX,
                  { .defaultVariant = true },
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_RIGHT_STICK_OFF).active; } },
                { "Sensibilidade X do Analógico Direito: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.RightStickSensitivityX",
                  "Ajusta a sensibilidade do eixo X do analógico direito em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_RIGHT_STICK_OFF).active; } },
                { "Sensibilidade Y do Analógico Direito: %.0f%%",
                  "gEnhancements.Camera.FirstPerson.RightStickSensitivityY",
                  "Ajusta a sensibilidade do eixo Y do analógico direito em primeira pessoa.",
                  WIDGET_CVAR_SLIDER_FLOAT,
                  { .min = 0.01f, .max = 2.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                  nullptr,
                  [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_RIGHT_STICK_OFF).active; } },
            },
            // Camera Enhancements
            { { .widgetName = "Câmeras", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Câmera Livre",
                "gEnhancements.Camera.FreeLook.Enable",
                "Ativa o controle livre da câmera.\nObs.: tire os botões C do analógico direito nas configurações do "
                "controle e mapeie a câmera no analógico direito.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterCameraFreeLook(); },
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_DEBUG_CAM_ON).active)
                        info.activeDisables.push_back(DISABLE_FOR_DEBUG_CAM_ON);
                } },
              { "Distância da Câmera: %d",
                "gEnhancements.Camera.FreeLook.MaxCameraDistance",
                "Distância máxima da Câmera Livre.",
                WIDGET_CVAR_SLIDER_INT,
                { 100, 900, 185 },
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_FREE_LOOK_OFF).active; } },
              { "Velocidade de Transição da Câmera: %d",
                "gEnhancements.Camera.FreeLook.TransitionSpeed",
                "Alguém pode me ajudar?",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 900, 25 },
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_FREE_LOOK_OFF).active; } },
              { "Ângulo Máximo de Altura: %.0f\xC2\xB0",
                "gEnhancements.Camera.FreeLook.MaxPitch",
                "Altura máxima da câmera.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = -89.0f, .max = 89.0f, .defaultVariant = 72.0f, .format = "%.0f\xC2\xB0" },
                [](widgetInfo& info) { FreeLookPitchMinMax(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_FREE_LOOK_OFF).active; } },
              { "Ângulo Mínimo de Altura: %.0f\xC2\xB0",
                "gEnhancements.Camera.FreeLook.MinPitch",
                "Altura mínima da câmera.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = -89.0f, .max = 89.0f, .defaultVariant = -49.0f, .format = "%.0f\xC2\xB0" },
                [](widgetInfo& info) { FreeLookPitchMinMax(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_FREE_LOOK_OFF).active; } },
              { "Câmera de Depuração",
                "gEnhancements.Camera.DebugCam.Enable",
                "Ativa o controle livre da câmera.",
                WIDGET_CVAR_CHECKBOX,
                {},
                ([](widgetInfo& info) { RegisterDebugCam(); }),
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_FREE_LOOK_ON).active) {
                        info.activeDisables.push_back(DISABLE_FOR_FREE_LOOK_ON);
                    }
                } },
              { "Inverter Eixo X da Câmera",
                "gEnhancements.Camera.RightStick.InvertXAxis",
                "Inverte o eixo X da câmera",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_CAMERAS_OFF).active) {
                        info.activeDisables.push_back(DISABLE_FOR_CAMERAS_OFF);
                    }
                } },
              { "Inverter Eixo Y da Câmera",
                "gEnhancements.Camera.RightStick.InvertYAxis",
                "Inverte o eixo Y da câmera",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_CAMERAS_OFF).active) {
                        info.activeDisables.push_back(DISABLE_FOR_CAMERAS_OFF);
                    }
                } },
              { "Sensibilidade Horizontal em Terceira Pessoa: %.0f",
                "gEnhancements.Camera.RightStick.CameraSensitivity.X",
                "Ajusta a sensibilidade do eixo X em terceira pessoa.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.01f, .max = 5.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                nullptr,
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_CAMERAS_OFF).active) {
                        info.activeDisables.push_back(DISABLE_FOR_CAMERAS_OFF);
                    }
                } },
              { "Sensibilidade Vertical em Terceira Pessoa: %.0f",
                "gEnhancements.Camera.RightStick.CameraSensitivity.Y",
                "Ajusta a sensibilidade do eixo X em terceira pessoa.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.01f, .max = 5.0f, .defaultVariant = 1.0f, .format = "%.0f%%", .isPercentage = true },
                nullptr,
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_CAMERAS_OFF).active) {
                        info.activeDisables.push_back(DISABLE_FOR_CAMERAS_OFF);
                    }
                } },
              { "Ativar Rolagem (6 graus de liberdade)",
                "gEnhancements.Camera.DebugCam.6DOF",
                "Permite os seis graus de movimento da câmera. Obs.: a guinada funciona diferente aqui, girando em "
                "torno do ponto focal em vez de um eixo polar.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_CAM_OFF).active; } },
              { "Velocidade da Câmera: %.0f",
                "gEnhancements.Camera.DebugCam.CameraSpeed",
                "Ajusta a velocidade da câmera.",
                WIDGET_CVAR_SLIDER_FLOAT,
                { .min = 0.1f, .max = 3.0f, .defaultVariant = 0.5f, .format = "%.0f%%", .isPercentage = true },
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_CAM_OFF).active; } } } } });
    // Cheats
    enhancementsSidebar.push_back(
        { "Trapaças",
          3,
          { { { "Vida Infinita", "gCheats.InfiniteHealth", "Corações sempre cheios.", WIDGET_CVAR_CHECKBOX, {} },
              { "Magia Infinita", "gCheats.InfiniteMagic", "Magia sempre cheia.", WIDGET_CVAR_CHECKBOX, {} },
              { "Rupias Infinitas", "gCheats.InfiniteRupees", "Carteira sempre cheia.", WIDGET_CVAR_CHECKBOX, {} },
              { "Consumíveis Infinitos", "gCheats.InfiniteConsumables",
                "Consumíveis sempre no máximo. É preciso já ter coletado cada consumível.", WIDGET_CVAR_CHECKBOX },
              { "Planar Mais com a Flor Deku",
                "gCheats.LongerFlowerGlide",
                "Deku Link plana por mais tempo, sem cair depois de certa distância.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterLongerFlowerGlide(); } },
              { "Atravessar Paredes", "gCheats.NoClip", "Link atravessa paredes e objetos.", WIDGET_CVAR_CHECKBOX },
              { "Razor Sword Inquebrável", "gCheats.UnbreakableRazorSword",
                "A Razor Sword pode ser usada sem perder o fio.", WIDGET_CVAR_CHECKBOX },
              { "Itens sem Restrição", "gCheats.UnrestrictedItems", "Todas as formas podem usar todos os itens.",
                WIDGET_CVAR_CHECKBOX },
              { "Pulo Lunar no L",
                "gCheats.MoonJumpOnL",
                "Segurar L faz Link flutuar.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterMoonJumpOnL(); } },
              { "Elegy of Emptiness em Qualquer Lugar", "gCheats.ElegyAnywhere",
                "Permite tocar a Elegy of Emptiness fora de Ikana", WIDGET_CVAR_CHECKBOX },
              { "Parar o Tempo nas Masmorras",
                "gCheats.TempleTimeStop",
                "Impede o tempo de passar nas áreas escolhidas. Mude de sala para aplicar.\n\n- Desligado: "
                "comportamento original.\n- Templos: para o tempo nos templos de Woodfall, Snowhead, Great Bay e Stone "
                "Tower.\n- Templos + Mini Masmorras: além dos templos, para o tempo nas duas Spider Houses, Pirates' "
                "Fortress, Beneath the Well, Ancient Castle of Ikana e Secret Shrine.",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = timeStopOptions } } } } });
    // Gameplay Enhancements
    enhancementsSidebar.push_back(
        { "Jogabilidade",
          3,
          { { { .widgetName = "Jogador", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Lançamento Rápido da Flor Deku",
                "gEnhancements.Player.FastFlowerLaunch",
                "Reduz o tempo para atingir a altura máxima ao sair de uma flor Deku",
                WIDGET_CVAR_CHECKBOX,
                {},
                ([](widgetInfo& info) { RegisterFastFlowerLaunch(); }) },
              { "Guardar Instantâneo", "gEnhancements.Player.InstantPutaway",
                "Link guarda o item na mão na hora, sem esperar.", WIDGET_CVAR_CHECKBOX },
              { "Fierce Deity Guarda a Espada", "gEnhancements.Player.FierceDeityPutaway",
                "Permite que o Fierce Deity Link guarde a espada.", WIDGET_CVAR_CHECKBOX },
              { "Velocidade de Escalada",
                "gEnhancements.Player.ClimbSpeed",
                "Aumenta a velocidade com que Link sobe trepadeiras e escadas.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 5, 1 } },
              { "Equipar no D-pad", "gEnhancements.Dpad.DpadEquips", "Permite equipar itens no D-pad",
                WIDGET_CVAR_CHECKBOX },
              { "Sempre Vencer a Corrida de Cães",
                "gEnhancements.Minigames.AlwaysWinDoggyRace",
                "Deixa a Corrida de Cães mais fácil de vencer.",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = alwaysWinDoggyraceOptions } },
              { "Prêmio da Corrida do Leite",
                "gEnhancements.Minigames.CremiaHugs",
                "Escolha o prêmio ao vencer a escolta do leite depois da primeira vez.\n-Original: prêmio "
                "aleatório\n-Abraço: cena do abraço\n-Rupias: prêmio em rupias",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = cremiaRewardOptions } },
              { "Pontuação para Vencer na Escola de Espadachim",
                "gEnhancements.Minigames.SwordsmanSchoolScore",
                "Define a pontuação necessária para vencer na Escola de Espadachim.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 30, 30 } },
              { "Equipar Flechas Mágicas Rápido", "gEnhancements.Equipment.MagicArrowEquipSpeed",
                "Remove a animação de equipar as flechas mágicas.", WIDGET_CVAR_CHECKBOX },
              { "Retorno Instantâneo das Barbatanas", "gEnhancements.PlayerActions.InstantRecall",
                "Apertar B faz as barbatanas-bumerangue voltarem na hora para o Zora Link.", WIDGET_CVAR_CHECKBOX },
              { "Giro com Espada de Duas Mãos", "gEnhancements.Equipment.TwoHandedSwordSpinAttack",
                "Ativa o giro mágico com a espada do Fierce Deity e a Great Fairy's Sword.", WIDGET_CVAR_CHECKBOX } },
            { { .widgetName = "Modos", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Jogar como Kafei", "gModes.PlayAsKafei", "Recarregue a cena para aplicar.", WIDGET_CVAR_CHECKBOX },
              { "O Tempo Anda Quando Você Anda",
                "gModes.TimeMovesWhenYouMove",
                "O tempo só passa quando Link não está parado.",
                WIDGET_CVAR_CHECKBOX,
                {},
                ([](widgetInfo& info) { RegisterTimeMovesWhenYouMove(); }) },
              { "Mundo Espelhado",
                "gModes.MirroredWorld.Mode",
                "Espelha o mundo na horizontal.",
                WIDGET_CVAR_CHECKBOX,
                {},
                ([](widgetInfo& info) {
                    if (CVarGetInteger("gModes.MirroredWorld.Mode", 0)) {
                        CVarSetInteger("gModes.MirroredWorld.State", 1);
                    } else {
                        CVarClear("gModes.MirroredWorld.State");
                    }
                }) } },
            { { .widgetName = "Salvamento", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Save da Coruja Permanente", "gEnhancements.Saving.PersistentOwlSaves",
                "Continuar um save não apaga o save da coruja. Tocar a Song of Time, deixar a lua cair ou zerar o jogo "
                "apaga o save da coruja e vira o novo último save.",
                WIDGET_CVAR_CHECKBOX },
              { "Salvar no Menu de Pausa", "gEnhancements.Saving.PauseSave",
                "Traz de volta o salvamento pelo menu de pausa. Apertar B no menu de pausa oferece criar um save da "
                "coruja permanente no local atual.\n\nAo carregar, você volta na entrada da masmorra onde salvou ou em "
                "South Clock Town.",
                WIDGET_CVAR_CHECKBOX },
              { "Salvamento Automático",
                "gEnhancements.Saving.Autosave",
                "Cria automaticamente um save da coruja permanente no intervalo escolhido.\n\nAo carregar, você volta "
                "na entrada da masmorra onde salvou ou em South Clock Town.",
                WIDGET_CVAR_CHECKBOX,
                {},
                ([](widgetInfo& info) { RegisterAutosave(); }) },
              { "Intervalo do Salvamento Automático: %d minutos",
                "gEnhancements.Saving.AutosaveInterval",
                "Define o intervalo entre os salvamentos automáticos.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 60, 5 },
                nullptr,
                [](widgetInfo& info) {
                    if (disabledMap.at(DISABLE_FOR_AUTO_SAVE_OFF).active) {
                        info.activeDisables.push_back(DISABLE_FOR_AUTO_SAVE_OFF);
                    }
                } },
              { .widgetName = "Ciclo de Tempo", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Não Esvaziar as Garrafas", "gEnhancements.Cycle.DoNotResetBottleContent",
                "Tocar a Song of Time não esvazia as garrafas.", WIDGET_CVAR_CHECKBOX },
              { "Não Zerar Consumíveis", "gEnhancements.Cycle.DoNotResetConsumables",
                "Tocar a Song of Time não zera os consumíveis.", WIDGET_CVAR_CHECKBOX },
              { "Não Perder a Razor Sword", "gEnhancements.Cycle.DoNotResetRazorSword",
                "Tocar a Song of Time não volta a espada para a Kokiri Sword.", WIDGET_CVAR_CHECKBOX },
              { "Não Zerar Rupias", "gEnhancements.Cycle.DoNotResetRupees",
                "Tocar a Song of Time não zera suas rupias.", WIDGET_CVAR_CHECKBOX },
              { "Não Resetar a Velocidade do Tempo", "gEnhancements.Cycle.DoNotResetTimeSpeed",
                "Tocar a Song of Time não desfaz a velocidade do tempo definida pela Inverted Song of Time.",
                WIDGET_CVAR_CHECKBOX },
              {
                  .widgetName = "Manter a Express Mail",
                  .widgetCVar = "gEnhancements.Cycle.KeepExpressMail",
                  .widgetTooltip = "Mantém a Express Mail no inventário depois da primeira entrega, para fazer as duas "
                                   "entregas no mesmo ciclo",
                  .widgetType = WIDGET_CVAR_CHECKBOX,
                  .widgetCallback = [](widgetInfo& info) { RegisterKeepExpressMail(); },
              },
              { .widgetName = "Instável",
                .widgetType = WIDGET_SEPARATOR_TEXT,
                .widgetOptions = { .color = UIWidgets::Colors::Yellow } },
              { "Remover Espera ao Salvar", "gEnhancements.Saving.DisableSaveDelay",
                "Remove a espera de 2 segundos ao salvar do jogo original. Pode causar problemas no glitch do Dia 0",
                WIDGET_CVAR_CHECKBOX } } } });
    // Graphics Enhancements
    enhancementsSidebar.push_back(
        { "Gráficos",
          3,
          { {
              { .widgetName = "Relógio", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Tipo de Relógio",
                "gEnhancements.Graphics.ClockType",
                "Alterna entre o relógio gráfico e o relógio só em texto.",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = clockTypeOptions } },
              { "Relógio de 24 Horas", "gEnhancements.Graphics.24HoursClock", "Muda o relógio de 12 para 24 horas",
                WIDGET_CVAR_CHECKBOX },
              { .widgetName = "Mods", .widgetType = WIDGET_SEPARATOR_TEXT },
              { .widgetName = "Usar Assets Alternativos",
                .widgetCVar = "gEnhancements.Mods.AlternateAssets",
                .widgetTooltip = "Alterna entre os assets padrão e os alternativos. Os mods costumam dizer se é "
                                 "preciso usar esta opção.",
                .widgetType = WIDGET_CVAR_CHECKBOX },
              { .widgetName = "Desfoque de Movimento", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Modo do Desfoque de Movimento",
                "gEnhancements.Graphics.MotionBlur.Mode",
                "Escolhe o modo do desfoque de movimento.",
                WIDGET_CVAR_COMBOBOX,
                { .comboBoxOptions = motionBlurOptions, .labelPosition = UIWidgets::LabelPosition::None } },
              { "Interpolar",
                "gEnhancements.Graphics.MotionBlur.Interpolate",
                "Captura o desfoque também nos quadros interpolados, não só nos quadros do jogo.\nIsso reduz bastante "
                "a força do desfoque, mas suaviza os rastros.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_MOTION_BLUR_MODE).value == 1; } },
              { "Ligado/Desligado",
                "",
                "Ativa o desfoque de movimento.",
                WIDGET_CHECKBOX,
                { .valuePointer = (bool*)&R_MOTION_BLUR_ENABLED },
                nullptr,
                [](widgetInfo& info) {
                    info.widgetOptions.valuePointer = (bool*)&R_MOTION_BLUR_ENABLED;
                    info.isHidden = disabledMap.at(DISABLE_FOR_MOTION_BLUR_MODE).value != 0;
                } },
              { "Intensidade",
                "gEnhancements.Graphics.MotionBlur.Strength",
                "Intensidade do desfoque de movimento.",
                WIDGET_CVAR_SLIDER_INT,
                { 0, 255, 180 },
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_MOTION_BLUR_MODE).value != 2; } },
              { "Intensidade",
                "",
                "Intensidade do desfoque de movimento.",
                WIDGET_SLIDER_INT,
                { 0, 255, 180, {}, &motionBlurStrength },
                [](widgetInfo& info) { R_MOTION_BLUR_ALPHA = motionBlurStrength; },
                [](widgetInfo& info) {
                    motionBlurStrength = R_MOTION_BLUR_ALPHA;
                    info.isHidden = disabledMap.at(DISABLE_FOR_MOTION_BLUR_MODE).value != 0 ||
                                    disabledMap.at(DISABLE_FOR_MOTION_BLUR_OFF).active;
                } },
              { .widgetName = "Outros", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Itens Soltos em 3D",
                "gEnhancements.Graphics.3DItemDrops",
                "Deixa em 3D os itens que caem no chão",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { Register3DItemDrops(); } },
              { "Logo Autêntico", "gEnhancements.Graphics.AuthenticLogo",
                "Esconde a versão e os detalhes da build e mostra o modelo e a textura originais no logo da tela "
                "inicial",
                WIDGET_CVAR_CHECKBOX },
              { "Mira do Arco", "gEnhancements.Graphics.BowReticle", "Mostra uma mira ao puxar uma flecha.",
                WIDGET_CVAR_CHECKBOX },
              { "Cel Shading (Visual de Anime)", "gEnhancements.Graphics.CelShading",
                "Visual de anime estilo DBZ Budokai: a luz do mundo 3D vira poucos tons chapados de sombra, as "
                "cores ficam mais vivas e tudo ganha contorno preto. As texturas e o HUD continuam iguais.",
                WIDGET_CVAR_CHECKBOX },
              { "Tons de Sombra: %d",
                "gEnhancements.Graphics.CelShadingBands",
                "2 = luz e sombra bem marcadas, como no Budokai; mais tons = mais suave.",
                WIDGET_CVAR_SLIDER_INT,
                { 2, 6, 2 } },
              { "Espessura do Contorno: %d",
                "gEnhancements.Graphics.CelOutlineThickness",
                "Largura das linhas pretas do cel shading.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 4, 2 } },
              { "Remover Barras Pretas", "gEnhancements.Graphics.DisableBlackBars",
                "Remove as barras pretas nas cenas e ao mirar com Z.\nObs.: podem aparecer pequenas falhas visuais que "
                "as barras escondiam.\nDesative esta opção antes de relatar um bug.",
                WIDGET_CVAR_CHECKBOX },
              { .widgetName = "Instável",
                .widgetType = WIDGET_SEPARATOR_TEXT,
                .widgetOptions = { .color = UIWidgets::Colors::Yellow } },
              { "Desativar Limite de Distância do Cenário",
                "gEnhancements.Graphics.DisableSceneGeometryDistanceCheck",
                "Desenha o cenário a qualquer distância do jogador. Pode ter efeitos colaterais.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { GfxPatcher_ApplyGeometryIssuePatches(); } },
              { "Corte de Objetos para Widescreen", "gEnhancements.Graphics.ActorCullingAccountsForWidescreen",
                "Ajusta o corte de objetos para telas widescreen. Pode ter efeitos colaterais.", WIDGET_CVAR_CHECKBOX },
              {
                  "Aumentar Distância de Desenho: %dx",
                  "gEnhancements.Graphics.IncreaseActorDrawDistance",
                  "Aumenta a distância em que personagens e objetos aparecem. Pode ter efeitos colaterais.",
                  WIDGET_CVAR_SLIDER_INT,
                  { 1, 5, 1 },
              },
          } } });
    enhancementsSidebar.push_back(
        { "Itens/Músicas",
          3,
          { // Mask Enhancements
            { { .widgetName = "Máscaras", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Blast Mask com Força de Powder Keg", "gEnhancements.Masks.BlastMaskKeg",
                "A Blast Mask também destrói o que só o Powder Keg destrói.", WIDGET_CVAR_CHECKBOX },
              { "Transformação Rápida", "gEnhancements.Masks.FastTransformation",
                "Remove a demora ao usar as máscaras de transformação.", WIDGET_CVAR_CHECKBOX },
              { "Fierce Deity's Mask em Qualquer Lugar", "gEnhancements.Masks.FierceDeitysAnywhere",
                "Permite usar a Fierce Deity's Mask fora das salas de chefe.", WIDGET_CVAR_CHECKBOX },
              { "Bunny Hood Permanente",
                "gEnhancements.Masks.PersistentBunnyHood.Enabled",
                "Liga/desliga para sempre o bônus de velocidade da Bunny Hood apertando A nela no menu de máscaras.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { UpdatePersistentMasksState(); } },
              { "Blast Mask sem Espera", "gEnhancements.Masks.NoBlastMaskCooldown",
                "Remove a espera entre usos da Blast Mask.", WIDGET_CVAR_CHECKBOX } },
            // Song Enhancements
            { { .widgetName = "Ocarina", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Ativar Sun's Song", "gEnhancements.Songs.EnableSunsSong",
                "Ativa a Sun's Song, que ficou incompleta no jogo. Toque DIREITA-BAIXO-CIMA-DIREITA-BAIXO-CIMA. O "
                "tempo passa muito rápido até Link mudar de cena ou mudar o período do dia.",
                WIDGET_CVAR_CHECKBOX },
              { "Ocarina no D-pad", "gEnhancements.Playback.DpadOcarina", "Permite tocar a ocarina com o D-pad.",
                WIDGET_CVAR_CHECKBOX },
              { "Teletransporte pelo Menu de Pausa", "gEnhancements.Songs.PauseOwlWarp",
                "Permite viajar para as estátuas de coruja ativadas pelo mapa do menu de pausa. Exige poder tocar a "
                "Song of Soaring normalmente.\n\nFunciona com o Index-Warp ativo, mostrando todos os destinos válidos. "
                "O destino Great Bay Coast sempre aparece para o índice 0.",
                WIDGET_CVAR_CHECKBOX },
              { "Escolher a Hora na Song of Double Time",
                "gEnhancements.Songs.DoubleTimeHourSelect",
                "Como no Majora's Mask 3D, escolha a hora para onde a Song of Double Time te leva. Use "
                "esquerda/direita no analógico ou D-pad enquanto a confirmação aparece. Dá para escolher até o próximo "
                "amanhecer ou anoitecer.",
                WIDGET_CVAR_CHECKBOX,
                { .defaultVariant = true } },
              { "Ovos Zora para a Bossa Nova",
                "gEnhancements.Songs.ZoraEggCount",
                "Quantos ovos são necessários para liberar a New Wave Bossa Nova.",
                WIDGET_CVAR_SLIDER_INT,
                { 1, 7, 7 } },
              { "Não Perder Notas da Ocarina", "gEnhancements.Playback.NoDropOcarinaInput",
                "Evita perder notas ao tocar a ocarina rápido.", WIDGET_CVAR_CHECKBOX },
              { "Pular a Música do Espantalho", "gEnhancements.Playback.SkipScarecrowSong",
                "Pierre aparece quando você pega a ocarina.", WIDGET_CVAR_CHECKBOX },
              { "Músicas Mais Rápidas",
                "gEnhancements.Songs.FasterSongPlayback",
                "Acelera a reprodução das músicas.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterFasterSongPlayback(); } } } } });
    enhancementsSidebar.push_back(
        { "Economia de Tempo",
          3,
          { // Cutscene Skips
            { { .widgetName = "Cenas", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Esconder Títulos das Áreas", "gEnhancements.Cutscenes.HideTitleCards",
                "Esconde o nome da área ao entrar nela.", WIDGET_CVAR_CHECKBOX },
              { "Pular Cenas de Entrada", "gEnhancements.Cutscenes.SkipEntranceCutscenes",
                "Pula as cenas da primeira entrada em uma área nova.", WIDGET_CVAR_CHECKBOX },
              { "Ir Direto para a Seleção de Save", "gEnhancements.Cutscenes.SkipToFileSelect",
                "Pula a abertura e vai direto para a seleção de save ao iniciar.", WIDGET_CVAR_CHECKBOX },
              { "Pular Introdução", "gEnhancements.Cutscenes.SkipIntroSequence",
                "Ao começar um jogo, você vai direto para South Clock Town como Deku Link.", WIDGET_CVAR_CHECKBOX },
              { "Pular o Primeiro Ciclo",
                "gEnhancements.Cutscenes.SkipFirstCycle",
                "Ao começar um jogo, você vai direto para South Clock Town como Link humano, com a Deku Mask, a "
                "ocarina, a Song of Time e a Song of Healing.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_INTRO_SKIP_OFF).active; } },
              { "Pular Cenas da História", "gEnhancements.Cutscenes.SkipStoryCutscenes",
                "Aviso: ainda faz pouca coisa. Mais cenas serão puladas com o tempo.", WIDGET_CVAR_CHECKBOX },
              { "Pular Interações Diversas", "gEnhancements.Cutscenes.SkipMiscInteractions",
                "Aviso: ainda faz pouca coisa. Mais cenas serão puladas com o tempo.", WIDGET_CVAR_CHECKBOX },
              { "Segurar B para Acelerar Cenas", "gEnhancements.Cutscenes.HoldBToFastForward",
                "Durante uma cena, segure B por meio segundo para ela passar bem mais rápido. A cena roda "
                "inteira, só que acelerada, então nada do jogo é pulado. Segurar B também passa os "
                "diálogos sozinho (menos as perguntas, que continuam esperando o A).",
                WIDGET_CVAR_CHECKBOX },
              { "Velocidade das Cenas Aceleradas: %dx",
                "gEnhancements.Cutscenes.FastForwardSpeed",
                "Quantas vezes mais rápido a cena passa enquanto B está segurado.",
                WIDGET_CVAR_SLIDER_INT,
                { 2, 8, 5 } } },
            // Dialogue Enhancements
            { { .widgetName = "Diálogos", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Seleção Rápida no Banco", "gEnhancements.Dialogue.FastBankSelection",
                "Com o diálogo de depósito/saque aberto, Z coloca todas as rupias de Link e R coloca 0.",
                WIDGET_CVAR_CHECKBOX },
              { "Texto Rápido", "gEnhancements.Dialogue.FastText",
                "Acelera o texto e permite segurar B para passar as mensagens.", WIDGET_CVAR_CHECKBOX } } } });
    enhancementsSidebar.push_back(
        { "Correções",
          3,
          { // Fixes
            { { .widgetName = "Correções", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Corrigir Cor da Munição", "gFixes.FixAmmoCountEnvColor",
                "Corrige a cor errada da contagem de munição antes de obter a magia e em outros casos.",
                WIDGET_CVAR_CHECKBOX },
              { "Corrigir Movimento do Fierce Deity ao Mirar", "gEnhancements.Fixes.FierceDeityZTargetMovement",
                "Corrige o movimento travado do Fierce Deity ao mirar com Z", WIDGET_CVAR_CHECKBOX },
              { "Corrigir Travamento do HESS e Weirdshot", "gEnhancements.Fixes.HessCrash",
                "Corrige um travamento que pode acontecer ao fazer HESS ou Weirdshot.", WIDGET_CVAR_CHECKBOX },
              { "Corrigir Caracteres de Controle do Texto", "gEnhancements.Fixes.ControlCharacters",
                "Corrige caracteres de controle que falham dependendo da posição no texto.", WIDGET_CVAR_CHECKBOX },
              { "Corrigir Cor da Fonte da Great Fairy de Ikana", "gFixes.FixIkanaGreatFairyFountainColor",
                "Corrige a fonte da Great Fairy de Ikana que aparece verde em vez de amarela. Isso foi corrigido na "
                "versão europeia",
                WIDGET_CVAR_CHECKBOX },
              { .widgetName = "Corrigir Texturas Fora dos Limites",
                .widgetCVar = "gEnhancements.Fixes.FixTexturesOOB",
                .widgetTooltip = "Corrige texturas que estouram o tamanho, usando o tamanho ou formato certo",
                .widgetType = WIDGET_CVAR_CHECKBOX,
                .widgetOptions = { .defaultVariant = true },
                .widgetCallback = [](widgetInfo& info) { GfxPatcher_ApplyOverflowTexturePatches(); } } } } });
    enhancementsSidebar.push_back(
        { "Restaurações",
          3,
          { // Restorations
            { { .widgetName = "Restaurações", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Mortais e Pulos Laterais com Distância Fixa", "gEnhancements.Restorations.ConstantFlipsHops",
                "Mortais para trás e pulos laterais percorrem sempre a mesma distância, como no OoT.",
                WIDGET_CVAR_CHECKBOX },
              { "Golpe Agachado Forte", "gEnhancements.Restorations.PowerCrouchStab",
                "O golpe agachado usa a força do último ataque de Link, como no MM JP 1.0 e no OoT.",
                WIDGET_CVAR_CHECKBOX },
              { "Rolamento Lateral", "gEnhancements.Restorations.SideRoll", "Traz de volta o rolamento lateral do OoT.",
                WIDGET_CVAR_CHECKBOX },
              { "ISG com a Tatl", "gEnhancements.Restorations.TatlISG",
                "Traz de volta o ISG da Navi do OoT, agora com a Tatl.", WIDGET_CVAR_CHECKBOX },
              { "Aparência da Montanha de Woodfall",
                "gEnhancements.Restorations.WoodfallMountainAppearance",
                "Depois de terminar o Woodfall Temple, a montanha de Woodfall deixa de parecer envenenada vista de "
                "Termina Field\n\nRecarregue a cena para aplicar",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterWoodfallMountainAppearance(); } },
              { .widgetName = "Majora's Mask 3D", .widgetType = WIDGET_SEPARATOR_TEXT },
              { "Bombers' Notebook desde o Início",
                "gEnhancements.MM3D.EarlyBombersNotebook",
                "Como no Majora's Mask 3D, receba o Bombers' Notebook na primeira visita a Clock Town, sem precisar do "
                "esconde-esconde dos Bombers.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterEarlyBombersNotebook(); } },
              { "Variações da Song of Time na Pausa",
                "gEnhancements.MM3D.TimeSongVariantsInPause",
                "Como no Majora's Mask 3D, a Song of Time da tela de Status mostra também a Inverted Song of Time, "
                "a Song of Double Time e a música do espantalho. Com o cursor na Song of Time, aperte L ou C-Baixo "
                "para alternar e A para ouvir as notas.",
                WIDGET_CVAR_CHECKBOX,
                { .defaultVariant = true } },
              { "Guia do Bombers' Notebook Junto com o Caderno", "gEnhancements.MM3D.BombersNotebookGuide",
                "Ao abrir o Bombers' Notebook, abre também um guia com o prêmio de cada pessoa, o dia e a hora certos, "
                "o lugar e a máscara ou condição necessária. A pessoa selecionada no Caderno fica destacada.",
                WIDGET_CVAR_CHECKBOX },
              { "Abrir Guia do Bombers' Notebook",
                "gWindows.BombersNotebookGuide",
                "Abre o guia do Bombers' Notebook em uma janela",
                WIDGET_WINDOW_BUTTON,
                { .size = UIWidgets::Sizes::Inline, .windowName = "Bombers Notebook Guide" } },
              { "Iluminação Estilo 3D", "gEnhancements.MM3D.Lighting.Enabled",
                "Deixa as cenas mais claras e com bem menos neblina, perto do visual do Majora's Mask 3D. As mudanças "
                "de "
                "luz do dia e da noite continuam funcionando.",
                WIDGET_CVAR_CHECKBOX },
              { "Redução da Neblina: %d%%",
                "gEnhancements.MM3D.Lighting.FogReduction",
                "Quanto a neblina começa mais longe. 0 = igual ao N64, 100 = quase sem neblina.",
                WIDGET_CVAR_SLIDER_INT,
                { 0, 100, 50 } },
              { "Luz Ambiente Extra: %d",
                "gEnhancements.MM3D.Lighting.AmbientBoost",
                "Clareia as áreas de sombra. 0 = igual ao N64.",
                WIDGET_CVAR_SLIDER_INT,
                { 0, 100, 20 } },
              { "Intensidade da Luz Direta: %d%%",
                "gEnhancements.MM3D.Lighting.DirectionalScale",
                "Força da luz do sol, da lua e das lâmpadas sobre os personagens. 100 = igual ao N64.",
                WIDGET_CVAR_SLIDER_INT,
                { 50, 200, 110 } },
              { "Brilho Geral: %d%%",
                "gEnhancements.MM3D.Lighting.Brightness",
                "Clareia tudo na tela, inclusive o cenário. 100 = igual ao N64.",
                WIDGET_CVAR_SLIDER_INT,
                { 80, 150, 115 } },
              { "Saturação das Cores: %d%%",
                "gEnhancements.MM3D.Lighting.Saturation",
                "Cores mais vivas, como no 3DS. 100 = igual ao N64.",
                WIDGET_CVAR_SLIDER_INT,
                { 50, 150, 115 } } } } });
    enhancementsSidebar.push_back({ "Dificuldade",
                                    3,
                                    { { { "Takkuri Não Rouba",
                                          "gEnhancements.Cheats.DisableTakkuriSteal",
                                          "Impede o Takkuri de roubar itens importantes, como garrafas e espadas. Ele "
                                          "ainda pode roubar outros itens.",
                                          WIDGET_CVAR_CHECKBOX,
                                          {},
                                          [](widgetInfo& info) { RegisterDisableTakkuriSteal(); } } } } });
    enhancementsSidebar.push_back({ "Editor de HUD",
                                    1,
                                    { // HUD Editor
                                      { { "Abrir Editor de HUD em Janela",
                                          "gWindows.HudEditor",
                                          "Abre a janela do Editor de HUD para personalizar a interface",
                                          WIDGET_WINDOW_BUTTON,
                                          { .size = UIWidgets::Sizes::Inline, .windowName = "HUD Editor" } } } } });
    enhancementsSidebar.push_back(
        { "Rastreador de Itens",
          1,
          { // Item Tracker Settings
            { { "Abrir Configurações do Rastreador em Janela",
                "gWindows.ItemTrackerSettings",
                "",
                WIDGET_WINDOW_BUTTON,
                { .size = UIWidgets::Sizes::Inline, .windowName = "Item Tracker Settings" } } } } });
}

void AddDevTools() {
    devToolsSidebar.push_back(
        { "Geral",
          3,
          { { { "Menu em Janela", "gSettings.Menu.Popout", "Mostra o menu em uma janela em vez de sobreposto ao jogo.",
                WIDGET_CVAR_CHECKBOX },
              { "Marcar Ponto de Teletransporte",
                "",
                "Cria um ponto para onde você pode se teletransportar depois",
                WIDGET_BUTTON,
                {},
                [](widgetInfo& info) {
                    Player* player = GET_PLAYER(gPlayState);

                    CVarSetInteger(WARP_POINT_CVAR "Entrada", gSaveContext.save.entrance);
                    CVarSetInteger(WARP_POINT_CVAR "Sala", gPlayState->roomCtx.curRoom.num);
                    CVarSetFloat(WARP_POINT_CVAR "X", player->actor.world.pos.x);
                    CVarSetFloat(WARP_POINT_CVAR "Y", player->actor.world.pos.y);
                    CVarSetFloat(WARP_POINT_CVAR "Z", player->actor.world.pos.z);
                    CVarSetFloat(WARP_POINT_CVAR "Rotação", player->actor.shape.rot.y);
                    CVarSetInteger(WARP_POINT_CVAR "Salvo", 1);
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesOnNextTick();
                },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_NULL_PLAY_STATE).active; } },
              { "ID da Sala da Cena",
                "",
                "",
                WIDGET_TEXT,
                {},
                nullptr,
                [](widgetInfo& info) {
                    u32 sceneId = Entrance_GetSceneIdAbsolute(
                        CVarGetInteger(WARP_POINT_CVAR "Entrada", ENTRANCE(SOUTH_CLOCK_TOWN, 0)));
                    info.widgetName = fmt::format("{} Room {}", warpPointSceneList[sceneId],
                                                  CVarGetInteger(WARP_POINT_CVAR "Sala", 0));
                    info.isHidden = disabledMap.at(DISABLE_FOR_NULL_PLAY_STATE).active ||
                                    disabledMap.at(DISABLE_FOR_WARP_POINT_NOT_SET).active;
                } },
              { ICON_FA_TIMES,
                "",
                "Apagar ponto de teletransporte",
                WIDGET_BUTTON,
                { .size = UIWidgets::Sizes::Inline, .sameLine = true },
                [](widgetInfo& info) {
                    CVarClear(WARP_POINT_CVAR "Entrada");
                    CVarClear(WARP_POINT_CVAR "Sala");
                    CVarClear(WARP_POINT_CVAR "X");
                    CVarClear(WARP_POINT_CVAR "Y");
                    CVarClear(WARP_POINT_CVAR "Z");
                    CVarClear(WARP_POINT_CVAR "Rotação");
                    CVarClear(WARP_POINT_CVAR "Salvo");
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesOnNextTick();
                },
                [](widgetInfo& info) {
                    info.isHidden = disabledMap.at(DISABLE_FOR_NULL_PLAY_STATE).active ||
                                    disabledMap.at(DISABLE_FOR_WARP_POINT_NOT_SET).active;
                } },
              { "Teletransportar",
                "",
                "Teletransporta para o ponto marcado",
                WIDGET_BUTTON,
                { .size = UIWidgets::Sizes::Inline, .sameLine = true },
                [](widgetInfo& info) { Warp(); },
                [](widgetInfo& info) {
                    info.isHidden = disabledMap.at(DISABLE_FOR_NULL_PLAY_STATE).active ||
                                    disabledMap.at(DISABLE_FOR_WARP_POINT_NOT_SET).active;
                } } },
            { { "Modo de Depuração",
                "gDeveloperTools.DebugEnabled",
                "Ativa o modo de depuração, que permite escolher mapas com L + R + Z.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) {
                    if (!CVarGetInteger("gDeveloperTools.DebugEnabled", 0)) {
                        CVarClear("gDeveloperTools.DebugSaveFileMode");
                        CVarClear("gDeveloperTools.PreventActorUpdate");
                        CVarClear("gDeveloperTools.PreventActorDraw");
                        CVarClear("gDeveloperTools.PreventActorInit");
                        CVarClear("gDeveloperTools.DisableObjectDependency");
                        if (gPlayState != NULL) {
                            gPlayState->frameAdvCtx.enabled = false;
                        }
                        RegisterDebugSaveCreate();
                        RegisterPreventActorUpdateHooks();
                        RegisterPreventActorDrawHooks();
                        RegisterPreventActorInitHooks();
                    }
                } },
              { "Seleção de Mapas Melhorada",
                "gDeveloperTools.BetterMapSelect.Enabled",
                "Troca a seleção de mapas original por uma versão traduzida e mais fácil de usar.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Tipo de Save de Depuração",
                "gDeveloperTools.DebugSaveFileMode",
                "Muda o save criado com o modo de depuração ativo:\n\n- Save vazio: save padrão de 3 corações no "
                "primeiro ciclo\n- Save de depuração original: usa o save da tela de título (8 corações, todos os "
                "itens e máscaras)\n- Save 100\%: todos os itens, equipamentos, máscaras, status de missão e Bombers' "
                "Notebook completo",
                WIDGET_CVAR_COMBOBOX,
                { 0, 0, 0, debugSaveOptions },
                [](widgetInfo& info) { RegisterDebugSaveCreate(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Impedir Atualização dos Atores",
                "gDeveloperTools.PreventActorUpdate",
                "Impede os atores de atualizar.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterPreventActorUpdateHooks(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Impedir Desenho dos Atores",
                "gDeveloperTools.PreventActorDraw",
                "Impede os atores de serem desenhados.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterPreventActorDrawHooks(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Impedir Criação dos Atores",
                "gDeveloperTools.PreventActorInit",
                "Impede os atores de serem criados.",
                WIDGET_CVAR_CHECKBOX,
                {},
                [](widgetInfo& info) { RegisterPreventActorInitHooks(); },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Desativar Dependência de Objetos",
                "gDeveloperTools.DisableObjectDependency",
                "Desativa as dependências ao carregar objetos.",
                WIDGET_CVAR_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Nível de Log",
                "gDeveloperTools.LogLevel",
                "Define quais mensagens aparecem no console. Não afeta o arquivo de log",
                WIDGET_CVAR_COMBOBOX,
                { 0, 0, 0, logLevels },
                [](widgetInfo& info) {
                    Ship::Context::GetInstance()->GetLogger()->set_level(
                        (spdlog::level::level_enum)CVarGetInteger("gDeveloperTools.LogLevel", 1));
                },
                [](widgetInfo& info) { info.isHidden = disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active; } },
              { "Avanço de Quadro",
                "",
                "Permite avançar o jogo um quadro por vez. Para avançar, segure Z e toque R no segundo controle. "
                "Segurar Z e R avança um quadro a cada meio segundo. Você também pode usar os botões abaixo.",
                WIDGET_CHECKBOX,
                {},
                nullptr,
                [](widgetInfo& info) {
                    info.isHidden = disabledMap.at(DISABLE_FOR_NULL_PLAY_STATE).active ||
                                    disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active;
                    if (gPlayState != nullptr) {
                        info.widgetOptions.valuePointer = (bool*)&gPlayState->frameAdvCtx.enabled;
                    } else {
                        info.widgetOptions.valuePointer = (bool*)nullptr;
                    }
                } },
              { "Avançar 1",
                "",
                "Avança 1 quadro.",
                WIDGET_BUTTON,
                { .size = UIWidgets::Sizes::Inline },
                [](widgetInfo& info) { CVarSetInteger("gDeveloperTools.FrameAdvanceTick", 1); },
                [](widgetInfo& info) {
                    info.isHidden = disabledMap.at(DISABLE_FOR_FRAME_ADVANCE_OFF).active ||
                                    disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active;
                } },
              { "Avançar (Segurar)",
                "",
                "Avança quadros enquanto o botão estiver pressionado.",
                WIDGET_BUTTON,
                { .size = UIWidgets::Sizes::Inline, .sameLine = true },
                nullptr,
                [](widgetInfo& info) {
                    info.isHidden = disabledMap.at(DISABLE_FOR_FRAME_ADVANCE_OFF).active ||
                                    disabledMap.at(DISABLE_FOR_DEBUG_MODE_OFF).active;
                },
                [](widgetInfo& info) {
                    if (ImGui::IsItemActive()) {
                        CVarSetInteger("gDeveloperTools.FrameAdvanceTick", 1);
                    }
                } } } } });
    // dev tools windows
    devToolsSidebar.push_back({ "Visualizador de Colisão",
                                1,
                                { { { "Abrir Visualizador de Colisão em Janela",
                                      "gWindows.CollisionViewer",
                                      "Mostra a colisão na tela",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Collision Viewer" } } } } });
    devToolsSidebar.push_back({ "Estatísticas",
                                1,
                                { { { "Abrir Estatísticas em Janela",
                                      "gOpenWindows.Stats",
                                      "Mostra a janela de estatísticas com FPS, tempo de quadro e sistema operacional",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Stats" } } } } });
    devToolsSidebar.push_back({ "Console",
                                1,
                                { { { "Abrir Console em Janela",
                                      "gOpenWindows.Console",
                                      "Abre o console para digitar comandos. Digite help para ver exemplos",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Console" } } } } });
    devToolsSidebar.push_back({ "Depurador Gráfico",
                                1,
                                { { { "Abrir Depurador Gráfico em Janela",
                                      "gOpenWindows.GfxDebugger",
                                      "Abre o Depurador Gráfico para digitar comandos. Digite help para ver exemplos",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "GfxDebuggerWindow" } } } } });
    devToolsSidebar.push_back({ "Editor de Save",
                                1,
                                { { { "Abrir Editor de Save em Janela",
                                      "gWindows.SaveEditor",
                                      "Abre o Editor de Save para editar seu save",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Save Editor" } } } } });
    devToolsSidebar.push_back({ "Visualizador de Atores",
                                1,
                                { { { "Abrir Visualizador de Atores em Janela",
                                      "gWindows.ActorViewer",
                                      "Abre o Visualizador de Atores para ver os atores do mundo.",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Actor Viewer" } } } } });
    devToolsSidebar.push_back({ "Registro de Eventos",
                                1,
                                { { { "Abrir Registro de Eventos em Janela",
                                      "gWindows.EventLog",
                                      "Abre a janela de registro de eventos",
                                      WIDGET_WINDOW_BUTTON,
                                      { .size = UIWidgets::Sizes::Inline, .windowName = "Event Log" } } } } });
}

void SearchMenuGetItem(widgetInfo& widget) {
    disabledTempTooltip = "Esta opção está desativada porque: \n\n";
    disabledValue = false;
    disabledTooltip = " ";

    if (widget.preFunc != nullptr) {
        widget.activeDisables.clear();
        widget.isHidden = false;
        widget.preFunc(widget);
        if (widget.isHidden) {
            return;
        }
        if (!widget.activeDisables.empty()) {
            disabledValue = true;
            for (auto option : widget.activeDisables) {
                disabledTempTooltip += std::string("- ") + disabledMap.at(option).reason + std::string("\n");
            }
            disabledTooltip = disabledTempTooltip.c_str();
        }
    }

    if (widget.widgetOptions.sameLine) {
        ImGui::SameLine();
    }

    try {
        switch (widget.widgetType) {
            case WIDGET_CHECKBOX: {
                bool* pointer = std::get<bool*>(widget.widgetOptions.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("Checkbox Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                if (UIWidgets::Checkbox(
                        widget.widgetName.c_str(), pointer,
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled = disabledValue,
                          .disabledTooltip = disabledTooltip,
                          .labelPosition = widget.widgetOptions.labelPosition == UIWidgets::LabelPosition::None
                                               ? UIWidgets::LabelPosition::Near
                                               : widget.widgetOptions.labelPosition })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                }
            } break;
            case WIDGET_CVAR_CHECKBOX:
                if (UIWidgets::CVarCheckbox(
                        widget.widgetName.c_str(), widget.widgetCVar,
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled = disabledValue,
                          .disabledTooltip = disabledTooltip,
                          .defaultValue = static_cast<bool>(std::get<int32_t>(widget.widgetOptions.defaultVariant)),
                          .labelPosition = widget.widgetOptions.labelPosition == UIWidgets::LabelPosition::None
                                               ? UIWidgets::LabelPosition::Near
                                               : widget.widgetOptions.labelPosition })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                };
                break;
            case WIDGET_AUDIO_BACKEND: {
                auto currentAudioBackend = Ship::Context::GetInstance()->GetAudio()->GetAudioBackend();
                if (UIWidgets::Combobox(
                        "API de Áudio", &currentAudioBackend, audioBackendsMap,
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled =
                              Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1,
                          .disabledTooltip = "Só existe uma API de áudio nesta plataforma." })) {
                    Ship::Context::GetInstance()->GetAudio()->SetAudioBackend(currentAudioBackend);
                }
            } break;
            case WIDGET_VIDEO_BACKEND: {
                DrawWindowBackendButtons();
            } break;
            case WIDGET_CUSTOM:
                if (widget.widgetCallback != nullptr) {
                    widget.widgetCallback(widget);
                }
                break;
            case WIDGET_SEPARATOR:
                ImGui::Separator();
                break;
            case WIDGET_SEPARATOR_TEXT:
                if (widget.widgetOptions.color != COLOR_NONE) {
                    ImGui::PushStyleColor(ImGuiCol_Text, widget.widgetOptions.color);
                }
                ImGui::SeparatorText(widget.widgetName.c_str());
                if (widget.widgetOptions.color != COLOR_NONE) {
                    ImGui::PopStyleColor();
                }
                break;
            case WIDGET_TEXT:
                ImGui::AlignTextToFramePadding();
                ImGui::Text(widget.widgetName.c_str());
                break;
            case WIDGET_COMBOBOX: {
                int32_t* pointer = std::get<int32_t*>(widget.widgetOptions.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("Combobox Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                if (UIWidgets::Combobox(
                        widget.widgetName.c_str(), pointer, widget.widgetOptions.comboBoxOptions,
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled = disabledValue,
                          .disabledTooltip = disabledTooltip,
                          .labelPosition = widget.widgetOptions.labelPosition == UIWidgets::LabelPosition::None
                                               ? UIWidgets::LabelPosition::Above
                                               : widget.widgetOptions.labelPosition })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                };
            } break;
            case WIDGET_CVAR_COMBOBOX:
                if (UIWidgets::CVarCombobox(
                        widget.widgetName.c_str(), widget.widgetCVar, widget.widgetOptions.comboBoxOptions,
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled = disabledValue,
                          .disabledTooltip = disabledTooltip,
                          .defaultIndex = static_cast<uint32_t>(std::get<int32_t>(widget.widgetOptions.defaultVariant)),
                          .labelPosition = widget.widgetOptions.labelPosition == UIWidgets::LabelPosition::None
                                               ? UIWidgets::LabelPosition::Above
                                               : widget.widgetOptions.labelPosition })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                }
                break;
            case WIDGET_SLIDER_INT: {
                int32_t* pointer = std::get<int32_t*>(widget.widgetOptions.valuePointer);
                if (pointer == nullptr) {
                    SPDLOG_ERROR("int32 Slider Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                if (UIWidgets::SliderInt(widget.widgetName.c_str(), pointer,
                                         std::get<int32_t>(widget.widgetOptions.min),
                                         std::get<int32_t>(widget.widgetOptions.max),
                                         {
                                             .color = menuTheme[menuThemeIndex],
                                             .tooltip = widget.widgetTooltip,
                                             .disabled = disabledValue,
                                             .disabledTooltip = disabledTooltip,
                                         })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                };
            } break;
            case WIDGET_SLIDER_FLOAT: {
                float* pointer = std::get<float*>(widget.widgetOptions.valuePointer);

                if (pointer == nullptr) {
                    SPDLOG_ERROR("float Slider Widget requires a value pointer, currently nullptr");
                    assert(false);
                    return;
                }
                if (UIWidgets::SliderFloat(widget.widgetName.c_str(), pointer,
                                           std::get<float>(widget.widgetOptions.min),
                                           std::get<float>(widget.widgetOptions.max),
                                           { .color = menuTheme[menuThemeIndex],
                                             .tooltip = widget.widgetTooltip,
                                             .disabled = disabledValue,
                                             .disabledTooltip = disabledTooltip,
                                             .showButtons = widget.widgetOptions.showButtons,
                                             .format = widget.widgetOptions.format,
                                             .step = widget.widgetOptions.step,
                                             .isPercentage = widget.widgetOptions.isPercentage })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                }
            } break;
            case WIDGET_CVAR_SLIDER_INT:
                if (UIWidgets::CVarSliderInt(widget.widgetName.c_str(), widget.widgetCVar,
                                             std::get<int32_t>(widget.widgetOptions.min),
                                             std::get<int32_t>(widget.widgetOptions.max),
                                             std::get<int32_t>(widget.widgetOptions.defaultVariant),
                                             {
                                                 .color = menuTheme[menuThemeIndex],
                                                 .tooltip = widget.widgetTooltip,
                                                 .disabled = disabledValue,
                                                 .disabledTooltip = disabledTooltip,
                                             })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                };
                break;
            case WIDGET_CVAR_SLIDER_FLOAT: {
                if (UIWidgets::CVarSliderFloat(
                        widget.widgetName.c_str(), widget.widgetCVar, std::get<float>(widget.widgetOptions.min),
                        std::get<float>(widget.widgetOptions.max), std::get<float>(widget.widgetOptions.defaultVariant),
                        { .color = menuTheme[menuThemeIndex],
                          .tooltip = widget.widgetTooltip,
                          .disabled = disabledValue,
                          .disabledTooltip = disabledTooltip,
                          .showButtons = widget.widgetOptions.showButtons,
                          .format = widget.widgetOptions.format,
                          .step = widget.widgetOptions.step,
                          .isPercentage = widget.widgetOptions.isPercentage })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                }
            } break;
            case WIDGET_BUTTON:
                if (UIWidgets::Button(widget.widgetName.c_str(),
                                      { menuTheme[menuThemeIndex], widget.widgetOptions.size, widget.widgetTooltip,
                                        disabledValue, disabledTooltip })) {
                    if (widget.widgetCallback != nullptr) {
                        widget.widgetCallback(widget);
                    }
                }
                break;
            case WIDGET_WINDOW_BUTTON: {
                if (widget.widgetOptions.windowName == nullptr || widget.widgetOptions.windowName[0] == '\0') {
                    std::string msg =
                        fmt::format("Error drawing window contents for {}: windowName not defined", widget.widgetName);
                    SPDLOG_ERROR(msg.c_str());
                    break;
                }
                auto window =
                    Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow(widget.widgetOptions.windowName);
                if (!window) {
                    std::string msg = fmt::format("Error drawing window contents: windowName {} does not exist",
                                                  widget.widgetOptions.windowName);
                    SPDLOG_ERROR(msg.c_str());
                    break;
                }
                UIWidgets::WindowButton(widget.widgetName.c_str(), widget.widgetCVar, window,
                                        { .size = widget.widgetOptions.size, .tooltip = widget.widgetTooltip });
                if (!window->IsVisible()) {
                    window->DrawElement();
                }
            } break;
            case WIDGET_SEARCH: {
                if (ImGui::Button("Limpar")) {
                    menuSearch.Clear();
                }
                ImGui::SameLine();
                if (CVarGetInteger("gSettings.SearchAutofocus", 0) &&
                    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive() &&
                    !ImGui::IsMouseClicked(0)) {
                    ImGui::SetKeyboardFocusHere(0);
                }
                menuSearch.Draw();
                std::string menuSearchText(menuSearch.InputBuf);

                if (menuSearchText == "") {
                    ImGui::Text("Digite para ver resultados.");
                    return;
                }
                ImGui::BeginChild("Resultados da Busca");
                for (auto& [menuLabel, menuSidebar, cvar] : menuEntries) {
                    for (auto& sidebar : menuSidebar) {
                        for (auto& widgets : sidebar.columnWidgets) {
                            int column = 1;
                            for (auto& info : widgets) {
                                if (info.widgetType == WIDGET_SEARCH || info.widgetType == WIDGET_SEPARATOR ||
                                    info.widgetType == WIDGET_SEPARATOR_TEXT || info.isHidden) {
                                    continue;
                                }
                                std::string widgetStr =
                                    std::string(info.widgetName) +
                                    std::string(info.widgetTooltip != NULL ? info.widgetTooltip : "");
                                std::transform(menuSearchText.begin(), menuSearchText.end(), menuSearchText.begin(),
                                               ::tolower);
                                menuSearchText.erase(std::remove(menuSearchText.begin(), menuSearchText.end(), ' '),
                                                     menuSearchText.end());
                                std::transform(widgetStr.begin(), widgetStr.end(), widgetStr.begin(), ::tolower);
                                widgetStr.erase(std::remove(widgetStr.begin(), widgetStr.end(), ' '), widgetStr.end());
                                if (widgetStr.find(menuSearchText) != std::string::npos) {
                                    SearchMenuGetItem(info);
                                    ImGui::PushStyleColor(ImGuiCol_Text, UIWidgets::Colors::Gray);
                                    std::string origin =
                                        fmt::format("  ({} -> {}, Clmn {})", menuLabel, sidebar.label, column);
                                    ImGui::Text("%s", origin.c_str());
                                    ImGui::PopStyleColor();
                                }
                            }
                            column++;
                        }
                    }
                }
                ImGui::EndChild();
            } break;
            default:
                break;
        }
        if (widget.postFunc != nullptr) {
            widget.postFunc(widget);
        }
    } catch (const std::bad_variant_access& e) {
        SPDLOG_ERROR("Failed to draw menu item \"{}\" due to: {}", widget.widgetName, e.what());
        assert(false);
    }
}
} // namespace BenGui
