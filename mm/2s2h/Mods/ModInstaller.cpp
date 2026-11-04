#include "ModInstaller.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <libultraship/libultraship.h>
#include <ship/resource/archive/Archive.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/resource/Resource.h>
#include <ship/utils/StringHelper.h>
#include "2s2h/BenGui/Translation.h"
#include "2s2h/BenPort.h"

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <jni.h>
#else
#include "2s2h/Extractor/portable-file-dialogs.h"
#endif

namespace ModInstaller {

using BenGui::Tr;

static const char* sDisabledSuffix = ".desativado";

static bool IsArchiveName(const std::string& name) {
    const std::string extension = std::filesystem::path(name).extension().string();
    return StringHelper::IEquals(extension, ".o2r") || StringHelper::IEquals(extension, ".otr") ||
           StringHelper::IEquals(extension, ".zip");
}

std::string GetModsFolder(const std::string& appShortName) {
    return Ship::Context::GetPathRelativeToAppDirectory("mods", appShortName);
}

std::vector<ModFile> List(const std::string& appShortName) {
    std::vector<ModFile> mods;
    std::error_code ec;
    const std::string folder = GetModsFolder(appShortName);
    if (!std::filesystem::is_directory(folder, ec)) {
        return mods;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        bool enabled = true;
        if (StringHelper::EndsWith(name, sDisabledSuffix)) {
            name = name.substr(0, name.size() - strlen(sDisabledSuffix));
            enabled = false;
        }
        if (IsArchiveName(name)) {
            mods.push_back({ entry.path().string(), name, enabled });
        }
    }
    std::sort(mods.begin(), mods.end(), [](const ModFile& a, const ModFile& b) { return a.name < b.name; });
    return mods;
}

// ----- Live loading.

namespace fs = std::filesystem;

size_t sBaseArchives = 0;
std::atomic<bool> sApplyRequested = false;
std::mutex sNoticeMutex;
Notice sNotice;
// Disabled while loaded, but the open file could not be renamed (Windows): renamed once it is out of the game.
std::vector<std::string> sPendingDisable;
// Resources replaced by a live change. The game may still hold raw pointers into them (skeletons, scenes already
// loaded), so they stay alive until the game closes instead of being freed under it.
std::vector<std::shared_ptr<Ship::IResource>> sRetired;

void SetNotice(const std::string& text) {
    std::lock_guard<std::mutex> lock(sNoticeMutex);
    sNotice.serial++;
    sNotice.text = text;
}

Notice LastNotice() {
    std::lock_guard<std::mutex> lock(sNoticeMutex);
    return sNotice;
}

static std::string Join(const std::vector<std::string>& parts, const char* sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        out += (i ? sep : "") + parts[i];
    }
    return out;
}

static std::string Norm(const std::string& path) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(path), ec);
    if (ec) {
        p = fs::path(path).lexically_normal();
    }
    return p.generic_string();
}

static std::string ModsPrefix() {
    std::string folder = Norm(GetModsFolder(appShortName));
    if (!folder.empty() && folder.back() != '/') {
        folder += '/';
    }
    return folder;
}

static bool IsModArchive(const std::shared_ptr<Ship::Archive>& archive, const std::string& prefix) {
    return StringHelper::StartsWith(Norm(archive->GetPath()), prefix);
}

static std::shared_ptr<Ship::ArchiveManager> Archives() {
    auto context = Ship::Context::GetInstance();
    if (context == nullptr || context->GetResourceManager() == nullptr) {
        return nullptr;
    }
    return context->GetResourceManager()->GetArchiveManager();
}

static std::set<std::string> LoadedModPaths() {
    std::set<std::string> loaded;
    auto manager = Archives();
    if (manager == nullptr) {
        return loaded;
    }
    const std::string prefix = ModsPrefix();
    for (const auto& archive : *manager->GetArchives()) {
        if (IsModArchive(archive, prefix)) {
            loaded.insert(Norm(archive->GetPath()));
        }
    }
    return loaded;
}

void NoteStartup(size_t baseArchiveCount) {
    sBaseArchives = baseArchiveCount;
}

