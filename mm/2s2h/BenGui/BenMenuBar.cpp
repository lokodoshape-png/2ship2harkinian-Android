#include "BenMenuBar.h"
#include "BenPort.h"
#include <imgui.h>
#include "public/bridge/consolevariablebridge.h"
#include <libultraship/libultraship.h>
#include "graphic/Fast3D/gfx_rendering_api.h"
#include "UIWidgets.hpp"
#include <unordered_map>
#include <string>
#include "2s2h/Enhancements/Enhancements.h"
#include "2s2h/Enhancements/GfxPatcher/AuthenticGfxPatches.h"
#include "2s2h/DeveloperTools/DeveloperTools.h"
#include "HudEditor.h"
#include "2s2h/Language/GameLanguage.h"

#include "2s2h/Enhancements/Trackers/ItemTracker.h"
#include "2s2h/Enhancements/Trackers/ItemTrackerSettings.h"

extern "C" {
#include "z64.h"
#include "functions.h"
extern PlayState* gPlayState;
}

static const std::unordered_map<int32_t, const char*> textureFilteringMap = {
    { FILTER_THREE_POINT, "Três pontos" },
    { FILTER_LINEAR, "Linear" },
    { FILTER_NONE, "Nenhum" },
};

static const std::unordered_map<int32_t, const char*> debugSaveOptions = {
    { DEBUG_SAVE_INFO_COMPLETE, "Save 100\%" },
    { DEBUG_SAVE_INFO_VANILLA_DEBUG, "Save de debug original" },
    { DEBUG_SAVE_INFO_NONE, "Save vazio" },
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

// Rendering resolution presets. Index 0 follows the screen; the others fix the vertical pixel count through
// libultraship's advanced resolution settings and keep the screen's aspect ratio.
static const std::unordered_map<int32_t, const char*> resolutionPresetOptions = {
    { 0, "Tela do aparelho (padrão)" },
    { 1, "N64 original (320x240, 4:3)" },
    { 2, "240p" },
    { 3, "360p" },
    { 4, "480p" },
    { 5, "540p" },
    { 6, "720p" },
    { 7, "900p" },
    { 8, "1080p" },
    { 9, "1440p" },
};
static const int32_t sResolutionPresetHeights[] = { 0, 240, 240, 360, 480, 540, 720, 900, 1080, 1440 };

static void ApplyResolutionPreset(int32_t preset) {
    const bool fixedHeight = preset >= 2;
    CVarSetInteger(CVAR_LOW_RES_MODE, preset == 1 ? 1 : 0);
    CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".Enabled", fixedHeight ? 1 : 0);
    if (fixedHeight) {
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalResolutionToggle", 1);
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".VerticalPixelCount", sResolutionPresetHeights[preset]);
        // An aspect ratio of 0:0 makes the width follow the screen's shape.
        CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioX", 0.0f);
        CVarSetFloat(CVAR_PREFIX_ADVANCED_RESOLUTION ".AspectRatioY", 0.0f);
        CVarSetInteger(CVAR_PREFIX_ADVANCED_RESOLUTION ".PixelPerfectMode", 0);
    }
}

static const std::unordered_map<int32_t, const char*> gameLanguageOptions = {
    { GameLanguage::LANGUAGE_ENGLISH, "English (original)" },
    { GameLanguage::LANGUAGE_PORTUGUESE_BR, "Português (pacote de tradução)" },
};

