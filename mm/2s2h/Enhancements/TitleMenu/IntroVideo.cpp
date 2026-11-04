#include "IntroVideo.h"

#ifndef __ANDROID__

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
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
#include <SDL2/SDL.h>

#include "2s2h/BenPort.h"
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
#include "TitleMenu.h"

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
// The moon (see DrawMoon): the whole transition, and the burst of light the menu comes out of (from then the menu
// takes input and builds its buttons up, TitleMenu.cpp's sBuild, as the light fades off it).
constexpr float kMoonTotal = 4.8f, kMoonReveal = 3.5f;
// The eyes in intro_moon.png (840 x 300), used only if the menu's picture cannot be found.
const ImVec2 kIntroEyes[] = { ImVec2(232.0f / 840, 165.0f / 300), ImVec2(604.0f / 840, 17.0f / 300) };

enum class Phase { Start, Logo, Video, Moon, Done };
Phase sPhase = Phase::Start;
float sTime = 0.0f;     // time in the phase
bool sSkippable = true;
float sHint = 0.0f;     // time the hint has been waited for
float sHold = 0.0f;     // how long a button, key or click has been held (hold to skip)
bool sReplay = false;   // after a reset: plays whatever "every launch" says
constexpr float kHold = 1.0f;

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
    // plm_frame_to_rgba writes only red, green and blue: the alpha bytes stay as they are, so they are made opaque
    // here once (a zero alpha drew the whole video invisible, the black under it was all that showed).
    size_t n = (size_t)frame->width * frame->height * 4;
    if (sRgba.size() != n) {
        sRgba.assign(n, 255);
    }
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
    // Nothing the clicks and keys did under the opening keeps a window busy: the game and the menu get the keyboard
    // and the controller again (ControlDeck blocks the keyboard while another window holds the active item).
    ImGui::ClearActiveID();
    ImGui::SetWindowFocus(nullptr);
    SPDLOG_INFO("Abertura: terminou");
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
    Intro_SetGameMuted(false); // the menu's music starts with the moon, fading in (BenPort.cpp, TitleMenu.cpp)
    SPDLOG_INFO("Abertura: transição da lua");
}

bool Start() {
    bool replay = sReplay;
    sReplay = false;
    sHint = 0.0f;
    sHold = 0.0f;
    if (CVarGetInteger("gEnhancements.TitleMenu.Intro", 1) == 0 ||
        (!replay && CVarGetInteger("gEnhancements.TitleMenu.IntroEveryLaunch", 1) == 0 &&
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

bool sPadHeld = false; // A, B, START... held on the game's controller, from the game frame

// Something held down: a key, a controller button, a mouse button or a finger.
bool SkipHeld() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDown[0] || io.MouseDown[1] || sPadHeld) {
        return true;
    }
    for (int k = ImGuiKey_Tab; k <= ImGuiKey_KeypadEqual; k++) {
        if (ImGui::IsKeyDown((ImGuiKey)k)) {
            return true;
        }
    }
    for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadR3; k++) {
        if (ImGui::IsKeyDown((ImGuiKey)k)) {
            return true;
        }
    }
    return false;
}

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

// The dark over everything (alpha at most), with a round opening at c of radius r and a soft edge f: a grid of
// cells, each corner as dark as its distance from c says.
void Cover(ImDrawList* dl, ImVec2 pos, ImVec2 size, ImVec2 c, float r, float f, float alpha) {
    const int cols = 48, rows = 27;
    auto dark = [&](float x, float y) {
        float d = std::sqrt((x - c.x) * (x - c.x) + (y - c.y) * (y - c.y));
        return IM_COL32(0, 0, 0, (int)(255 * alpha * Smooth((d - (r - f)) / f)));
    };
    for (int j = 0; j < rows; j++) {
        float y0 = pos.y + size.y * j / rows, y1 = pos.y + size.y * (j + 1) / rows;
        for (int i = 0; i < cols; i++) {
            float x0 = pos.x + size.x * i / cols, x1 = pos.x + size.x * (i + 1) / cols;
            ImU32 a = dark(x0, y0), b = dark(x1, y0), cc = dark(x1, y1), d = dark(x0, y1);
            if (((a | b | cc | d) & IM_COL32_A_MASK) == 0) {
                continue;
            }
            dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), a, b, cc, d);
        }
    }
}

