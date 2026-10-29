#include "ModsMenu.h"

#include <string>
#include <vector>

#include "BenPort.h"
#include "Translation.h"
#include "UIWidgets.hpp"
#include "2s2h/Language/GameLanguage.h"
#include "2s2h/Mods/ModInstaller.h"
#include "2s2h/Enhancements/Audio/MusicFolder.h"
#include "2s2h/Enhancements/Audio/SequenceMidi.h"
#include "2s2h/Enhancements/Audio/AudioCollection.h"

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

void DrawMusicMenu() {
    static std::vector<MusicFolder::Track> sTracks = MusicFolder::GetTracks();
    static bool sWasRunning = false;
    const MusicFolder::ImportStatus status = MusicFolder::GetImportStatus();
    if (sWasRunning && !status.running) {
        sTracks = MusicFolder::GetTracks();
    }
    sWasRunning = status.running;

    ImGui::SeparatorText(Tr("Music"));
    ImGui::TextWrapped("%s", Tr("Pick a folder with your songs (wav, flac, mp3, ogg or opus). Name each file after the "
                                "game song it replaces, like \"Clock Town Day 1.flac\"; the names are listed below. "
                                "The songs are converted into the game's music folder:"));
    ImGui::TextWrapped("%s", MusicFolder::GetLibraryFolder().c_str());

    if (status.running) {
        ImGui::TextWrapped("%s %d/%d: %s", Tr("Converting songs"), status.done, status.total, status.current.c_str());
        ImGui::ProgressBar(status.total > 0 ? (float)status.done / status.total : 0.0f, ImVec2(-1.0f, 0.0f));
    } else {
        if (UIWidgets::Button("Choose Music Folder...", UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
            MusicFolder::PickFolder();
        }
        if (!sTracks.empty()) {
            ImGui::SameLine();
            if (UIWidgets::Button("Remove Music", UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
                MusicFolder::Clear();
                sTracks.clear();
            }
        }
    }
    if (!status.message.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", status.message.c_str());
    }
    static std::string sModMessage;
    if (!status.running && !sTracks.empty()) {
        if (UIWidgets::Button("Export as Mod (.o2r)", UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
            MusicFolder::ExportMod(&sModMessage);
        }
        ImGui::TextWrapped("%s", Tr("Packs these songs into one mod file. On this device they already play; the mod "
                                    "is for another device or for sharing, installed through Settings > Mods."));
        if (!sModMessage.empty()) {
            ImGui::TextWrapped("%s", sModMessage.c_str());
        }
    }

    UIWidgets::CVarCheckbox("Play Songs in Place of the Originals", "gEnhancements.Audio.AutoReplaceByName",
                            UIWidgets::CheckboxOptions().DefaultValue(true).Tooltip(
                                "A song named after a game song plays instead of it. The Audio Editor can "
                                "still choose another song for any track."));
    ImGui::TextWrapped("%s", Tr("To skip an intro when the song repeats, put a loops.txt in the folder with lines "
                                "like \"Termina Field = 7.5\" or \"Clock Town Day 1 = 3.1 - 92.4\" (seconds)."));

    if (!sTracks.empty() && ImGui::CollapsingHeader(Tr("Songs in the music folder"))) {
        for (const MusicFolder::Track& track : sTracks) {
            if (track.replaces.empty()) {
                ImGui::BulletText("%s  (%s)", track.name.c_str(), Tr("choose where it plays in the Audio Editor"));
            } else {
                ImGui::BulletText("%s  ->  %s", track.name.c_str(), track.replaces.c_str());
            }
        }
    }
    ImGui::SeparatorText(Tr("Game Songs as MIDI"));
    ImGui::TextWrapped("%s", Tr("Saves every game song as a MIDI file, already named for the music folder, with a "
                                "loops.txt of its loop points. Arrange them in any music program, export the audio "
                                "with the same names and choose that folder above."));
    const SequenceMidi::ExportStatus midiStatus = SequenceMidi::GetExportStatus();
    if (midiStatus.running) {
        ImGui::TextWrapped("%s %d/%d", Tr("Exporting songs"), midiStatus.done, midiStatus.total);
        ImGui::ProgressBar(midiStatus.total > 0 ? (float)midiStatus.done / midiStatus.total : 0.0f,
                           ImVec2(-1.0f, 0.0f));
    } else if (UIWidgets::Button("Export Game Songs (MIDI)",
                                 UIWidgets::ButtonOptions().Size(UIWidgets::Sizes::Inline))) {
        SequenceMidi::StartExport(SequenceMidi::GetExportFolder());
    }
    if (!midiStatus.message.empty()) {
        ImGui::TextWrapped("%s", midiStatus.message.c_str());
    }

    if (ImGui::CollapsingHeader(Tr("Game song names"))) {
        if (AudioCollection::Instance != nullptr) {
            for (const auto& [seqId, info] : AudioCollection::Instance->GetAllSequences()) {
                if (seqId <= AudioCollection::Instance->GetMaxOriginalSeqId() && info.canBeReplaced &&
                    (info.category & (SEQ_BGM_CUSTOM | SEQ_BGM_CUSTOM_FANFARE))) {
                    ImGui::BulletText("%s", info.label.c_str());
                }
            }
        }
    }
}

} // namespace BenGui