static const std::unordered_map<int32_t, const char*> clockTypeOptions = {
    { CLOCK_TYPE_ORIGINAL, "Original" },
    { CLOCK_TYPE_3DS, "Estilo MM3D" },
    { CLOCK_TYPE_TEXT_BASED, "Só texto" },
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

static const std::unordered_map<int32_t, const char*> timeStopOptions = {
    { TIME_STOP_OFF, "Desligado" },
    { TIME_STOP_TEMPLES, "Templos" },
    { TIME_STOP_TEMPLES_DUNGEONS, "Templos + minimasmorras" },
};

namespace BenGui {
std::shared_ptr<std::vector<Ship::WindowBackend>> availableWindowBackends;
std::unordered_map<Ship::WindowBackend, const char*> availableWindowBackendsMap;
Ship::WindowBackend configWindowBackend;

void UpdateWindowBackendObjects() {
    Ship::WindowBackend runningWindowBackend = Ship::Context::GetInstance()->GetWindow()->GetWindowBackend();
    int32_t configWindowBackendId = Ship::Context::GetInstance()->GetConfig()->GetInt("Window.Backend.Id", -1);
    if (Ship::Context::GetInstance()->GetWindow()->IsAvailableWindowBackend(configWindowBackendId)) {
        configWindowBackend = static_cast<Ship::WindowBackend>(configWindowBackendId);
    } else {
        configWindowBackend = runningWindowBackend;
    }

    availableWindowBackends = Ship::Context::GetInstance()->GetWindow()->GetAvailableWindowBackends();
    for (auto& backend : *availableWindowBackends) {
        availableWindowBackendsMap[backend] = windowBackendsMap[backend];
    }
}

void DrawMenuBarIcon() {
    static bool gameIconLoaded = false;
    if (!gameIconLoaded) {
        Ship::Context::GetInstance()->GetWindow()->GetGui()->LoadTextureFromRawImage("Game_Icon",
                                                                                     "textures/icons/g2ShipIcon.png");
        gameIconLoaded = true;
    }

    if (Ship::Context::GetInstance()->GetWindow()->GetGui()->HasTextureByName("Game_Icon")) {
#ifdef __SWITCH__
        ImVec2 iconSize = ImVec2(20.0f, 20.0f);
        float posScale = 1.0f;
#elif defined(__WIIU__)
        ImVec2 iconSize = ImVec2(16.0f * 2, 16.0f * 2);
        float posScale = 2.0f;
#elif defined(__ANDROID__)
        ImVec2 iconSize = ImVec2(16.0f * 2, 16.0f * 2);
        float posScale = 2.0f;
#else
        ImVec2 iconSize = ImVec2(16.0f, 16.0f);
        float posScale = 1.0f;
#endif
        ImGui::SetCursorPos(ImVec2(5, 5) * posScale);
        ImGui::Image(Ship::Context::GetInstance()->GetWindow()->GetGui()->GetTextureByName("Game_Icon"), iconSize);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(25, 0) * posScale);
    }
}

void DrawBenMenu() {
    if (UIWidgets::BeginMenu("2Ship")) {
        if (UIWidgets::MenuItem("Esconder barra de menu",
#if !defined(__SWITCH__) && !defined(__WIIU__) && !defined(__ANDROID__)
                                "F1"
#else
                                "[-]"
#endif
                                )) {
            Ship::Context::GetInstance()->GetWindow()->GetGui()->GetMenuBar()->ToggleVisibility();
        }
#if !defined(__SWITCH__) && !defined(__WIIU__) && !defined(__ANDROID__)
        if (UIWidgets::MenuItem("Alternar tela cheia", "F11")) {
            Ship::Context::GetInstance()->GetWindow()->ToggleFullscreen();
        }
#endif
        if (UIWidgets::MenuItem("Reiniciar",
#ifdef __APPLE__
                                "Command-R"
#elif !defined(__SWITCH__) && !defined(__WIIU__) && !defined(__ANDROID__)
                                "Ctrl+R"
#else
                                ""
#endif
                                )) {
            std::reinterpret_pointer_cast<Ship::ConsoleWindow>(
                Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"))
                ->Dispatch("reset");
        }
#if !defined(__SWITCH__) && !defined(__WIIU__) && !defined(__ANDROID__)
        if (UIWidgets::MenuItem("Abrir pasta de arquivos do app")) {
            std::string filesPath = Ship::Context::GetInstance()->GetAppDirectoryPath();
            SDL_OpenURL(std::string("file:///" + std::filesystem::absolute(filesPath).string()).c_str());
        }

        if (UIWidgets::MenuItem("Sair")) {
            Ship::Context::GetInstance()->GetWindow()->Close();
        }
#endif
        ImGui::EndMenu();
    }
}

extern std::shared_ptr<BenInputEditorWindow> mBenInputEditorWindow;

void DrawSettingsMenu() {
    if (UIWidgets::BeginMenu("Configurações")) {
        if (UIWidgets::BeginMenu("Áudio")) {
            UIWidgets::CVarSliderFloat("Volume geral: %.0f %%", "gSettings.Audio.MasterVolume", 0.0f, 1.0f, 1.0f,
                                       { .showButtons = false, .format = "", .isPercentage = true });

            if (UIWidgets::CVarSliderFloat("Volume da música principal: %.0f %%", "gSettings.Audio.MainMusicVolume",
                                           0.0f, 1.0f, 1.0f,
                                           { .showButtons = false, .format = "", .isPercentage = true })) {
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_BGM_MAIN, CVarGetFloat("gSettings.Audio.MainMusicVolume", 1.0f));
            }
            if (UIWidgets::CVarSliderFloat("Volume da música secundária: %.0f %%", "gSettings.Audio.SubMusicVolume",
                                           0.0f, 1.0f, 1.0f,
                                           { .showButtons = false, .format = "", .isPercentage = true })) {
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_BGM_SUB, CVarGetFloat("gSettings.Audio.SubMusicVolume", 1.0f));
            }
            if (UIWidgets::CVarSliderFloat("Volume dos efeitos sonoros: %.0f %%", "gSettings.Audio.SoundEffectsVolume",
                                           0.0f, 1.0f, 1.0f,
                                           { .showButtons = false, .format = "", .isPercentage = true })) {
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_SFX, CVarGetFloat("gSettings.Audio.SoundEffectsVolume", 1.0f));
            }
            if (UIWidgets::CVarSliderFloat("Volume das fanfarras: %.0f %%", "gSettings.Audio.FanfareVolume", 0.0f, 1.0f,
                                           1.0f, { .showButtons = false, .format = "", .isPercentage = true })) {
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_FANFARE, CVarGetFloat("gSettings.Audio.FanfareVolume", 1.0f));
            }
            if (UIWidgets::CVarSliderFloat("Volume do ambiente: %.0f %%", "gSettings.Audio.AmbienceVolume", 0.0f, 1.0f,
                                           1.0f, { .showButtons = false, .format = "", .isPercentage = true })) {
                AudioSeq_SetPortVolumeScale(SEQ_PLAYER_AMBIENCE, CVarGetFloat("gSettings.Audio.AmbienceVolume", 1.0f));
            }

            auto currentAudioBackend = Ship::Context::GetInstance()->GetAudio()->GetAudioBackend();
            if (UIWidgets::Combobox(
                    "API de áudio", &currentAudioBackend, audioBackendsMap,
                    { .tooltip = "Define a API de áudio usada pelo jogo. Precisa reabrir o app para valer.",
                      .disabled = Ship::Context::GetInstance()->GetAudio()->GetAvailableAudioBackends()->size() <= 1,
                      .disabledTooltip = "Só há uma API de áudio disponível nesta plataforma." })) {
                Ship::Context::GetInstance()->GetAudio()->SetAudioBackend(currentAudioBackend);
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Gráficos")) {

            if (UIWidgets::CVarCombobox("Resolução", "gSettings.ResolutionPreset", resolutionPresetOptions)) {
                ApplyResolutionPreset(CVarGetInteger("gSettings.ResolutionPreset", 0));
            }
            UIWidgets::Tooltip("Resolução em que o jogo é desenhado. Menor deixa mais leve e com cara de N64; maior "
                               "deixa mais nítido, mas pesa mais no aparelho. Vale na hora.");

#ifndef __APPLE__
            if (UIWidgets::CVarSliderFloat("Resolução interna: %f %%", CVAR_INTERNAL_RESOLUTION, 0.5f, 2.0f, 1.0f)) {
                Ship::Context::GetInstance()->GetWindow()->SetResolutionMultiplier(
                    CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1));
            };
            UIWidgets::Tooltip("Multiplica a resolução de saída pelo valor escolhido, uma forma de anti-aliasing mais "
                               "pesada, porém eficaz");
#endif
#ifndef __WIIU__
            if (UIWidgets::CVarSliderInt((CVarGetInteger(CVAR_MSAA_VALUE, 1) == 1) ? "Anti-aliasing (MSAA): desligado"
                                                                                   : "Anti-aliasing (MSAA): %d",
                                         CVAR_MSAA_VALUE, 1, 8, 1)) {
                Ship::Context::GetInstance()->GetWindow()->SetMsaaLevel(CVarGetInteger(CVAR_MSAA_VALUE, 1));
            };
            UIWidgets::Tooltip(
                "Ativa o MSAA (anti-aliasing por multiamostragem) de 2x até 8x, para suavizar as bordas da "
                "geometria.\nMais amostras deixam as bordas dos modelos mais suaves, mas podem reduzir o desempenho.");
#endif

            { // FPS Slider
                constexpr unsigned int minFps = 20;
                static unsigned int maxFps;
                if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
                    Ship::WindowBackend::FAST3D_DXGI_DX11) {
                    maxFps = 360;
                } else {
                    maxFps = Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();
                }
                unsigned int currentFps =
                    std::max(std::min(OTRGlobals::Instance->GetInterpolationFPS(), maxFps), minFps);
                bool matchingRefreshRate = CVarGetInteger("gMatchRefreshRate", 0) &&
                                           Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() !=
                                               Ship::WindowBackend::FAST3D_DXGI_DX11;
                UIWidgets::CVarSliderInt((currentFps == 20) ? "FPS: original (20)" : "FPS: %d", "gInterpolationFPS",
                                         minFps, maxFps, 20, { .disabled = matchingRefreshRate });
                if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
                    Ship::WindowBackend::FAST3D_DXGI_DX11) {
                    UIWidgets::Tooltip(
                        "Usa interpolação de matrizes para criar quadros extras e deixar a imagem mais fluida. É só "
                        "visual e não afeta a lógica do jogo, a execução de glitches etc.\n\nUm FPS maior que a taxa "
                        "de atualização da tela desperdiça recursos e pode piorar o resultado.");
                } else {
                    UIWidgets::Tooltip("Usa interpolação de matrizes para criar quadros extras e deixar a imagem mais "
                                       "fluida. É só visual e não afeta a lógica do jogo, a execução de glitches etc.");
                }
            } // END FPS Slider

            if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
                Ship::WindowBackend::FAST3D_DXGI_DX11) {
                // UIWidgets::Spacer(0);
                if (ImGui::Button("Igualar à taxa de atualização")) {
                    int hz = Ship::Context::GetInstance()->GetWindow()->GetCurrentRefreshRate();
                    if (hz >= 20 && hz <= 360) {
                        CVarSetInteger("gInterpolationFPS", hz);
                        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesOnNextTick();
                    }
                }
            } else {
                UIWidgets::CVarCheckbox("Igualar à taxa de atualização", "gMatchRefreshRate");
            }
            UIWidgets::Tooltip("Iguala o FPS da interpolação à taxa de atualização da tela");

            if (Ship::Context::GetInstance()->GetWindow()->GetWindowBackend() ==
                Ship::WindowBackend::FAST3D_DXGI_DX11) {
                UIWidgets::CVarSliderInt(CVarGetInteger("gExtraLatencyThreshold", 80) == 0
                                             ? "Correção de tremor: desligada"
                                             : "Correção de tremor: >= %d FPS",
                                         "gExtraLatencyThreshold", 0, 360, 80);
                UIWidgets::Tooltip("Quando o FPS da interpolação for pelo menos este valor, adiciona um quadro de "
                                   "atraso nos comandos (ex.: 16,6 ms a 60 FPS) para evitar tremores. Assim a CPU "
                                   "trabalha num quadro enquanto a GPU desenha o anterior.\nUse quando o aparelho for "
                                   "lento demais para fazer o trabalho de CPU + GPU a tempo.");
            }

            // UIWidgets::PaddedSeparator(true, true, 3.0f, 3.0f);
            //  #endregion */

            if (UIWidgets::Combobox(
                    "API de renderização (precisa reabrir)", &configWindowBackend, availableWindowBackendsMap,
                    { .tooltip = "Define a API de renderização usada pelo jogo. Precisa reabrir o app para valer.",
                      .disabled = availableWindowBackends->size() <= 1,
                      .disabledTooltip = "Só há uma API de renderização disponível nesta plataforma." })) {
                Ship::Context::GetInstance()->GetConfig()->SetInt("Window.Backend.Id",
                                                                  static_cast<int32_t>(configWindowBackend));
                Ship::Context::GetInstance()->GetConfig()->SetString("Window.Backend.Name",
                                                                     windowBackendsMap.at(configWindowBackend));
                Ship::Context::GetInstance()->GetConfig()->Save();
                UpdateWindowBackendObjects();
            }

            if (Ship::Context::GetInstance()->GetWindow()->CanDisableVerticalSync()) {
                UIWidgets::CVarCheckbox("Ativar Vsync", CVAR_VSYNC_ENABLED);
            }

            if (Ship::Context::GetInstance()->GetWindow()->SupportsWindowedFullscreen()) {
                UIWidgets::CVarCheckbox("Tela cheia em janela", CVAR_SDL_WINDOWED_FULLSCREEN);
            }

            if (Ship::Context::GetInstance()->GetWindow()->GetGui()->SupportsViewports()) {
                UIWidgets::CVarCheckbox(
                    "Permitir várias janelas", CVAR_ENABLE_MULTI_VIEWPORTS,
                    { .tooltip = "Permite abrir várias janelas ao mesmo tempo. Precisa reabrir para valer." });
            }

            UIWidgets::CVarCombobox("Filtro de textura (precisa reabrir)", CVAR_TEXTURE_FILTER, textureFilteringMap);

            // Currently this only has "Fonte dos textos sobrepostos", it doesn't use our new UIWidgets so it stands out
            // Ship::Context::GetInstance()->GetWindow()->GetGui()->GetGameOverlay()->DrawSettings();

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Idioma")) {
            static int32_t sGameLanguage = GameLanguage::GetSaved(appShortName);
            static const std::string sPackFolder =
                GameLanguage::GetPackFolder(GameLanguage::LANGUAGE_PORTUGUESE_BR, appShortName);
            if (UIWidgets::Combobox("Idioma do texto do jogo (precisa reabrir)", &sGameLanguage, gameLanguageOptions)) {
                GameLanguage::Save(static_cast<GameLanguage::Language>(sGameLanguage), appShortName);
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 900.0f);
            ImGui::TextWrapped("Vale na próxima vez que o app abrir. Para português, coloque o pacote de tradução "
                               "(.o2r, .zip ou .otr) na pasta:");
            ImGui::TextWrapped("%s", sPackFolder.c_str());
            ImGui::TextWrapped("Sem pacote nessa pasta, o texto do jogo continua em inglês.");
            ImGui::PopTextWrapPos();

            ImGui::EndMenu();
        }
        // #region 2S2H [Todo] None of this works yet
        /*
        if (UIWidgets::BeginMenu("Controle")) { */
        if (mBenInputEditorWindow) {
            UIWidgets::WindowButton("Mapeamento do controle", "gWindows.InputEditor", mBenInputEditorWindow);
        }
        /*
        #ifndef __SWITCH__
            UIWidgets::CVarCheckbox("Navegar na barra de menu com o controle", CVAR_IMGUI_CONTROLLER_NAV, {
                .tooltip = "Permite navegar na barra de menu (Configurações, Melhorias...) com o controle.\nATENÇÃO: os
        comandos do jogo ficam desativados enquanto a barra estiver visível.\n\nDirecional para mover entre itens, A
        para selecionar e X para focar a barra de menu"
            });
        #endif
            UIWidgets::CVarCheckbox("Mostrar comandos", "gInputEnabled", {
                .tooltip = "Mostra os botões apertados no canto inferior direito da tela"
            });
            UIWidgets::CVarSliderFloat("Tamanho dos comandos: %.1f", "gInputScale", 1.0f, 3.0f, 1.0f, {
                .tooltip = "Define o tamanho na tela dos comandos mostrados pela opção Mostrar comandos",
                .showButtons = false,
                .format = ""
            });
            UIWidgets::CVarSliderInt("Atraso simulado nos comandos: %d quadros", CVAR_SIMULATED_INPUT_LAG, 0, 6, 0, {
                .tooltip = "Guarda os comandos para executá-los alguns quadros depois"
            });

            ImGui::EndMenu();
        }
        ImGui::EndMenu();
        */
        ImGui::EndMenu();
    }
}