bool LiveApplyEnabled() {
    return CVarGetInteger(CVAR_MODS_LIVE_APPLY, 1) != 0;
}

bool IsActive(const ModFile& mod) {
    return LoadedModPaths().count(Norm(mod.path)) > 0;
}

int PendingCount(const std::string& appShortName) {
    std::set<std::string> loaded = LoadedModPaths();
    int count = 0;
    for (const ModFile& mod : List(appShortName)) {
        bool active = loaded.count(Norm(mod.path)) > 0;
        if (active != mod.enabled) {
            count++;
        }
    }
    return count;
}

void RequestApply() {
    sApplyRequested = true;
}

bool SetEnabled(const ModFile& mod, bool enabled) {
    if (mod.enabled == enabled) {
        return true;
    }
    std::filesystem::path path(mod.path);
    std::filesystem::path target = path.parent_path() / (enabled ? mod.name : mod.name + sDisabledSuffix);
    std::error_code ec;
    std::filesystem::rename(path, target, ec);
    if (ec && !enabled && IsActive(mod)) {
        // The game has the file open: take it out of the game, then rename it.
        sPendingDisable.push_back(mod.path);
        RequestApply();
        return true;
    }
    if (!ec && LiveApplyEnabled()) {
        RequestApply();
    }
    return !ec;
}

// The order mods load in at startup: by file name without the extension, ignoring case.
static bool ModOrder(const std::string& a, const std::string& b) {
    std::string an = fs::path(a).stem().string(), bn = fs::path(b).stem().string();
    return std::lexicographical_compare(an.begin(), an.end(), bn.begin(), bn.end(), [](char c1, char c2) {
        return std::tolower((unsigned char)c1) < std::tolower((unsigned char)c2);
    });
}

// The file names an archive holds, and the plain names of its "alt/" ones, for the resource cache.
static void CollectNames(const std::shared_ptr<Ship::Archive>& archive, std::set<std::string>& names) {
    auto files = archive->ListFiles();
    if (files == nullptr) {
        return;
    }
    for (const auto& [hash, name] : *files) {
        names.insert(name);
        if (StringHelper::StartsWith(name, "alt/")) {
            names.insert(name.substr(4));
        }
    }
}

void TakePendingLink(); // ModDownloader.cpp

