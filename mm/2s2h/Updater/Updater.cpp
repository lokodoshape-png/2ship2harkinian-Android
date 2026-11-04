#include "Updater.h"

#include "2s2h/BenGui/Translation.h"
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/window/Window.h>

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

const wchar_t* kApiUrl = L"https://api.github.com/repos/lokodoshape-png/MajorasMaskDefinitive/releases?per_page=30";
const char* kTagPrefix = "pc-4.0-build-";
const wchar_t* kGameExe = L"Majoras-Mask-Android-Definitive-Edition.exe";

#ifdef MMADE_PC_BUILD
constexpr long kThisBuild = MMADE_PC_BUILD;
#else
constexpr long kThisBuild = -1; // not a release build: never offer updates
#endif

enum class State { Idle, Checking, UpToDate, Available, Downloading, Installing, Failed };

std::atomic<State> sState = State::Idle;
std::atomic<uint64_t> sDone = 0, sTotal = 0;
std::mutex sMutex;
std::string sTag, sZipUrl, sError;
long sNewBuild = -1;
bool sDismissed = false;
std::atomic<bool> sReadyToClose = false;

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// GET over HTTPS, following GitHub's redirect to its download host. `sink` gets the body in chunks.
bool HttpGet(const std::wstring& url, const std::function<bool(const char*, DWORD, uint64_t)>& sink) {
    URL_COMPONENTS parts = { sizeof(parts) };
    wchar_t host[256] = {}, path[4096] = {}, extra[4096] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 4096;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        return false;
    }
    std::wstring fullPath = std::wstring(path) + extra;

    HINTERNET session = WinHttpOpen(L"MajorasMaskDefinitive-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        session = WinHttpOpen(L"MajorasMaskDefinitive-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
        return false;
    }
    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    if (request != nullptr) {
        if (WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1L,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            wchar_t lengthText[32] = {};
            DWORD lengthSize = sizeof(lengthText);
            uint64_t total = 0;
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText,
                                    &lengthSize, WINHTTP_NO_HEADER_INDEX)) {
                total = _wcstoui64(lengthText, nullptr, 10);
            }
            if (status == 200) {
                ok = true;
                std::vector<char> buffer(1 << 16);
                for (;;) {
                    DWORD read = 0;
                    if (!WinHttpReadData(request, buffer.data(), (DWORD)buffer.size(), &read)) {
                        ok = false;
                        break;
                    }
                    if (read == 0) {
                        break;
                    }
                    if (!sink(buffer.data(), read, total)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        WinHttpCloseHandle(request);
    }
    if (connect != nullptr) {
        WinHttpCloseHandle(connect);
    }
    WinHttpCloseHandle(session);
    return ok;
}

// The newest "pc-4.0-build-N" release with a Windows zip. Plain string searches are enough for GitHub's fixed
// field names.
bool FindNewestRelease(long& build, std::string& tag, std::string& zipUrl) {
    std::string json;
    if (!HttpGet(kApiUrl, [&](const char* data, DWORD n, uint64_t) {
            json.append(data, n);
            return true;
        })) {
        return false;
    }
    build = -1;
    const std::string tagKey = "\"tag_name\":";
    const std::string urlKey = "\"browser_download_url\":";
    size_t pos = 0;
    while ((pos = json.find(tagKey, pos)) != std::string::npos) {
        size_t q1 = json.find('"', pos + tagKey.size());
        size_t q2 = q1 == std::string::npos ? q1 : json.find('"', q1 + 1);
        if (q2 == std::string::npos) {
            break;
        }
        std::string name = json.substr(q1 + 1, q2 - q1 - 1);
        size_t next = json.find(tagKey, q2);
        pos = q2;
        if (name.rfind(kTagPrefix, 0) != 0) {
            continue;
        }
        long number = std::strtol(name.c_str() + std::strlen(kTagPrefix), nullptr, 10);
        if (number <= build) {
            continue;
        }
        // GitHub lists a release's assets after its tag name, so stay before the next release.
        std::string body = json.substr(q2, (next == std::string::npos ? json.size() : next) - q2);
        size_t u = 0;
        while ((u = body.find(urlKey, u)) != std::string::npos) {
            size_t a = body.find('"', u + urlKey.size());
            size_t b = a == std::string::npos ? a : body.find('"', a + 1);
            if (b == std::string::npos) {
                break;
            }
            std::string asset = body.substr(a + 1, b - a - 1);
            u = b;
            if (asset.find("-windows-") != std::string::npos && asset.size() > 4 &&
                asset.compare(asset.size() - 4, 4, ".zip") == 0) {
                build = number;
                tag = name;
                zipUrl = asset;
                break;
            }
        }
    }
    return build >= 0;
}

bool RunHidden(const std::wstring& commandLine) {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = commandLine;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

// Windows 10 and 11 ship tar.exe, which reads zip files; PowerShell is the fallback.
bool Extract(const fs::path& zip, const fs::path& to) {
    wchar_t system[MAX_PATH];
    GetSystemDirectoryW(system, MAX_PATH);
    fs::path tar = fs::path(system) / L"tar.exe";
    if (fs::exists(tar) &&
        RunHidden(L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + to.wstring() + L"\"")) {
        return true;
    }
    return RunHidden(L"powershell -NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -Force -LiteralPath '" +
                     zip.wstring() + L"' -DestinationPath '" + to.wstring() + L"'\"");
}

fs::path GameDir() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    return fs::path(self).parent_path();
}

void Fail(const char* message) {
    std::lock_guard<std::mutex> lock(sMutex);
    sError = message;
    sState = State::Failed;
    SPDLOG_ERROR("Updater: {}", message);
}

void CheckThread() {
    long build;
    std::string tag, url;
    if (!FindNewestRelease(build, tag, url)) {
        SPDLOG_INFO("Updater: could not reach GitHub");
        sState = State::Idle;
        return;
    }
    SPDLOG_INFO("Updater: this is build {}, newest is {}", kThisBuild, tag);
    std::lock_guard<std::mutex> lock(sMutex);
    sTag = tag;
    sZipUrl = url;
    sNewBuild = build;
    sState = build > kThisBuild ? State::Available : State::UpToDate;
}

// Downloads and unpacks the zip, then writes the script that copies it over the game folder once the game has
// closed, and asks the game to close.
void DownloadThread() {
    std::string url, tag;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        url = sZipUrl;
        tag = sTag;
    }
    wchar_t tempBase[MAX_PATH];
    GetTempPathW(MAX_PATH, tempBase);
    fs::path work = fs::path(tempBase) / L"MajorasMaskDefinitive-update";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work / L"files", ec);
    fs::path zip = work / L"update.zip";

    sDone = 0;
    sTotal = 0;
    {
        std::ofstream out(zip, std::ios::binary);
        bool ok = HttpGet(Widen(url), [&](const char* data, DWORD n, uint64_t total) {
            out.write(data, n);
            sDone += n;
            sTotal = total;
            return (bool)out;
        });
        if (!ok) {
            Fail("Could not download the update. Check the internet connection and try again.");
            return;
        }
    }
    sState = State::Installing;
    if (!Extract(zip, work / L"files")) {
        Fail("Could not unpack the update.");
        return;
    }
    fs::remove(zip, ec);

    // The zip may hold the files at its root or inside one folder; the root is where the game executable is.
    fs::path root;
    for (auto it = fs::recursive_directory_iterator(work / L"files", ec); it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        if (_wcsicmp(it->path().filename().c_str(), kGameExe) == 0) {
            root = it->path().parent_path();
            break;
        }
    }
    if (root.empty()) {
        Fail("The downloaded update has no game executable.");
        return;
    }

    fs::path dir = GameDir();
    DWORD pid = GetCurrentProcessId();
    std::string r = Narrow(root.wstring()), d = Narrow(dir.wstring()), w = Narrow(work.wstring());
    std::string exe = Narrow(kGameExe);
    fs::path script = fs::path(tempBase) / L"MajorasMaskDefinitive-update.bat";
    {
        std::ofstream bat(script, std::ios::binary);
        bat << "@echo off\r\n"
               "chcp 65001 >nul\r\n"
               "title Majoras Mask Android Definitive Edition\r\n"
               "echo Instalando a "
            << tag << "...\r\n"
            << ":wait\r\n"
               "tasklist /FI \"PID eq "
            << pid << "\" /NH 2>nul | find \" " << pid
            << " \" >nul\r\n"
               "if not errorlevel 1 (\r\n"
               "  timeout /t 1 /nobreak >nul\r\n"
               "  goto wait\r\n"
               ")\r\n"
               // The player's own files are never in the zip, but keep them out anyway.
               "robocopy \""
            << r << "\" \"" << d
            << "\" /E /R:10 /W:1 /NFL /NDL /NJH /NJS /NP /XF mm.o2r 2ship2harkinian.json imgui.ini /XD \"" << r
            << "\\mods\" \"" << r << "\\saves\" \"" << r << "\\shadercache\" \"" << r
            << "\\logs\"\r\n"
               "if errorlevel 8 (\r\n"
               "  echo Nao consegui copiar os arquivos da atualizacao.\r\n"
               "  pause\r\n"
               "  exit /b 1\r\n"
               ")\r\n"
               "start \"\" \""
            << d << "\\" << exe
            << "\"\r\n"
               "rd /s /q \""
            << w
            << "\"\r\n"
               "(goto) 2>nul & del \"%~f0\"\r\n";
    }
    SPDLOG_INFO("Updater: {} downloaded, closing to install", tag);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"cmd.exe /c \"\"" + script.wstring() + L"\"\"";
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, nullptr, &si,
                        &pi)) {
        Fail("Could not start the installer.");
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // The game closes from its own thread, on the next frame (see Draw).
    sReadyToClose = true;
}

