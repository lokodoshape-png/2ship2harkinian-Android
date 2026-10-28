#include "ModsMenu.h"

#include <string>
#include <vector>

#include "BenPort.h"
#include "Translation.h"
#include "UIWidgets.hpp"
#include "2s2h/Language/GameLanguage.h"
#include "2s2h/Mods/ModInstaller.h"

namespace BenGui {

void DrawModsMenu() {
    static std::vector<ModInstaller::ModFile> sMods;
    static std::string sMessage;
    static bool sChanged = false;
    static double sNextScan = 0.0;
    // Rescan now and then, so a file the app copied in (Android picker) shows up without reopening the menu.
    if (ImGui::GetTime() >= sNextScan) {
        sMods = ModInstaller::List(appShortName);
        sNextScan = ImGui::GetTime() + 1.0;
    }

    if (UIWidgets::Button("Add Mod...", UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
        sMessage = ModInstaller::OpenPicker(appShortName);
        sNextScan = 0.0;
    }
    ImGui::TextWrapped("%s", Tr("Accepts 2Ship mods in .o2r, .otr or .zip. On Android, .o2r and .otr files inside a "
                                ".zip are extracted. Mods are kept in this folder:"));
    ImGui::TextWrapped("%s", ModInstaller::GetModsFolder(appShortName).c_str());
    if (!sMessage.empty()) {
        ImGui::TextWrapped("%s", sMessage.c_str());
    }

    ImGui::SeparatorText(Tr("Installed Mods"));
    if (sMods.empty()) {
        ImGui::TextWrapped("%s", Tr("No mods installed."));
    }
    for (ModInstaller::ModFile& mod : sMods) {
        bool enabled = mod.enabled;
        // The file name is not a menu text, so it goes straight to ImGui instead of through the translated widgets.
        if (ImGui::Checkbox(mod.name.c_str(), &enabled) && ModInstaller::SetEnabled(mod, enabled)) {
            sChanged = true;
            sNextScan = 0.0;
        }
    }
    if (sChanged) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", Tr("Close and reopen the game to apply."));
    }

    ImGui::SeparatorText(Tr("Textures"));
    UIWidgets::CVarCheckbox("Use Alternate Assets", "gEnhancements.Mods.AlternateAssets",
                            UIWidgets::CheckboxOptions().Tooltip("HD texture packs usually need this turned on."));
}

void DrawGameLanguageMenu() {
    static const std::vector<const char*> sLanguageOptions = { "English (original)", "Portuguese (translation pack)" };
    static int32_t sGameLanguage = GameLanguage::GetSaved(appShortName);
    static const std::string sPackFolder =
        GameLanguage::GetPackFolder(GameLanguage::LANGUAGE_PORTUGUESE_BR, appShortName);

    if (UIWidgets::Combobox("Game Text Language (needs restart)", &sGameLanguage, sLanguageOptions)) {
        GameLanguage::Save(static_cast<GameLanguage::Language>(sGameLanguage), appShortName);
    }
    ImGui::TextWrapped("%s", Tr("Takes effect the next time the game opens. For Portuguese, put the translation "
                                "pack (.o2r, .zip or .otr) in this folder:"));
    ImGui::TextWrapped("%s", sPackFolder.c_str());
    ImGui::TextWrapped("%s", Tr("Without a pack in that folder, the game text stays in English."));
#ifdef __ANDROID__
    if (UIWidgets::Button("Choose Translation File...", UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
        // The app saves Portuguese as the language once a pack is copied; it applies on the next launch.
        GameLanguage::OpenPackPicker();
    }
#endif
}

} // namespace BenGui