extern std::shared_ptr<HudEditorWindow> mHudEditorWindow;
extern std::shared_ptr<ItemTrackerWindow> mItemTrackerWindow;
extern std::shared_ptr<ItemTrackerSettingsWindow> mItemTrackerSettingsWindow;

void DrawEnhancementsMenu() {
    if (UIWidgets::BeginMenu("Melhorias")) {
        if (UIWidgets::BeginMenu("Câmera")) {
            ImGui::SeparatorText("Correções");
            UIWidgets::CVarCheckbox("Corrigir salto da câmera ao mirar", "gEnhancements.Camera.FixTargettingCameraSnap",
                                    { .tooltip = "Corrige o salto da câmera que acontece quando você está andando e "
                                                 "aperta o botão de mira (Z)." });

            ImGui::SeparatorText("Primeira pessoa");
            UIWidgets::CVarCheckbox("Desativar centralização automática",
                                    "gEnhancements.Camera.FirstPerson.DisableFirstPersonAutoCenterView");
            UIWidgets::CVarCheckbox("Inverter eixo X", "gEnhancements.Camera.FirstPerson.InvertX");
            UIWidgets::CVarCheckbox("Inverter eixo Y", "gEnhancements.Camera.FirstPerson.InvertY",
                                    { .defaultValue = true });
            UIWidgets::CVarSliderFloat("Sensibilidade do eixo X: %.0f%%",
                                       "gEnhancements.Camera.FirstPerson.SensitivityX", 0.1f, 2.0f, 1.0f,
                                       { .isPercentage = true });
            UIWidgets::CVarSliderFloat("Sensibilidade do eixo Y: %.0f%%",
                                       "gEnhancements.Camera.FirstPerson.SensitivityY", 0.1f, 2.0f, 1.0f,
                                       { .isPercentage = true });
            UIWidgets::CVarCheckbox("Mira por giroscópio", "gEnhancements.Camera.FirstPerson.GyroEnabled",
                                    { .tooltip = "Ativa a mira por giroscópio em primeira pessoa. Precisa de um "
                                                 "controle com giroscópio, mapeado no menu Mapeamento do controle" });
            if (CVarGetInteger("gEnhancements.Camera.FirstPerson.GyroEnabled", 0)) {
                UIWidgets::CVarCheckbox("Inverter eixo X do giroscópio",
                                        "gEnhancements.Camera.FirstPerson.GyroInvertX");
                UIWidgets::CVarCheckbox("Inverter eixo Y do giroscópio",
                                        "gEnhancements.Camera.FirstPerson.GyroInvertY");
                UIWidgets::CVarSliderFloat("Sensibilidade X do giroscópio: %.0f%%",
                                           "gEnhancements.Camera.FirstPerson.GyroSensitivityX", 0.1f, 2.0f, 1.0f,
                                           { .isPercentage = true });
                UIWidgets::CVarSliderFloat("Sensibilidade Y do giroscópio: %.0f%%",
                                           "gEnhancements.Camera.FirstPerson.GyroSensitivityY", 0.1f, 2.0f, 1.0f,
                                           { .isPercentage = true });
            }

            UIWidgets::CVarCheckbox("Mira com o analógico direito",
                                    "gEnhancements.Camera.FirstPerson.RightStickEnabled",
                                    { .tooltip = "Ativa a mira com o analógico direito em primeira pessoa. O analógico "
                                                 "direito precisa estar mapeado no menu Mapeamento do controle" });
            if (CVarGetInteger("gEnhancements.Camera.FirstPerson.RightStickEnabled", 0)) {
                UIWidgets::CVarCheckbox("Andar com o analógico esquerdo em primeira pessoa",
                                        "gEnhancements.Camera.FirstPerson.MoveInFirstPerson");
                UIWidgets::CVarCheckbox("Inverter eixo X do analógico direito",
                                        "gEnhancements.Camera.FirstPerson.RightStickInvertX");
                UIWidgets::CVarCheckbox("Inverter eixo Y do analógico direito",
                                        "gEnhancements.Camera.FirstPerson.RightStickInvertY", { .defaultValue = true });
                UIWidgets::CVarSliderFloat("Sensibilidade X do analógico direito: %.0f%%",
                                           "gEnhancements.Camera.FirstPerson.RightStickSensitivityX", 0.1f, 2.0f, 1.0f,
                                           { .isPercentage = true });
                UIWidgets::CVarSliderFloat("Sensibilidade Y do analógico direito: %.0f%%",
                                           "gEnhancements.Camera.FirstPerson.RightStickSensitivityY", 0.1f, 2.0f, 1.0f,
                                           { .isPercentage = true });
            }

            ImGui::SeparatorText("Câmera livre");
            if (UIWidgets::CVarCheckbox(
                    "Câmera livre", "gEnhancements.Camera.FreeLook.Enable",
                    { .tooltip = "Ativa o controle livre da câmera.\nObs.: tire os botões C do analógico direito no "
                                 "menu do controle e mapeie a câmera no analógico direito.",
                      .disabled = CVarGetInteger("gEnhancements.Camera.DebugCam.Enable", 0) != 0 })) {
                RegisterCameraFreeLook();
            }

            if (CVarGetInteger("gEnhancements.Camera.FreeLook.Enable", 0)) {
                UIWidgets::CVarCheckbox("Inverter eixo X da câmera", "gEnhancements.Camera.RightStick.InvertXAxis",
                                        { .tooltip = "Inverte o eixo X da câmera" });
                UIWidgets::CVarCheckbox("Inverter eixo Y da câmera", "gEnhancements.Camera.RightStick.InvertYAxis",
                                        { .tooltip = "Inverte o eixo Y da câmera", .defaultValue = true });
                UIWidgets::CVarSliderFloat("Sensibilidade horizontal (3ª pessoa): %.0f",
                                           "gEnhancements.Camera.RightStick.CameraSensitivity.X", 0.01f, 5.0f, 1.0f);
                UIWidgets::CVarSliderFloat("Sensibilidade vertical (3ª pessoa): %.0f",
                                           "gEnhancements.Camera.RightStick.CameraSensitivity.Y", 0.01f, 5.0f, 1.0f);

                UIWidgets::CVarSliderInt("Distância da câmera: %d", "gEnhancements.Camera.FreeLook.MaxCameraDistance",
                                         100, 900, 185);
                UIWidgets::CVarSliderInt("Velocidade de transição da câmera: %d",
                                         "gEnhancements.Camera.FreeLook.TransitionSpeed", 1, 900, 25);
                UIWidgets::CVarSliderFloat("Ângulo máximo de altura da câmera: %.0f°",
                                           "gEnhancements.Camera.FreeLook.MaxPitch", -89.0f, 89.0f, 72.0f);
                UIWidgets::CVarSliderFloat("Ângulo mínimo de altura da câmera: %.0f°",
                                           "gEnhancements.Camera.FreeLook.MinPitch", -89.0f, 89.0f, -49.0f);
                f32 maxY = CVarGetFloat("gEnhancements.Camera.FreeLook.MaxPitch", 72.0f);
                f32 minY = CVarGetFloat("gEnhancements.Camera.FreeLook.MinPitch", -49.0f);
                CVarSetFloat("gEnhancements.Camera.FreeLook.MaxPitch", std::max(maxY, minY));
                CVarSetFloat("gEnhancements.Camera.FreeLook.MinPitch", std::min(maxY, minY));
            }

            ImGui::SeparatorText("Câmera de 'debug'");
            if (UIWidgets::CVarCheckbox(
                    "Câmera de debug", "gEnhancements.Camera.DebugCam.Enable",
                    { .tooltip = "Ativa o controle livre da câmera.",
                      .disabled = CVarGetInteger("gEnhancements.Camera.FreeLook.Enable", 0) != 0 })) {
                RegisterDebugCam();
            }

            if (CVarGetInteger("gEnhancements.Camera.DebugCam.Enable", 0)) {
                UIWidgets::CVarCheckbox("Inverter eixo X da câmera", "gEnhancements.Camera.RightStick.InvertXAxis",
                                        { .tooltip = "Inverte o eixo X da câmera" });
                UIWidgets::CVarCheckbox("Inverter eixo Y da câmera", "gEnhancements.Camera.RightStick.InvertYAxis",
                                        { .tooltip = "Inverte o eixo Y da câmera", .defaultValue = true });
                UIWidgets::CVarSliderFloat("Sensibilidade horizontal (3ª pessoa): %.0f",
                                           "gEnhancements.Camera.RightStick.CameraSensitivity.X", 0.01f, 5.0f, 1.0f);
                UIWidgets::CVarSliderFloat("Sensibilidade vertical (3ª pessoa): %.0f",
                                           "gEnhancements.Camera.RightStick.CameraSensitivity.Y", 0.01f, 5.0f, 1.0f);

                UIWidgets::CVarCheckbox(
                    "Ativar rolagem (seis graus de liberdade)", "gEnhancements.Camera.DebugCam.6DOF",
                    { .tooltip = "Permite mover a câmera nos seis graus de liberdade. OBS.: a rotação horizontal "
                                 "funciona diferente aqui, girando em volta do ponto focal em vez de um eixo polar." });
                UIWidgets::CVarSliderFloat("Velocidade da câmera: %.0f", "gEnhancements.Camera.DebugCam.CameraSpeed",
                                           0.1f, 3.0f, 0.5f);
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Cenas")) {
            UIWidgets::CVarCheckbox("Esconder títulos de área", "gEnhancements.Cutscenes.HideTitleCards");
            UIWidgets::CVarCheckbox("Pular cenas de entrada", "gEnhancements.Cutscenes.SkipEntranceCutscenes");
            UIWidgets::CVarCheckbox("Ir direto para a seleção de arquivo", "gEnhancements.Cutscenes.SkipToFileSelect",
                                    { .tooltip = "Pula a abertura e vai direto para a seleção de arquivo ao iniciar" });
            UIWidgets::CVarCheckbox(
                "Pular introdução", "gEnhancements.Cutscenes.SkipIntroSequence",
                {
                    .tooltip = "Ao começar um jogo novo, você vai direto para South Clock Town como Link Deku.",
                });
            if (CVarGetInteger("gEnhancements.Cutscenes.SkipIntroSequence", 0)) {
                UIWidgets::CVarCheckbox(
                    "Pular o primeiro ciclo", "gEnhancements.Cutscenes.SkipFirstCycle",
                    { .tooltip = "Ao começar um jogo novo, você vai direto para South Clock Town como Link humano, com "
                                 "a Máscara Deku, a Ocarina, a Song of Time e a Song of Healing." });
            }
            UIWidgets::CVarCheckbox("Pular cenas da história", "gEnhancements.Cutscenes.SkipStoryCutscenes",
                                    {
                                        .tooltip = "Aviso: ainda faz pouco; mais cenas serão puladas com o tempo",
                                    });
            UIWidgets::CVarCheckbox("Pular interações diversas", "gEnhancements.Cutscenes.SkipMiscInteractions",
                                    {
                                        .tooltip = "Aviso: ainda faz pouco; mais cenas serão puladas com o tempo",
                                    });

            UIWidgets::CVarCheckbox("Segurar B para acelerar cenas", "gEnhancements.Cutscenes.HoldBToFastForward",
                                    { .tooltip = "Durante uma cena, segure B por meio segundo para ela passar bem "
                                                 "mais rápido. A cena roda inteira, só que acelerada, então nada do "
                                                 "jogo é pulado. Combine com Texto rápido para passar as falas." });
            if (CVarGetInteger("gEnhancements.Cutscenes.HoldBToFastForward", 0)) {
                UIWidgets::CVarSliderInt("Velocidade das cenas aceleradas: %dx",
                                         "gEnhancements.Cutscenes.FastForwardSpeed", 2, 8, 5);
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Salvar / ciclo do tempo")) {

            ImGui::SeparatorText("Salvar");
            UIWidgets::CVarCheckbox(
                "Saves da coruja permanentes", "gEnhancements.Saving.PersistentOwlSaves",
                { .tooltip = "Continuar um save não apaga o save da coruja. Tocar a Song of Time, deixar a lua cair ou "
                             "terminar o jogo apaga o save da coruja, que vira o último save." });
            UIWidgets::CVarCheckbox(
                "Salvar no menu de pausa", "gEnhancements.Saving.PauseSave",
                { .tooltip = "Volta a permitir salvar no menu de pausa. Apertar B na pausa dá a opção de criar um save "
                             "da coruja permanente no lugar onde você está.\n\nAo carregar, você volta na entrada da "
                             "masmorra onde salvou ou em South Clock Town." });
            if (UIWidgets::CVarCheckbox(
                    "Salvamento automático", "gEnhancements.Saving.Autosave",
                    { .tooltip = "Cria um save da coruja permanente sozinho no intervalo escolhido.\n\nAo carregar, "
                                 "você volta na entrada da masmorra onde salvou ou em South Clock Town." })) {
                RegisterAutosave();
            }
            UIWidgets::CVarSliderInt("Intervalo do salvamento automático (minutos): %d",
                                     "gEnhancements.Saving.AutosaveInterval", 1, 60, 5,
                                     { .disabled = !CVarGetInteger("gEnhancements.Saving.Autosave", 0) });

            ImGui::SeparatorText("Ciclo do tempo");
            UIWidgets::CVarCheckbox("Não esvaziar as garrafas", "gEnhancements.Cycle.DoNotResetBottleContent",
                                    { .tooltip = "Tocar a Song of Time não esvazia as garrafas." });
            UIWidgets::CVarCheckbox("Não zerar os consumíveis", "gEnhancements.Cycle.DoNotResetConsumables",
                                    { .tooltip = "Tocar a Song of Time não zera os consumíveis." });
            UIWidgets::CVarCheckbox("Não perder a Razor Sword", "gEnhancements.Cycle.DoNotResetRazorSword",
                                    { .tooltip = "Tocar a Song of Time não volta a espada para a Kokiri Sword." });
            UIWidgets::CVarCheckbox("Não zerar as rupias", "gEnhancements.Cycle.DoNotResetRupees",
                                    { .tooltip = "Tocar a Song of Time não zera as suas rupias." });
            UIWidgets::CVarCheckbox(
                "Não resetar a velocidade do tempo", "gEnhancements.Cycle.DoNotResetTimeSpeed",
                { .tooltip =
                      "Tocar a Song of Time não desfaz a velocidade do tempo escolhida com a Inverted Song of Time." });

            if (UIWidgets::CVarCheckbox("Manter o Express Mail", "gEnhancements.Cycle.KeepExpressMail",
                                        { .tooltip = "Mantém o Express Mail no inventário depois da primeira entrega, "
                                                     "para fazer as duas entregas no mesmo ciclo" })) {
                RegisterKeepExpressMail();
            }

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 255, 0, 255));
            ImGui::SeparatorText("Instável");
            ImGui::PopStyleColor();
            UIWidgets::CVarCheckbox("Remover espera ao salvar", "gEnhancements.Saving.DisableSaveDelay",
                                    { .tooltip = "Remove a espera de 2 segundos ao salvar do jogo original. Pode "
                                                 "causar problemas ao tentar o glitch do Dia 0" });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Diálogos")) {
            UIWidgets::CVarCheckbox(
                "Seleção rápida no banco", "gEnhancements.Dialogue.FastBankSelection",
                { .tooltip =
                      "Com a tela de depositar/sacar rupias aberta, Z coloca todas as rupias do Link e R coloca 0." });
            UIWidgets::CVarCheckbox("Texto rápido", "gEnhancements.Dialogue.FastText",
                                    { .tooltip = "Acelera os textos e permite segurar B para avançar as mensagens" });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Direcional")) {
            UIWidgets::CVarCheckbox("Equipar no direcional", "gEnhancements.Dpad.DpadEquips",
                                    { .tooltip = "Permite equipar itens no direcional" });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Equipamento")) {
            UIWidgets::CVarCheckbox("Animação rápida das flechas mágicas",
                                    "gEnhancements.Equipment.MagicArrowEquipSpeed",
                                    { .tooltip = "Remove a animação ao equipar flechas mágicas." });

            UIWidgets::CVarCheckbox(
                "Retorno instantâneo das nadadeiras", "gEnhancements.PlayerActions.InstantRecall",
                { .tooltip = "Apertar B faz as nadadeiras-bumerangue voltarem na hora para o Link Zora." });

            UIWidgets::CVarCheckbox(
                "Giratório com espadas de duas mãos", "gEnhancements.Equipment.TwoHandedSwordSpinAttack",
                { .tooltip =
                      "Permite o ataque giratório mágico com a espada da Fierce Deity e a Great Fairy's Sword." });
            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Correções")) {
            UIWidgets::CVarCheckbox("Corrigir cor da munição", "gFixes.FixAmmoCountEnvColor",
                                    { .tooltip = "Corrige um gDPSetEnvColor que falta e deixa o contador de munição "
                                                 "com a cor errada antes de ter magia, entre outros casos." });

            UIWidgets::CVarCheckbox("Corrigir movimento da Fierce Deity com Z",
                                    "gEnhancements.Fixes.FierceDeityZTargetMovement",
                                    { .tooltip = "Corrige o movimento travado da Fierce Deity ao mirar com Z" });

            UIWidgets::CVarCheckbox("Corrigir travamento do HESS e Weirdshot", "gEnhancements.Fixes.HessCrash",
                                    { .tooltip = "Corrige um travamento que pode acontecer ao fazer HESS ou Weirdshot.",
                                      .defaultValue = true });

            UIWidgets::CVarCheckbox(
                "Corrigir caracteres de controle do texto", "gEnhancements.Fixes.ControlCharacters",
                { .tooltip =
                      "Corrige caracteres de controle que não funcionam direito dependendo da posição no texto." });

            UIWidgets::CVarCheckbox("Corrigir cor da fonte da Grande Fada de Ikana",
                                    "gFixes.FixIkanaGreatFairyFountainColor",
                                    { .tooltip = "Corrige o bug que deixa a fonte da Grande Fada de Ikana verde em vez "
                                                 "de amarela; isso foi corrigido na versão europeia" });

            if (UIWidgets::CVarCheckbox("Corrigir estouro de texturas", "gEnhancements.Fixes.FixTexturesOOB",
                                        { .tooltip = "Corrige texturas que estouram, usando o tamanho ou formato certo",
                                          .defaultValue = true })) {
                GfxPatcher_ApplyOverflowTexturePatches();
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Gráficos")) {
            ImGui::SeparatorText("Relógio");
            UIWidgets::CVarCombobox("Tipo de relógio", "gEnhancements.Graphics.ClockType", clockTypeOptions);
            UIWidgets::CVarCheckbox("Relógio de 24 horas", "gEnhancements.Graphics.24HoursClock");

            ImGui::SeparatorText("Mods");
            UIWidgets::CVarCheckbox("Usar assets alternativos", "gEnhancements.Mods.AlternateAssets",
                                    { .tooltip = "Alterna entre os assets normais e os alternativos. Normalmente o mod "
                                                 "avisa se essa opção precisa estar ligada." });

            MotionBlur_RenderMenuOptions();
            ImGui::SeparatorText("Outros");
            UIWidgets::CVarCheckbox("Logo autêntico", "gEnhancements.Graphics.AuthenticLogo",
                                    { .tooltip = "Esconde a versão e os detalhes do build e mostra o modelo e a "
                                                 "textura autênticos no logo da tela inicial" });
            UIWidgets::CVarCheckbox("Mira do arco", "gEnhancements.Graphics.BowReticle",
                                    { .tooltip = "Mostra uma mira ao puxar uma flecha" });
            if (UIWidgets::CVarCheckbox("Itens soltos em 3D", "gEnhancements.Graphics.3DItemDrops",
                                        { .tooltip = "Deixa os itens que caem no chão em 3D" })) {
                Register3DItemDrops();
            }
            UIWidgets::CVarCheckbox(
                "Remover faixas pretas", "gEnhancements.Graphics.DisableBlackBars",
                { .tooltip = "Remove as faixas pretas nas cenas e ao mirar com Z\nObs.: podem aparecer pequenos "
                             "defeitos visuais que as faixas escondiam\nDesligue esta opção antes de relatar um bug" });

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255, 255, 0, 255));
            ImGui::SeparatorText("Instável");
            ImGui::PopStyleColor();
            if (UIWidgets::CVarCheckbox(
                    "Desativar limite de distância do cenário",
                    "gEnhancements.Graphics.DisableSceneGeometryDistanceCheck",
                    { .tooltip = "Desativa o limite de distância do cenário, que passa a ser desenhado a qualquer "
                                 "distância. Pode ter efeitos colaterais." })) {
                GfxPatcher_ApplyGeometryIssuePatches();
            }
            UIWidgets::CVarCheckbox(
                "Recorte de atores para widescreen", "gEnhancements.Graphics.ActorCullingAccountsForWidescreen",
                { .tooltip = "Ajusta o recorte de objetos para telas widescreen. Pode ter efeitos colaterais." });
            UIWidgets::CVarSliderInt("Distância de desenho dos atores: %dx",
                                     "gEnhancements.Graphics.IncreaseActorDrawDistance", 1, 5, 1,
                                     { .tooltip = "Aumenta a distância em que personagens e objetos são desenhados. "
                                                  "Pode ter efeitos colaterais." });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Máscaras")) {
            UIWidgets::CVarCheckbox("Blast Mask com força de Powder Keg", "gEnhancements.Masks.BlastMaskKeg");
            UIWidgets::CVarCheckbox("Transformação rápida", "gEnhancements.Masks.FastTransformation");
            UIWidgets::CVarCheckbox("Máscara da Fierce Deity em qualquer lugar",
                                    "gEnhancements.Masks.FierceDeitysAnywhere",
                                    { .tooltip = "Permite usar a máscara da Fierce Deity fora das salas de chefe." });
            UIWidgets::CVarCheckbox("Blast Mask sem espera", "gEnhancements.Masks.NoBlastMaskCooldown", {});
            if (UIWidgets::CVarCheckbox("Bunny Hood permanente", "gEnhancements.Masks.PersistentBunnyHood.Enabled",
                                        { .tooltip = "Liga ou desliga de vez a velocidade da Bunny Hood apertando 'A' "
                                                     "nela no menu de máscaras." })) {
                UpdatePersistentMasksState();
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Minigames")) {
            UIWidgets::CVarCombobox("Sempre ganhar a corrida de cachorros",
                                    "gEnhancements.Minigames.AlwaysWinDoggyRace", alwaysWinDoggyraceOptions);
            UIWidgets::CVarCombobox(
                "Prêmio da corrida do leite", "gEnhancements.Minigames.CremiaHugs", cremiaRewardOptions,
                { .tooltip = "Escolhe o prêmio por vencer a corrida do leite depois da primeira vez.\n-Original: "
                             "prêmio aleatório\n-Abraço: cena do abraço\n-Rupias: prêmio em rupias",
                  .defaultIndex = CREMIA_REWARD_RANDOM });
            UIWidgets::CVarSliderInt("Pontos para vencer na escola de espada: %d",
                                     "gEnhancements.Minigames.SwordsmanSchoolScore", 1, 30, 30);

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Modos")) {
            UIWidgets::CVarCheckbox("Jogar como Kafei", "gModes.PlayAsKafei",
                                    { .tooltip = "Precisa recarregar a área para valer." });
            if (UIWidgets::CVarCheckbox("O tempo só passa quando você se move", "gModes.TimeMovesWhenYouMove")) {
                RegisterTimeMovesWhenYouMove();
            }
            if (UIWidgets::CVarCheckbox("Mundo espelhado", "gModes.MirroredWorld.Mode")) {
                if (CVarGetInteger("gModes.MirroredWorld.Mode", 0)) {
                    CVarSetInteger("gModes.MirroredWorld.State", 1);
                } else {
                    CVarClear("gModes.MirroredWorld.State");
                }
            }

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Jogador")) {
            UIWidgets::CVarSliderInt("Velocidade de escalada", "gEnhancements.Player.ClimbSpeed", 1, 5, 1,
                                     { .tooltip = "Aumenta a velocidade do Link em trepadeiras e escadas." });
            if (UIWidgets::CVarCheckbox(
                    "Lançamento rápido da flor Deku", "gEnhancements.Player.FastFlowerLaunch",
                    { .tooltip = "Diminui o tempo para atingir a altura máxima ao sair de uma flor Deku" })) {
                RegisterFastFlowerLaunch();
            }
            UIWidgets::CVarCheckbox("Guardar item na hora", "gEnhancements.Player.InstantPutaway",
                                    { .tooltip = "Link guarda o item na mão na hora, sem esperar." });
            UIWidgets::CVarCheckbox("Fierce Deity guarda a espada", "gEnhancements.Player.FierceDeityPutaway",
                                    { .tooltip = "Permite ao Link Fierce Deity guardar a espada." });
            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Restaurações")) {
            UIWidgets::CVarCheckbox(
                "Mortais e saltos laterais de distância fixa", "gEnhancements.Restorations.ConstantFlipsHops",
                { .tooltip = "Mortais para trás e saltos laterais percorrem uma distância fixa, como no OOT." });
            UIWidgets::CVarCheckbox(
                "Golpe agachado forte", "gEnhancements.Restorations.PowerCrouchStab",
                { .tooltip = "O golpe agachado usa a força do ataque anterior do Link, como no MM JP 1.0 e no OOT." });
            UIWidgets::CVarCheckbox("Rolamentos laterais", "gEnhancements.Restorations.SideRoll",
                                    { .tooltip = "Traz de volta o rolamento lateral do OOT." });
            UIWidgets::CVarCheckbox("ISG com a Tatl", "gEnhancements.Restorations.TatlISG",
                                    { .tooltip = "Traz de volta o ISG da Navi do OOT, agora com a Tatl." });

            if (UIWidgets::CVarCheckbox(
                    "Aparência da montanha de Woodfall", "gEnhancements.Restorations.WoodfallMountainAppearance",
                    { .tooltip = "Faz a montanha de Woodfall deixar de parecer envenenada, vista de Termina Field, "
                                 "depois de vencer o Woodfall Temple\n\nPrecisa recarregar a área para valer" })) {
                RegisterWoodfallMountainAppearance();
            }
            if (UIWidgets::CVarCheckbox(
                    "Bombers' Notebook antecipado", "gEnhancements.MM3D.EarlyBombersNotebook",
                    { .tooltip = "Como no Majora's Mask 3D, ganhe o Bombers' Notebook na primeira visita a Clock Town, "
                                 "sem precisar do esconde-esconde dos Bombers." })) {
                RegisterEarlyBombersNotebook();
            }
            UIWidgets::CVarCheckbox(
                "Variações da Song of Time na pausa", "gEnhancements.MM3D.TimeSongVariantsInPause",
                { .tooltip = "Como no Majora's Mask 3D, a Song of Time na tela de status também mostra a Inverted Song "
                             "of Time, a Song of Double Time e a música do espantalho. Com o cursor na Song of Time, "
                             "aperte L ou C-baixo para trocar e A para ouvir as notas." });
            UIWidgets::CVarCheckbox(
                "Guia do Bombers' Notebook", "gEnhancements.MM3D.BombersNotebookGuide",
                { .tooltip = "Ao abrir o Bombers' Notebook, abre também um guia com o prêmio de cada pessoa, o dia e a "
                             "hora de encontrá-la, o lugar e a máscara ou condição necessária." });
            UIWidgets::CVarCheckbox(
                "Iluminação estilo MM3D", "gEnhancements.MM3D.Lighting.Enabled",
                { .tooltip = "Cenas mais claras, com cores mais vivas e bem menos neblina, mais perto do Majora's "
                             "Mask 3D. Brilho geral e Saturação valem para o cenário todo; as outras opções "
                             "afetam personagens e a neblina." });
            UIWidgets::CVarSliderInt("Redução da neblina: %d%%", "gEnhancements.MM3D.Lighting.FogReduction", 0, 100,
                                     50);
            UIWidgets::CVarSliderInt("Reforço da luz ambiente: %d", "gEnhancements.MM3D.Lighting.AmbientBoost", 0, 100,
                                     20);
            UIWidgets::CVarSliderInt("Luz direta: %d%%", "gEnhancements.MM3D.Lighting.DirectionalScale", 50, 200, 110);
            UIWidgets::CVarSliderInt("Brilho geral: %d%%", "gEnhancements.MM3D.Lighting.Brightness", 80, 150, 115);
            UIWidgets::Tooltip("Clareia tudo na tela, inclusive o cenário. 100 = igual ao N64.");
            UIWidgets::CVarSliderInt("Saturação das cores: %d%%", "gEnhancements.MM3D.Lighting.Saturation", 50, 150,
                                     115);
            UIWidgets::Tooltip("Cores mais vivas, como no 3DS. 100 = igual ao N64.");

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Músicas/Ocarina")) {
            UIWidgets::CVarCheckbox(
                "Ativar a Sun's Song", "gEnhancements.Songs.EnableSunsSong",
                { .tooltip =
                      "Ativa a Sun's Song, implementada em parte. Toque DIREITA-BAIXO-CIMA-DIREITA-BAIXO-CIMA. Ela faz "
                      "o tempo correr muito rápido até o Link mudar de área ou o período do dia mudar." });
            UIWidgets::CVarCheckbox("Ocarina no direcional", "gEnhancements.Playback.DpadOcarina",
                                    { .tooltip = "Permite tocar a ocarina com o direcional." });
            UIWidgets::CVarCheckbox("Não perder notas da ocarina", "gEnhancements.Playback.NoDropOcarinaInput",
                                    { .tooltip = "Evita perder notas ao tocar a ocarina rápido" });
            UIWidgets::CVarCheckbox("Pular a música do espantalho", "gEnhancements.Playback.SkipScarecrowSong",
                                    { .tooltip = "O Pierre aparece quando você pega a ocarina." });
            UIWidgets::CVarCheckbox(
                "Viajar às corujas pela pausa", "gEnhancements.Songs.PauseOwlWarp",
                { .tooltip = "Permite viajar às estátuas de coruja ativadas pelo mapa da pausa. Você precisa poder "
                             "tocar a Song of Soaring normalmente.\n\nConsidera o Index-Warp ativo mostrando todos os "
                             "destinos válidos para os pontos do mapa. Great Bay Coast sempre aparece como destino do "
                             "índice 0, por conveniência." });
            UIWidgets::CVarCheckbox(
                "Escolher a hora na Song of Double Time", "gEnhancements.Songs.DoubleTimeHourSelect",
                { .tooltip = "Como no Majora's Mask 3D, escolha para qual hora a Song of Double Time leva você. Use "
                             "esquerda/direita no analógico ou no direcional durante a confirmação. Dá para escolher "
                             "até o próximo amanhecer ou anoitecer." });
            UIWidgets::CVarSliderInt(
                "Ovos Zora para a Bossa Nova", "gEnhancements.Songs.ZoraEggCount", 1, 7, 7,
                { .tooltip = "Quantidade de ovos necessária para liberar a New Wave Bossa Nova." });

            ImGui::EndMenu();
        }

        if (UIWidgets::BeginMenu("Dificuldade")) {
            if (UIWidgets::CVarCheckbox("Takkuri não rouba", "gEnhancements.Cheats.DisableTakkuriSteal",
                                        { .tooltip = "Impede o Takkuri de roubar itens importantes, como garrafas e "
                                                     "espadas. Ele ainda pode roubar outros itens." })) {
                RegisterDisableTakkuriSteal();
            }
            ImGui::EndMenu();
        }

        if (mHudEditorWindow) {
            UIWidgets::WindowButton("Editor de HUD", "gWindows.HudEditor", mHudEditorWindow,
                                    { .tooltip = "Abre a janela do editor de HUD, para editar a interface" });
        }

        if (mItemTrackerWindow) {
            UIWidgets::WindowButton("Rastreador de itens", "gWindows.ItemTracker", mItemTrackerWindow);
        }

        if (mItemTrackerSettingsWindow) {
            UIWidgets::WindowButton("Configurações do rastreador", "gWindows.ItemTrackerSettings",
                                    mItemTrackerSettingsWindow);
        }

        ImGui::EndMenu();
    }
}

