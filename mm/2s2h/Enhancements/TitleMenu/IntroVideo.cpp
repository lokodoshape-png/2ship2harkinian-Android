#include "IntroVideo.h"

#ifndef __ANDROID__

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <imgui.h>
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
#include <SDL2/SDL.h>

#include "2s2h/BenPort.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"

extern "C" {
#include "pl_mpeg.h"
#include <z64.h>
#include "macros.h"
#include "variables.h"
}

// BenPort.cpp: silences the game's sound (and fades it back in after).
extern "C" void Intro_SetGameMuted(bool muted);

namespace IntroVideo {
namespace {

constexpr float kLogoIn = 1.5f, kLogoHold = 3.0f, kLogoOut = 1.0f, kHintDelay = 1.2f;
// The moon (see DrawMoon): the whole transition, and when the dark starts to lift off the menu.
constexpr float kMoonTotal = 4.6f, kMoonReveal = 3.9f;
// The eyes in intro_moon.png (840 x 300).
const ImVec2 kEyes[] = { ImVec2(232, 165), ImVec2(604, 17) };

enum class Phase { Start, Logo, Video, Moon, Done };
Phase sPhase = Phase::Start;
float sTime = 0.0f;     // time in the phase
bool sSkippable = true;
float sHint = 0.0f;     // time the hint has been waited for

struct Tex {
    uint32_t id = 0;
    ImTextureID im = nullptr;
    int w = 0, h = 0;
};
Tex sLogo, sFrame, sMoon;

plm_t* sPlm = nullptr;
SDL_AudioDeviceID sAudio = 0;
std::vector<uint8_t> sRgba;
bool sNewFrame = false;
double sVideoTime = 0.0;

struct Cue {
    double start, end;
    std::string text;
};
std::vector<Cue> sCues;

Fast::GfxRenderingAPI* Api() {
    auto window = std::dynamic_pointer_cast<Fast::Fast3dWindow>(Ship::Context::GetInstance()->GetWindow());
    if (window == nullptr) {
        return nullptr;
    }
    auto interpreter = window->GetInterpreterWeak().lock();
    return interpreter != nullptr ? interpreter->GetCurrentRenderingAPI() : nullptr;
}

// Sends pixels to a texture, made on first use, with linear filtering (see TitleMenu.cpp's Load).
void Upload(Tex& tex, const uint8_t* rgba, int w, int h) {
    Fast::GfxRenderingAPI* api = Api();
    if (api == nullptr) {
        return;
    }
    Fast::FilteringMode filter = api->GetTextureFilter();
    if (filter != Fast::FILTER_LINEAR) {
        api->SetTextureFilter(Fast::FILTER_LINEAR);
    }
    if (tex.im == nullptr) {
        tex.id = api->NewTexture();
    }
    api->SelectTexture(0, tex.id);
    api->SetSamplerParameters(0, true, G_TX_CLAMP, G_TX_CLAMP);
    api->UploadTexture(rgba, w, h);
    if (filter != Fast::FILTER_LINEAR) {
        api->SetTextureFilter(filter);
    }
    tex.im = api->GetTextureById(tex.id);
    tex.w = w;
    tex.h = h;
}

void Free(Tex& tex) {
    Fast::GfxRenderingAPI* api = Api();
    if (api != nullptr && tex.im != nullptr) {
        api->DeleteTexture(tex.id);
    }
    tex = Tex();
}

bool LoadImage(const std::string& path, Tex& out) {
    auto context = Ship::Context::GetInstance();
    auto archives = context->GetResourceManager()->GetArchiveManager();
    if (archives == nullptr || !archives->HasFile(path)) {
        return false;
    }
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(RESOURCE_TYPE_GUI_TEXTURE);
    initData->ResourceVersion = 0;
    initData->Path = path;
    auto image =
        std::static_pointer_cast<Ship::GuiTexture>(context->GetResourceManager()->LoadResource(path, false, initData));
    if (image == nullptr || image->Data == nullptr) {
        return false;
    }
    Upload(out, image->Data, image->Metadata.Width, image->Metadata.Height);
    context->GetResourceManager()->UnloadResource(path);
    return out.im != nullptr;
}

std::string Beside(const char* name) {
    return Ship::Context::LocateFileAcrossAppDirs(name);
}

void LoadCues() {
    std::ifstream in(Beside("abertura.srt"), std::ios::binary);
    if (!in) {
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string srt = ss.str();
    srt.erase(std::remove(srt.begin(), srt.end(), '\r'), srt.end());
    if (srt.rfind("\xEF\xBB\xBF", 0) == 0) {
        srt.erase(0, 3);
    }
    std::regex time(R"((\d+):(\d+):(\d+)[,.](\d+)\s*-->\s*(\d+):(\d+):(\d+)[,.](\d+))");
    std::istringstream lines(srt);
    std::string line;
    Cue* cue = nullptr;
    while (std::getline(lines, line)) {
        std::smatch m;
        if (std::regex_search(line, m, time)) {
            auto sec = [&](int g) {
                return std::stoi(m[g]) * 3600.0 + std::stoi(m[g + 1]) * 60.0 + std::stoi(m[g + 2]) +
                       std::stoi(m[g + 3]) / 1000.0;
            };
            sCues.push_back({ sec(1), sec(5), "" });
            cue = &sCues.back();
        } else if (line.empty()) {
            cue = nullptr;
        } else if (cue != nullptr) {
            cue->text += (cue->text.empty() ? "" : "\n") + line;
        }
    }
}

void OnVideo(plm_t*, plm_frame_t* frame, void*) {
    sRgba.resize((size_t)frame->width * frame->height * 4);
    plm_frame_to_rgba(frame, sRgba.data(), frame->width * 4);
    sNewFrame = true;
    sVideoTime = frame->time;
}

void OnAudio(plm_t*, plm_samples_t* samples, void*) {
    if (sAudio != 0) {
        SDL_QueueAudio(sAudio, samples->interleaved, samples->count * 2 * sizeof(float));
    }
}

bool OpenVideo() {
    std::string path = Beside("abertura.mpg");
    sPlm = plm_create_with_filename(path.c_str());
    if (sPlm == nullptr || !plm_probe(sPlm, 5000 * 1024) || plm_get_num_video_streams(sPlm) == 0) {
        SPDLOG_INFO("Abertura: sem o vídeo {}", path);
        if (sPlm != nullptr) {
            plm_destroy(sPlm);
            sPlm = nullptr;
        }
        return false;
    }
    plm_set_video_decode_callback(sPlm, OnVideo, nullptr);
    plm_set_audio_decode_callback(sPlm, OnAudio, nullptr);
    plm_set_loop(sPlm, 0);
    if (plm_get_num_audio_streams(sPlm) > 0) {
        if (!SDL_WasInit(SDL_INIT_AUDIO)) {
            SDL_InitSubSystem(SDL_INIT_AUDIO);
        }
        SDL_AudioSpec want{}, have{};
        want.freq = plm_get_samplerate(sPlm);
        want.format = AUDIO_F32;
        want.channels = 2;
        want.samples = 4096;
        sAudio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (sAudio != 0) {
            plm_set_audio_enabled(sPlm, 1);
            plm_set_audio_stream(sPlm, 0);
            plm_set_audio_lead_time(sPlm, (double)want.samples / want.freq);
            SDL_PauseAudioDevice(sAudio, 0);
        } else {
            plm_set_audio_enabled(sPlm, 0);
        }
    } else {
        plm_set_audio_enabled(sPlm, 0);
    }
    SPDLOG_INFO("Abertura: {} ({}x{})", path, plm_get_width(sPlm), plm_get_height(sPlm));
    return true;
}

void CloseVideo() {
    if (sAudio != 0) {
        SDL_ClearQueuedAudio(sAudio);
        SDL_CloseAudioDevice(sAudio);
        sAudio = 0;
    }
    if (sPlm != nullptr) {
        plm_destroy(sPlm);
        sPlm = nullptr;
    }
    sRgba.clear();
    sRgba.shrink_to_fit();
}

void End() {
    CloseVideo();
    Free(sLogo);
    Free(sFrame);
    Free(sMoon);
    sCues.clear();
    sPhase = Phase::Done;
    Intro_SetGameMuted(false);
}

// The one way into the title menu, from every way out of the opening (the video ended, a skip during the logo or the
// video, a video that could not be read): the video is stopped and let go, the screen goes dark, and the moon's red
// eyes burn, pulse and go out before the menu fades in. Only the first call counts.
void ExitToMenu() {
    if (sPhase == Phase::Moon || sPhase == Phase::Done) {
        return;
    }
    CloseVideo();
    Free(sFrame);
    Free(sLogo);
    sPhase = Phase::Moon;
    sTime = 0.0f;
}

bool Start() {
    if (CVarGetInteger("gEnhancements.TitleMenu.Intro", 1) == 0 ||
        (CVarGetInteger("gEnhancements.TitleMenu.IntroEveryLaunch", 1) == 0 &&
         CVarGetInteger("gEnhancements.TitleMenu.IntroPlayed", 0) != 0)) {
        return false;
    }
    bool video = OpenVideo();
    bool logo = LoadImage("overlays/titlemenu/intro_logo.jpg", sLogo);
    LoadImage("overlays/titlemenu/intro_moon.png", sMoon);
    if (!video && !logo) {
        return false;
    }
    if (video) {
        LoadCues();
    }
    sSkippable = CVarGetInteger("gEnhancements.TitleMenu.IntroSkippable", 1) != 0;
    if (CVarGetInteger("gEnhancements.TitleMenu.IntroPlayed", 0) == 0) {
        CVarSetInteger("gEnhancements.TitleMenu.IntroPlayed", 1);
        Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    Intro_SetGameMuted(true);
    sPhase = logo ? Phase::Logo : Phase::Video;
    sTime = 0.0f;
    return true;
}

bool SkipPressed() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseClicked[0] || io.MouseClicked[1]) {
        return true;
    }
    for (int k = ImGuiKey_Tab; k <= ImGuiKey_KeypadEqual; k++) {
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
            return true;
        }
    }
    for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadR3; k++) {
        if (ImGui::IsKeyPressed((ImGuiKey)k, false)) {
            return true;
        }
    }
    return false;
}