bool Update() {
    TakePendingLink();
    if (!sApplyRequested.exchange(false)) {
        return false;
    }
    auto manager = Archives();
    if (manager == nullptr) {
        return false;
    }
    auto resources = Ship::Context::GetInstance()->GetResourceManager();
    const std::string prefix = ModsPrefix();
    std::vector<std::shared_ptr<Ship::Archive>> current = *manager->GetArchives();

    std::set<std::string> pendingDisable;
    for (const std::string& path : sPendingDisable) {
        pendingDisable.insert(Norm(path));
    }
    std::set<std::string> wanted;
    for (const ModFile& mod : List(appShortName)) {
        std::string path = Norm(mod.path);
        if (mod.enabled && !pendingDisable.count(path)) {
            wanted.insert(path);
        }
    }

    // Split what is loaded into the base (before the mods), the mods, and the rest (translation, music folder).
    std::vector<std::shared_ptr<Ship::Archive>> base, rest, removed;
    std::vector<std::pair<std::string, std::shared_ptr<Ship::Archive>>> mods;
    std::set<std::string> loaded;
    for (size_t i = 0; i < current.size(); i++) {
        const auto& archive = current[i];
        if (IsModArchive(archive, prefix)) {
            std::string path = Norm(archive->GetPath());
            if (wanted.count(path)) {
                mods.push_back({ path, archive });
                loaded.insert(path);
            } else {
                removed.push_back(archive);
            }
        } else if (i < sBaseArchives) {
            base.push_back(archive);
        } else {
            rest.push_back(archive);
        }
    }
    std::vector<std::string> toAdd;
    for (const std::string& path : wanted) {
        if (!loaded.count(path)) {
            toAdd.push_back(path);
        }
    }
    if (removed.empty() && toAdd.empty()) {
        sPendingDisable.clear();
        return false;
    }

    std::set<std::string> touched;
    std::vector<std::string> removedNames, addedNames, failedNames;
    for (const auto& archive : removed) {
        CollectNames(archive, touched);
        removedNames.push_back(fs::path(archive->GetPath()).filename().string());
        archive->Unload(); // closes the file, so a disabled one can now be renamed
    }
    for (const std::string& path : sPendingDisable) {
        std::error_code ec;
        fs::rename(path, path + sDisabledSuffix, ec);
    }
    sPendingDisable.clear();

    for (const std::string& path : toAdd) {
        // AddArchive opens and checks it (and puts it last for now; the order is fixed below).
        auto archive = manager->AddArchive(path);
        if (archive == nullptr) {
            failedNames.push_back(fs::path(path).filename().string());
            continue;
        }
        CollectNames(archive, touched);
        mods.push_back({ path, archive });
        addedNames.push_back(fs::path(path).filename().string());
    }
    std::sort(mods.begin(), mods.end(), [](const auto& a, const auto& b) { return ModOrder(a.first, b.first); });

    auto ordered = std::make_shared<std::vector<std::shared_ptr<Ship::Archive>>>();
    ordered->insert(ordered->end(), base.begin(), base.end());
    for (const auto& [path, archive] : mods) {
        ordered->push_back(archive);
    }
    ordered->insert(ordered->end(), rest.begin(), rest.end());
    // Rebuilds the file table from scratch in this order (each archive is reopened).
    manager->SetArchives(ordered);

    // Whatever was loaded from these files is read again the next time the game asks for it.
    for (const std::string& name : touched) {
        auto resource = resources->GetCachedResource(name);
        if (resource != nullptr) {
            sRetired.push_back(resource);
            resource->Dirty();
        }
    }

    std::string text;
    if (!addedNames.empty()) {
        text += "Mod ligado: " + Join(addedNames, ", ") + ". ";
    }
    if (!removedNames.empty()) {
        text += "Mod desligado: " + Join(removedNames, ", ") + ". ";
    }
    if (!failedNames.empty()) {
        text += "Não foi possível abrir: " + Join(failedNames, ", ") + ". ";
    }
    if (!addedNames.empty() || !removedNames.empty()) {
        text +=
            "Texturas mudam na hora; modelos e cenários, ao entrar numa nova área; músicas, ao abrir o jogo de novo.";
    }
    SetNotice(text);
    return true;
}

#ifdef __ANDROID__
std::string OpenPicker(const std::string& appShortName) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return "";
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "openModPicker", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return "";
}

// The app copied a picked mod into the mods folder.
extern "C" JNIEXPORT void JNICALL Java_com_dishii_mm_MainActivity_nativeModsInstalled(JNIEnv*, jobject) {
    if (LiveApplyEnabled()) {
        RequestApply();
    }
}
#else
std::string OpenPicker(const std::string& appShortName) {
    std::vector<std::string> selection =
        pfd::open_file(Tr("Choose a mod"), ".", { "Mods (.o2r .otr .zip)", "*.o2r *.otr *.zip" }, pfd::opt::multiselect)
            .result();
    if (selection.empty()) {
        return "";
    }

    const std::filesystem::path folder = GetModsFolder(appShortName);
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    std::string installed, failed;
    for (const std::string& file : selection) {
        const std::filesystem::path source(file);
        const std::string name = source.filename().string();
        if (!IsArchiveName(name)) {
            failed += "\n" + name + " (" + Tr("format not supported") + ")";
            continue;
        }
        std::filesystem::copy_file(source, folder / name, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            failed += "\n" + name + " (" + ec.message() + ")";
        } else {
            installed += "\n" + name;
        }
    }

    std::string message;
    if (!installed.empty() && LiveApplyEnabled()) {
        RequestApply();
        message += "Mod instalado e ligado:" + installed;
    } else if (!installed.empty()) {
        message += Tr("Mod installed. Close and reopen the game to use it:") + installed;
    }
    if (!failed.empty()) {
        message += (message.empty() ? "" : "\n\n") + std::string(Tr("Could not install:")) + failed;
    }
    return message;
}
#endif

} // namespace ModInstaller