ImVec2 Lerp(ImVec2 a, ImVec2 b, float t) {
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// One of the moon's eyes, opening: an almond of red light whose height grows with open (0 shut, 1 wide), a burning
// orange-yellow iris and a dark pupil, in a violet haze; g is how bright it burns.
void Eye(ImDrawList* dl, ImVec2 c, float r, float open, float g) {
    if (open <= 0.0f || g <= 0.0f) {
        return;
    }
    float h = 0.62f * open;
    Light(dl, c, r * 3.2f, 150, 50, 255, 0.30f * g * open); // violet haze
    const int steps = 10;
    for (int i = 0; i < steps; i++) {
        float k = 1.6f - 1.2f * i / steps;
        dl->AddEllipseFilled(c, ImVec2(r * k, r * k * h), IM_COL32(255, 40, 15, (int)(255 * std::min(1.0f, 0.12f * g))),
                             0.0f, 48);
    }
    dl->AddEllipseFilled(c, ImVec2(r * 0.62f, r * 0.62f * std::min(1.0f, h * 1.5f)),
                         IM_COL32(255, 150, 40, (int)(255 * std::min(1.0f, 0.9f * g))), 0.0f, 40);
    dl->AddEllipseFilled(c, ImVec2(r * 0.42f, r * 0.42f * std::min(1.0f, h * 1.5f)),
                         IM_COL32(255, 225, 120, (int)(255 * std::min(1.0f, 0.95f * g))), 0.0f, 40);
    dl->AddEllipseFilled(c, ImVec2(r * 0.16f, r * 0.16f * std::min(1.0f, h * 1.6f)),
                         IM_COL32(40, 8, 4, (int)(255 * std::min(1.0f, 0.9f * g))), 0.0f, 24);
}

// From total darkness the moon's eyes open, slowly, close up; they burn and pulse, lighting the moon a little
// around them; then the glow swells into a burst of light that fills the screen, and the title menu comes out of it
// (TitleMenu.cpp builds its buttons up as the light fades). The moon is the menu's own (TitleMenu::MenuScene), so
// its eyes are where the menu's are.
void DrawMoon(ImDrawList* dl, ImVec2 pos, ImVec2 size) {
    float t = sTime;
    ImVec2 full(pos.x + size.x, pos.y + size.y);
    // The light the menu comes out of: it rises to white-hot at kMoonReveal, then fades off the menu.
    float flash = t < kMoonReveal ? Smooth((t - (kMoonReveal - 0.6f)) / 0.6f)
                                  : 1.0f - Smooth((t - kMoonReveal) / (kMoonTotal - kMoonReveal));
    if (t >= kMoonReveal) {
        dl->AddRectFilled(pos, full, IM_COL32(255, 190, 150, (int)(255 * flash)));
        return;
    }

    // The moon close up, and its eyes.
    ImTextureID tex = nullptr;
    ImVec2 m0, m1, eyes[2];
    TitleMenu::Scene sc;
    float zoom = 1.0f + 0.05f * Smooth(t / kMoonReveal);
    if (TitleMenu::MenuScene(sc)) {
        ImVec2 focus((sc.moon0.x + sc.moon1.x) / 2, (sc.moon0.y + sc.moon1.y) / 2);
        float w = (sc.moon1.x - sc.moon0.x) * sc.k, h = (sc.moon1.y - sc.moon0.y) * sc.k;
        float z = std::min(size.x * 0.86f / w, size.y * 0.72f / h) * zoom;
        ImVec2 anchor(pos.x + size.x / 2, pos.y + size.y * 0.42f);
        auto S = [&](ImVec2 p) {
            return ImVec2(anchor.x + (p.x - focus.x) * sc.k * z, anchor.y + (p.y - focus.y) * sc.k * z);
        };
        tex = sc.moonTex;
        m0 = S(sc.moon0);
        m1 = S(sc.moon1);
        eyes[0] = S(sc.eyes[0]);
        eyes[1] = S(sc.eyes[1]);
    } else if (sMoon.im != nullptr) {
        float k = std::min(size.x * 0.84f / sMoon.w, size.y * 0.8f / sMoon.h) * zoom;
        ImVec2 s(sMoon.w * k, sMoon.h * k);
        tex = sMoon.im;
        m0 = ImVec2(pos.x + (size.x - s.x) / 2, pos.y + (size.y - s.y) / 2);
        m1 = ImVec2(m0.x + s.x, m0.y + s.y);
        for (int i = 0; i < 2; i++) {
            eyes[i] = ImVec2(m0.x + kIntroEyes[i].x * s.x, m0.y + kIntroEyes[i].y * s.y);
        }
    }
    dl->AddRectFilled(pos, full, IM_COL32(0, 0, 0, 255));
    if (tex != nullptr) {
        float mh = m1.y - m0.y;
        float open = Smooth((t - 0.5f) / 1.5f);                 // the eyes open
        float lit = Smooth((t - 1.2f) / 1.0f);                  // and light the moon a little
        float swell = Smooth((t - 2.3f) / (kMoonReveal - 2.3f)); // then the glow swells
        float pulse = 0.8f + 0.2f * std::sin(t * 3.14159265f * 2.0f * 1.4f);
        dl->AddImage(tex, m0, m1, ImVec2(0, 0), ImVec2(1, 1),
                     IM_COL32(255, 255, 255, (int)(255 * (0.08f + 0.27f * lit + 0.35f * swell))));
        for (int i = 0; i < 2; i++) {
            float r = mh * 0.1f * (1.0f + 0.5f * swell);
            // The far eye opens a moment after the near one.
            float o = Smooth((t - 0.5f - i * 0.25f) / 1.5f);
            Eye(dl, eyes[i], r, o, std::min(1.0f, (0.75f + 0.25f * pulse) * (0.6f + 0.4f * open) + swell));
            Light(dl, eyes[i], r * (2.0f + 10.0f * swell), 255, 120, 60, 0.6f * swell);
        }
    }
    if (flash > 0.0f) {
        dl->AddRectFilled(pos, full, IM_COL32(255, 190, 150, (int)(255 * flash)));
    }
}

} // namespace