bool sPadSkip = false; // A, B, START... on the game's controller, from the game frame

// The whole picture inside the window at its own proportions.
void Fit(ImDrawList* dl, const Tex& tex, ImVec2 pos, ImVec2 size, float a) {
    if (tex.im == nullptr || tex.w == 0 || tex.h == 0) {
        return;
    }
    float k = std::min(size.x / tex.w, size.y / tex.h);
    ImVec2 s(tex.w * k, tex.h * k);
    ImVec2 p0(pos.x + (size.x - s.x) / 2, pos.y + (size.y - s.y) / 2);
    dl->AddImage(tex.im, p0, ImVec2(p0.x + s.x, p0.y + s.y), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * std::clamp(a, 0.0f, 1.0f))));
}

float Smooth(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v * v * (3.0f - 2.0f * v);
}

// A soft round light: rings from the edge in, each adding a little, so the middle is brightest.
void Light(ImDrawList* dl, ImVec2 c, float r, int red, int green, int blue, float a) {
    if (a <= 0.003f) {
        return;
    }
    const int steps = 18;
    for (int i = 0; i < steps; i++) {
        float k = 1.0f - (float)i / steps;
        dl->AddCircleFilled(c, r * k, IM_COL32(red, green, blue, (int)(255 * std::clamp(a * 2.2f / steps, 0.0f, 1.0f))),
                            48);
    }
}