bool sCloseRequested = false;

} // namespace

void UpdaterWindow::InitElement() {
    if (kThisBuild < 0 || !CVarGetInteger(CVAR_CHECK_FOR_UPDATES, 1)) {
        return;
    }
    sState = State::Checking;
    std::thread(CheckThread).detach();
}

void UpdaterWindow::Draw() {
    State state = sState;
    if (sDismissed || state == State::Idle || state == State::Checking || state == State::UpToDate) {
        return;
    }
    if (sReadyToClose && !sCloseRequested) {
        sCloseRequested = true;
        Ship::Context::GetInstance()->GetWindow()->Close();
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x / 2,
                                   viewport->WorkPos.y + viewport->WorkSize.y / 2),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(560, FLT_MAX));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoMove;
    std::string title = std::string(BenGui::Tr("Update available")) + "###MMADEUpdater";
    if (!ImGui::Begin(title.c_str(), nullptr, flags)) {
        ImGui::End();
        return;
    }
    std::string tag;
    {
        std::lock_guard<std::mutex> lock(sMutex);
        tag = sTag;
    }
    switch (state) {
        case State::Available: {
            ImGui::TextWrapped("%s", BenGui::Tr("A new version of the game is available:"));
            ImGui::Text("%s", tag.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("%s", BenGui::Tr("The game will download it, close, install it and open again. Your "
                                                "saves, mods and settings are kept."));
            ImGui::Spacing();
            if (ImGui::Button(BenGui::Tr("Update now"))) {
                sState = State::Downloading;
                std::thread(DownloadThread).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button(BenGui::Tr("Not now"))) {
                sDismissed = true;
            }
            break;
        }
        case State::Downloading: {
            uint64_t done = sDone, total = sTotal;
            ImGui::TextWrapped("%s %s...", BenGui::Tr("Downloading"), tag.c_str());
            float fraction = total > 0 ? (float)((double)done / (double)total) : 0.0f;
            std::string label = total > 0 ? fmt::format("{} / {} MB", done >> 20, total >> 20)
                                          : fmt::format("{} MB", done >> 20);
            ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0), label.c_str());
            break;
        }
        case State::Installing:
            ImGui::TextWrapped("%s", BenGui::Tr("Installing. The game closes now and opens again when it is done."));
            break;
        case State::Failed: {
            std::string error;
            {
                std::lock_guard<std::mutex> lock(sMutex);
                error = sError;
            }
            ImGui::TextWrapped("%s", BenGui::Tr(error.c_str()));
            if (ImGui::Button(BenGui::Tr("Try again"))) {
                sState = State::Downloading;
                std::thread(DownloadThread).detach();
            }
            ImGui::SameLine();
            if (ImGui::Button(BenGui::Tr("Close"))) {
                sDismissed = true;
            }
            break;
        }
        default:
            break;
    }
    ImGui::End();
}

#else

void UpdaterWindow::InitElement() {
}

void UpdaterWindow::Draw() {
}

#endif

void UpdaterWindow::DrawElement() {
}

void UpdaterWindow::UpdateElement() {
}
