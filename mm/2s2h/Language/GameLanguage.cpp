#include "GameLanguage.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <libultraship/libultraship.h>
#include <utils/StringHelper.h>

#ifdef __ANDROID__
#include <SDL2/SDL.h>
#include <jni.h>
#endif

namespace GameLanguage {

static const char* sLanguageFile = "idioma.txt";

static const char* GetCode(Language language) {
    switch (language) {
        case LANGUAGE_PORTUGUESE_BR:
            return "pt-BR";
        case LANGUAGE_ENGLISH:
        default:
            return "en";
    }
}

Language GetSaved(const std::string& appShortName) {
    std::ifstream file(Ship::Context::GetPathRelativeToAppDirectory(sLanguageFile, appShortName));
    std::string code;
    if (file.is_open() && std::getline(file, code) && StringHelper::IEquals(code, GetCode(LANGUAGE_PORTUGUESE_BR))) {
        return LANGUAGE_PORTUGUESE_BR;
    }
    return LANGUAGE_ENGLISH;
}

void Save(Language language, const std::string& appShortName) {
    std::ofstream file(Ship::Context::GetPathRelativeToAppDirectory(sLanguageFile, appShortName), std::ios::trunc);
    file << GetCode(language) << "\n";

    if (language != LANGUAGE_ENGLISH) {
        std::error_code ec;
        std::filesystem::create_directories(GetPackFolder(language, appShortName), ec);
    }
}

std::string GetPackFolder(Language language, const std::string& appShortName) {
    return Ship::Context::GetPathRelativeToAppDirectory(std::string("traducoes/") + GetCode(language), appShortName);
}

std::vector<std::string> GetPackArchives(const std::string& appShortName) {
    std::vector<std::string> archives;
    const Language language = GetSaved(appShortName);
    if (language == LANGUAGE_ENGLISH) {
        return archives;
    }

    const std::string folder = GetPackFolder(language, appShortName);
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return archives;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, ec)) {
        const std::string extension = entry.path().extension().string();
        if (StringHelper::IEquals(extension, ".o2r") || StringHelper::IEquals(extension, ".zip") ||
            StringHelper::IEquals(extension, ".otr")) {
            archives.push_back(entry.path().generic_string());
        }
    }
    std::sort(archives.begin(), archives.end());
    return archives;
}

#ifdef __ANDROID__
void OpenPackPicker() {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "openTranslationPicker", "()V");
    if (method != nullptr) {
        env->CallVoidMethod(activity, method);
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
}
#endif

} // namespace GameLanguage