// The moon close up, from the dark; its eyes kindle, burn red and violet and pulse, then dim and go out.
void DrawMoon(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    float t = sTime;
    if (sMoon.im == nullptr || t >= kMoonReveal) {
        return;
    }
    float moonA = Smooth((t - 0.4f) / 1.0f) * (1.0f - Smooth((t - 3.2f) / 0.7f));
    float eyes = Smooth((t - 1.0f) / 0.8f) * (1.0f - Smooth((t - 2.8f) / 0.9f));
    float pulse = 0.72f + 0.28f * std::sin(t * 3.14159265f * 2.0f * 1.6f);
    float zoom = 1.0f + 0.06f * std::clamp(t / kMoonTotal, 0.0f, 1.0f);
    float k = std::min(size.x * 0.96f / sMoon.w, size.y * 0.9f / sMoon.h) * zoom;
    ImVec2 s(sMoon.w * k, sMoon.h * k);
    ImVec2 p0(pos.x + (size.x - s.x) / 2, pos.y + (size.y - s.y) / 2);
    ImVec2 mid(pos.x + size.x / 2, pos.y + size.y / 2);
    Light(dl, mid, std::max(s.x, s.y) * 0.7f, 110, 35, 190, 0.4f * moonA * (0.6f + 0.4f * eyes)); // violet haze
    dl->AddImage(sMoon.im, p0, ImVec2(p0.x + s.x, p0.y + s.y), ImVec2(0, 0), ImVec2(1, 1),
                 IM_COL32(255, 255, 255, (int)(255 * moonA)));
    float g = eyes * pulse;
    if (g <= 0.0f) {
        return;
    }
    for (const ImVec2& e : kEyes) {
        ImVec2 c(p0.x + e.x * k, p0.y + e.y * k);
        float r = s.y * 0.5f;
        Light(dl, c, r * 1.6f, 160, 55, 255, 0.35f * g); // violet
        Light(dl, c, r, 255, 35, 15, 0.8f * g);          // red
        Light(dl, c, r * 0.22f, 255, 200, 140, 0.9f * g); // the burning middle
    }
}

} // namespace