// The menu takes input again once the dark starts to open from the moon.
bool Active() {
    return sPhase != Phase::Done && !(sPhase == Phase::Moon && sTime >= kMoonReveal);
}

bool Draw() {
    if (sPhase == Phase::Done) {
        return false;
    }
    if (sPhase == Phase::Start && !Start()) {
        sPhase = Phase::Done;
        return false;
    }
    // Esc (or the controller's Back) held to skip also opens the settings menu, which then stayed open, unseen,
    // under the opening and kept the title menu from every key and button: it stays shut while the opening is on.
    if (auto menu = Ship::Context::GetInstance()->GetWindow()->GetGui()->GetMenu(); menu != nullptr && menu->IsVisible()) {
        menu->ToggleVisibility();
        SPDLOG_INFO("Abertura: o menu de configurações abriu (Esc) e foi fechado");
    }
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
    sTime += dt;
    // Hold to skip: a second of holding anything goes to the moon.
    if (sSkippable && sPhase != Phase::Moon && SkipHeld()) {
        sHold += dt;
        if (sHold >= kHold) {
            sHold = 0.0f;
            ExitToMenu();
        }
    } else {
        sHold = std::max(0.0f, sHold - dt * 4.0f);
    }

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
            // The end of the file, or, should the decoder never say so, well past its length.
            if (plm_has_ended(sPlm) || sTime > plm_get_duration(sPlm) + 5.0) {
                ExitToMenu();
                shown = nullptr;
            }
            break;
        }
        case Phase::Moon: {
            if (sTime >= kMoonTotal) {
                End();
                return false;
            }
            cover = 0.0f; // DrawMoon draws the dark, opening from the moon
            break;
        }
        default:
            break;
    }

    if (cover > 0.0f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, (int)(255 * cover)));
    }
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
        float ha = sHold > 0.0f ? 1.0f : std::clamp((sHint - kHintDelay) / 0.5f, 0.0f, 1.0f) * 0.8f;
        const char* hint = "Segure qualquer botão para pular";
        float fs = 24.0f * scale;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, hint);
        ImVec2 p(pos.x + size.x - ts.x - 32 * scale, pos.y + 20 * scale);
        dl->AddText(font, fs, ImVec2(p.x + 1.5f * scale, p.y + 1.5f * scale), IM_COL32(0, 0, 0, (int)(255 * ha)),
                    hint);
        dl->AddText(font, fs, p, IM_COL32(235, 225, 245, (int)(255 * ha)), hint);
        // The bar under it fills while something is held.
        if (sHold > 0.0f) {
            float y = p.y + ts.y + 6 * scale;
            float w = ts.x * std::clamp(sHold / kHold, 0.0f, 1.0f);
            dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + ts.x, y + 3 * scale), IM_COL32(255, 255, 255, 50));
            dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + 3 * scale), IM_COL32(255, 80, 60, 230));
        }
    }
    return true;
}

void Replay() {
    if (sPhase == Phase::Done) {
        sReplay = true;
        sPhase = Phase::Start;
    }
}

} // namespace IntroVideo

#else // __ANDROID__: IntroPlayer.java plays the opening; the game only knows it is on while its sound is off.

#include <jni.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_system.h>
#include "2s2h/GameInteractor/GameInteractor.h"
#include "2s2h/ShipInit.hpp"
extern "C" {
#include <z64.h>
#include "macros.h"
#include "variables.h"
}
extern "C" bool Intro_OnScreen();

namespace IntroVideo {
namespace {
bool sPadHeld = false;
}
bool Draw() {
    return false;
}
bool Active() {
    return Intro_OnScreen();
}
void Replay() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "ReplayIntro", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    } else {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
}
} // namespace IntroVideo

#endif

// Nothing the controller does reaches the game under the opening (PC: held, it skips it).
static void RegisterIntroVideo() {
    COND_HOOK(OnGameStateMainStart, true, []() {
        if (gGameState == nullptr) {
            return;
        }
        Input* input = CONTROLLER1(gGameState);
        if (!IntroVideo::Active()) {
            IntroVideo::sPadHeld = false;
            return;
        }
        IntroVideo::sPadHeld = input->cur.button != 0;
        input->press.button = 0;
        input->cur.button = 0;
        input->rel.button = 0;
        input->cur.stick_x = input->cur.stick_y = 0;
        input->press.stick_x = input->press.stick_y = 0;
        input->rel.stick_x = input->rel.stick_y = 0;
    });
}

static RegisterShipInitFunc sIntroVideoInit(RegisterIntroVideo, {});
