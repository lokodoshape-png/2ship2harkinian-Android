#include "ModInstaller.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <libultraship/libultraship.h>
#include <utils/StringHelper.h>

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <jni.h>
#else
#include "2s2h/Extractor/portable-file-dialogs.h"
#endif

namespace ModInstaller {

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

bool SetEnabled(const ModFile& mod, bool enabled) {
    if (mod.enabled == enabled) {
        return true;
    }
    std::filesystem::path path(mod.path);
    std::filesystem::path target = path.parent_path() / (enabled ? mod.name : mod.name + sDisabledSuffix);
    std::error_code ec;
    std::filesystem::rename(path, target, ec);
    return !ec;
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
#else
std::string OpenPicker(const std::string& appShortName) {
    std::vector<std::string> selection =
        pfd::open_file("Escolha o mod", ".", { "Mods (.o2r .otr .zip)", "*.o2r *.otr *.zip" }, pfd::opt::multiselect)
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
            failed += "\n" + name + " (formato não aceito)";
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
    if (!installed.empty()) {
        message += "Mod instalado, feche e abra o jogo para usar:" + installed;
    }
    if (!failed.empty()) {
        message += (message.empty() ? "" : "\n\n") + std::string("Não foi possível instalar:") + failed;
    }
    return message;
}
#endif

} // namespace ModInstaller