bool Active() {
    return sPhase != Phase::Done;
}

bool Draw() {
    if (sPhase == Phase::Done) {
        return false;
    }
    if (sPhase == Phase::Start && !Start()) {
        sPhase = Phase::Done;
        return false;
    }
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    sTime += dt;
    if (sSkippable && sPhase != Phase::Moon && (SkipPressed() || sPadSkip)) {
        ExitToMenu();
    }
    sPadSkip = false;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 pos = vp->Pos, size = vp->Size;
    ImDrawList* dl = ImGui::GetForegroundDrawList(vp);
    float cover = 1.0f; // the black behind everything
    float picture = 1.0f;
    const Tex* shown = nullptr;

    switch (sPhase) {
        case Phase::Logo: {
            shown = &sLogo;
            if (sTime < kLogoIn) {
                picture = sTime / kLogoIn;
            } else if (sTime < kLogoIn + kLogoHold) {
                picture = 1.0f;
            } else {
                picture = 1.0f - (sTime - kLogoIn - kLogoHold) / kLogoOut;
            }
            if (sTime >= kLogoIn + kLogoHold + kLogoOut) {
                Free(sLogo);
                if (sPlm != nullptr) {
                    sPhase = Phase::Video;
                    sTime = 0.0f;
                    shown = nullptr;
                } else {
                    ExitToMenu();
                    shown = nullptr;
                }
            }
            break;
        }
        case Phase::Video: {
            plm_decode(sPlm, dt);
            if (sNewFrame) {
                sNewFrame = false;
                Upload(sFrame, sRgba.data(), plm_get_width(sPlm), plm_get_height(sPlm));
            }
            shown = &sFrame;
            if (plm_has_ended(sPlm)) {
                ExitToMenu();
                shown = nullptr;
            }
            break;
        }
        case Phase::Moon: {
            if (sTime >= kMoonReveal) {
                Intro_SetGameMuted(false); // the menu's sound fades in with its picture
            }
            if (sTime >= kMoonTotal) {
                End();
                return false;
            }
            cover = 1.0f - Smooth((sTime - kMoonReveal) / (kMoonTotal - kMoonReveal));
            break;
        }
        default:
            break;
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, (int)(255 * cover)));
    if (shown != nullptr && shown->im != nullptr) {
        Fit(dl, *shown, pos, size, picture);
    }
    if (sPhase == Phase::Moon) {
        DrawMoon(dl, pos, size);
    }
    ImFont* font = OTRGlobals::Instance != nullptr ? OTRGlobals::Instance->fontQuestTitle : nullptr;
    if (font == nullptr) {
        font = ImGui::GetFont();
    }
    float scale = size.y / 1080.0f;
    // Subtitles: white on a light shadow, near the bottom.
    if (sPhase == Phase::Video) {
        for (const Cue& c : sCues) {
            if (sVideoTime >= c.start && sVideoTime < c.end) {
                float fs = 40.0f * scale;
                ImVec2 ts = font->CalcTextSizeA(fs, size.x * 0.8f, size.x * 0.8f, c.text.c_str());
                ImVec2 p(pos.x + (size.x - ts.x) / 2, pos.y + size.y - 70.0f * scale - ts.y);
                dl->AddRectFilled(ImVec2(p.x - 16 * scale, p.y - 6 * scale),
                                  ImVec2(p.x + ts.x + 16 * scale, p.y + ts.y + 6 * scale), IM_COL32(0, 0, 0, 90),
                                  6 * scale);
                for (int i = 0; i < 4; i++) {
                    ImVec2 o(((i & 1) ? 2.0f : -2.0f) * scale, ((i & 2) ? 2.0f : -2.0f) * scale);
                    dl->AddText(font, fs, ImVec2(p.x + o.x, p.y + o.y), IM_COL32(0, 0, 0, 200), c.text.c_str(),
                                nullptr, size.x * 0.8f);
                }
                dl->AddText(font, fs, p, IM_COL32(255, 255, 255, 255), c.text.c_str(), nullptr, size.x * 0.8f);
                break;
            }
        }
    }
    // The hint, small in the corner.
    if (sSkippable && sPhase != Phase::Moon) {
        sHint += dt;
        float ha = std::clamp((sHint - kHintDelay) / 0.5f, 0.0f, 1.0f) * 0.8f;
        const char* hint = "Pressione qualquer botão para pular";
        float fs = 24.0f * scale;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, hint);
        ImVec2 p(pos.x + size.x - ts.x - 32 * scale, pos.y + size.y - ts.y - 20 * scale);
        dl->AddText(font, fs, ImVec2(p.x + 1.5f * scale, p.y + 1.5f * scale), IM_COL32(0, 0, 0, (int)(255 * ha)),
                    hint);
        dl->AddText(font, fs, p, IM_COL32(235, 225, 245, (int)(255 * ha)), hint);
    }
    return true;
}

} // namespace IntroVideo

// A, B, START or any other button on the game's controller skips it, and nothing reaches the game meanwhile.
static void RegisterIntroVideo() {
    COND_HOOK(OnGameStateMainStart, true, []() {
        if (!IntroVideo::Active() || gGameState == nullptr) {
            return;
        }
        Input* input = CONTROLLER1(gGameState);
        if (input->press.button != 0) {
            IntroVideo::sPadSkip = true;
        }
        input->press.button = 0;
        input->cur.button = 0;
        input->rel.button = 0;
    });
}

static RegisterShipInitFunc sIntroVideoInit(RegisterIntroVideo, {});

#else // __ANDROID__: IntroPlayer.java plays the opening.

namespace IntroVideo {
bool Draw() {
    return false;
}
bool Active() {
    return false;
}
} // namespace IntroVideo

#endif