void DrawCheatsMenu() {
    if (UIWidgets::BeginMenu("Trapaças")) {
        UIWidgets::CVarCheckbox("Vida infinita", "gCheats.InfiniteHealth");
        UIWidgets::CVarCheckbox("Magia infinita", "gCheats.InfiniteMagic");
        UIWidgets::CVarCheckbox("Rupias infinitas", "gCheats.InfiniteRupees");
        UIWidgets::CVarCheckbox("Consumíveis infinitos", "gCheats.InfiniteConsumables");
        if (UIWidgets::CVarCheckbox(
                "Planar mais com a flor Deku", "gCheats.LongerFlowerGlide",
                { .tooltip = "O Link Deku plana por mais tempo, sem cair depois de certa distância" })) {
            RegisterLongerFlowerGlide();
        }
        UIWidgets::CVarCheckbox("Atravessar paredes", "gCheats.NoClip");
        UIWidgets::CVarCheckbox("Razor Sword inquebrável", "gCheats.UnbreakableRazorSword");
        UIWidgets::CVarCheckbox("Itens sem restrição", "gCheats.UnrestrictedItems");
        if (UIWidgets::CVarCheckbox("Pulo lunar no L", "gCheats.MoonJumpOnL",
                                    { .tooltip = "Segurar L faz você flutuar" })) {
            RegisterMoonJumpOnL();
        }
        UIWidgets::CVarCheckbox("Elegy of Emptiness em qualquer lugar", "gCheats.ElegyAnywhere",
                                { .tooltip = "Permite a Elegy of Emptiness fora de Ikana" });
        UIWidgets::CVarCombobox(
            "Parar o tempo nas masmorras", "gCheats.TempleTimeStop", timeStopOptions,
            { .tooltip = "Para o tempo nas áreas escolhidas. Vale ao mudar de sala.\n\n- Desligado: como no "
                         "original.\n- Templos: para o tempo nos templos de Woodfall, Snowhead, Great Bay e Stone "
                         "Tower.\n- Templos + minimasmorras: além dos templos, para o tempo nas duas Spider Houses, "
                         "Pirate's Fortress, Beneath the Well, Ancient Castle of Ikana e Secret Shrine.",
              .defaultIndex = TIME_STOP_OFF });

        ImGui::EndMenu();
    }
}

