#ifndef MUSIC_FOLDER_H
#define MUSIC_FOLDER_H

#include <cstdint>
#include <string>
#include <vector>

// Replacement music from a folder the player picks (Enhancements > Audio Editor > Music).
//
// Picking a folder converts every song in it (wav, flac, mp3, ogg, opus) into the game's music library in
// <app folder>/musicas: one 48 kHz mono Opus file per channel plus faixas.json. At startup the library is mounted as
// an archive of streamed custom sequences, the same resources a "Future" streamed-audio .o2r carries, so the audio
// editor lists the songs and a song named after an original track ("Clock Town Day 1.flac") replaces it.
namespace MusicFolder {

struct Track {
    std::string name;
    uint32_t channels = 0;  // 1 or 2; each channel is its own file
    uint32_t frames = 0;    // at 48 kHz
    uint32_t loopStart = 0; // in frames
    uint32_t loopEnd = 0;
    bool once = false;    // fanfares and ocarina songs do not loop
    std::string replaces; // label of the original track it replaces, empty if none matches
};

struct ImportStatus {
    bool running = false;
    int done = 0;
    int total = 0;
    std::string current;
    std::string message; // result of the last import, shown in the menu
};

std::string GetLibraryFolder();

// Songs in the library (read from faixas.json).
std::vector<Track> GetTracks();

// Adds the library to the archive manager. Called once at startup, before the audio engine loads its sequences.
void Mount();

// Converts the songs of `sourceFolder` into the library in the background, replacing what was there.
// `deleteSource` removes the folder afterwards (Android copies the picked folder to a temporary one first).
void StartImport(const std::string& sourceFolder, bool deleteSource);
ImportStatus GetImportStatus();

// Opens the system folder picker and starts an import with the chosen folder.
void PickFolder();

// Empties the library. Takes effect on the next launch, like an import.
void Clear();

// Packs the library into one .o2r with the same songs, names and loop points, to install through Settings > Mods on
// another device or to share. Writes GetModPath() and returns a message for the menu.
std::string GetModPath();
bool ExportMod(std::string* message);

// The original track (label) a song name replaces, and whether that track plays once. Empty if none.
std::string FindOriginal(const std::string& songName, bool* once);

} // namespace MusicFolder

#endif // MUSIC_FOLDER_H