extern std::shared_ptr<Ship::GuiWindow> mStatsWindow;
extern std::shared_ptr<Ship::GuiWindow> mConsoleWindow;
extern std::shared_ptr<Ship::GuiWindow> mGfxDebuggerWindow;
extern std::shared_ptr<SaveEditorWindow> mSaveEditorWindow;
extern std::shared_ptr<ActorViewerWindow> mActorViewerWindow;
extern std::shared_ptr<CollisionViewerWindow> mCollisionViewerWindow;
extern std::shared_ptr<EventLogWindow> mEventLogWindow;

const char* logLevels[] = {
    "trace", "debug", "info", "warn", "error", "critical", "off",
};

void DrawDeveloperToolsMenu() {
    if (UIWidgets::BeginMenu("Ferramentas de desenvolvedor", UIWidgets::Colors::Yellow)) {
        if (UIWidgets::CVarCheckbox("Modo debug", "gDeveloperTools.DebugEnabled",
                                    { .tooltip = "Ativa o modo debug, que mostra opções de desenvolvedor e permite "
                                                 "abrir a seleção de mapa com L + R + Z" })) {
            // If disabling debug mode, disable all debug features
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
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f),
                               "Aviso: algumas destas opções podem quebrar o jogo\nou causar comportamentos estranhos. "
                               "Desligue-as\nantes de relatar qualquer bug.");
            ImGui::EndTooltip();
        }

        if (CVarGetInteger("gDeveloperTools.DebugEnabled", 0)) {
            UIWidgets::CVarCheckbox(
                "Seleção de mapa melhorada", "gDeveloperTools.BetterMapSelect.Enabled",
                { .tooltip = "Troca a seleção de mapa original por uma versão traduzida e mais fácil de usar." });

            if (UIWidgets::CVarCombobox(
                    "Tipo de save no modo debug", "gDeveloperTools.DebugSaveFileMode", debugSaveOptions,
                    { .tooltip = "Muda como os saves são criados com o modo debug ligado:\n\n- Save vazio: o save "
                                 "padrão com 3 corações no primeiro ciclo\n- Save de debug original: usa os dados da "
                                 "tela de título (8 corações, todos os itens e máscaras)\n- Save 100\%: todos os "
                                 "itens, equipamentos, máscaras, status e Bombers' Notebook completos",
                      .defaultIndex = DEBUG_SAVE_INFO_NONE })) {
                RegisterDebugSaveCreate();
            }

            if (UIWidgets::CVarCheckbox("Impedir atualização de atores", "gDeveloperTools.PreventActorUpdate")) {
                RegisterPreventActorUpdateHooks();
            }
            if (UIWidgets::CVarCheckbox("Impedir desenho de atores", "gDeveloperTools.PreventActorDraw")) {
                RegisterPreventActorDrawHooks();
            }
            if (UIWidgets::CVarCheckbox("Impedir criação de atores", "gDeveloperTools.PreventActorInit")) {
                RegisterPreventActorInitHooks();
            }
            UIWidgets::CVarCheckbox("Desativar dependência de objetos", "gDeveloperTools.DisableObjectDependency");

            if (UIWidgets::CVarCombobox(
                    "Nível de log", "gDeveloperTools.LogLevel", logLevels,
                    {
                        .tooltip =
                            "O nível de log define quais mensagens aparecem no console. Não afeta o arquivo de log",
                        .defaultIndex = 1,
                    })) {
                Ship::Context::GetInstance()->GetLogger()->set_level(
                    (spdlog::level::level_enum)CVarGetInteger("gDeveloperTools.LogLevel", 1));
            }

            if (gPlayState != NULL) {
                ImGui::Separator();
                UIWidgets::Checkbox(
                    "Avançar quadro a quadro", (bool*)&gPlayState->frameAdvCtx.enabled,
                    { .tooltip = "Permite avançar o jogo um quadro de cada vez. Para avançar um quadro, segure Z e "
                                 "toque R no segundo controle. Segurando Z e R avança um quadro a cada meio segundo. "
                                 "Também dá para usar os botões abaixo." });
                if (gPlayState->frameAdvCtx.enabled) {
                    if (UIWidgets::Button("Avançar 1", { .size = UIWidgets::Sizes::Inline })) {
                        CVarSetInteger("gDeveloperTools.FrameAdvanceTick", 1);
                    }
                    ImGui::SameLine();
                    UIWidgets::Button("Avançar (segurar)", { .size = UIWidgets::Sizes::Inline });
                    if (ImGui::IsItemActive()) {
                        CVarSetInteger("gDeveloperTools.FrameAdvanceTick", 1);
                    }
                }
            }
        }
        RenderWarpPointSection();
        ImGui::Separator();
        if (mCollisionViewerWindow) {
            UIWidgets::WindowButton("Visualizador de colisão", "gWindows.CollisionViewer", mCollisionViewerWindow,
                                    { .tooltip = "Desenha a colisão na tela" });
        }
        if (mStatsWindow) {
            UIWidgets::WindowButton(
                "Estatísticas", "gWindows.Stats", mStatsWindow,
                { .tooltip = "Mostra a janela de estatísticas, com o FPS, o tempo de cada quadro e o sistema em uso" });
        }
        if (mConsoleWindow) {
            UIWidgets::WindowButton(
                "Console", "gWindows.Console", mConsoleWindow,
                { .tooltip = "Abre a janela do console para digitar comandos; digite help para ver exemplos" });
        }
        if (mGfxDebuggerWindow) {
            UIWidgets::WindowButton("Depurador gráfico", "gWindows.GfxDebugger", mGfxDebuggerWindow,
                                    { .tooltip = "Abre a janela do depurador gráfico" });
        }
        if (mSaveEditorWindow) {
            UIWidgets::WindowButton(
                "Editor de save", "gWindows.SaveEditor", mSaveEditorWindow,
                { .tooltip = "Abre a janela do editor de save, para editar o seu arquivo de save" });
        }
        if (mActorViewerWindow) {
            UIWidgets::WindowButton(
                "Visualizador de atores", "gWindows.ActorViewer", mActorViewerWindow,
                { .tooltip = "Abre a janela do visualizador de atores, para ver os atores no mundo." });
        }
        if (mEventLogWindow) {
            UIWidgets::WindowButton("Registro de eventos", "gWindows.EventLog", mEventLogWindow);
        }
        ImGui::EndMenu();
    }
}

void BenMenuBar::InitElement() {
    UpdateWindowBackendObjects();
}

void BenMenuBar::DrawElement() {
    if (ImGui::BeginMenuBar()) {
        DrawMenuBarIcon();

        static ImVec2 sWindowPadding(8.0f, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sWindowPadding);

        ImGui::SetCursorPosY(0.0f);

        DrawBenMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawSettingsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawEnhancementsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawCheatsMenu();

        ImGui::SetCursorPosY(0.0f);

        DrawDeveloperToolsMenu();

        ImGui::SetCursorPosY(0.0f);

        ImGui::PopStyleVar(1);
        ImGui::EndMenuBar();
    }
}
} // namespace BenGui
// namespace BenGui
