#ifndef MOD_INSTALLER_H
#define MOD_INSTALLER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// "Mods": lists the archives in <app folder>/mods, turns them on and off, installs new ones from a file the player
// picks or from a Google Drive link, and (when "apply live" is on) loads and unloads them while the game runs.
namespace ModInstaller {

#define CVAR_MODS_LIVE_APPLY "gEnhancements.Mods.LiveApply"
#define CVAR_MODS_DRIVE_FOLDER "gEnhancements.Mods.DriveFolder"

struct ModFile {
    std::string path;
    std::string name; // file name without the ".desativado" suffix
    bool enabled;
};

std::string GetModsFolder(const std::string& appShortName);

// Archives in the mods folder (enabled ones end in .o2r, .otr or .zip; disabled ones also end in ".desativado"),
// sorted by name.
std::vector<ModFile> List(const std::string& appShortName);

// Renames the file to add or remove the ".desativado" suffix. Returns false if the rename failed. A loaded mod
// that cannot be renamed while it is open (Windows) is taken out of the game first, at the end of the frame.
bool SetEnabled(const ModFile& mod, bool enabled);

// Opens the system file picker. On Android the app copies the chosen file into the mods folder, taking the .o2r
// and .otr files out of a .zip that carries them. On PC the chosen file is copied here; the returned text says
// what happened (empty if the picker was closed or the copy happens later, on Android).
std::string OpenPicker(const std::string& appShortName);

// ----- Live loading.

// BenPort, before the resource manager starts: how many archives come before the mods (mm.o2r, 2ship.o2r). Mods
// loaded live go right after them, so the translation packs and the music folder still win, as at startup.
void NoteStartup(size_t baseArchiveCount);

bool LiveApplyEnabled();

// The mod is in the game right now.
bool IsActive(const ModFile& mod);

// Mods whose on / off differs from what the game has loaded (they wait for "apply" or the next launch).
int PendingCount(const std::string& appShortName);

// Makes the game match the mods folder at the end of this frame. Any thread.
void RequestApply();

// BenPort, at the end of a frame with the audio thread idle. Returns true when archives changed (the caller
// clears the texture cache).
bool Update();

// The last thing that happened (applied, downloaded, failed), with a number that grows on each new message.
struct Notice {
    int serial = 0;
    std::string text;
};
Notice LastNotice();

// ----- Google Drive.

struct DriveEntry {
    std::string id;
    std::string name;
};

enum class DownloadState { Idle, Listing, Downloading, Done, Failed };

struct DownloadStatus {
    DownloadState state = DownloadState::Idle;
    uint64_t done = 0, total = 0;
    std::string name; // the file being downloaded
    std::string message;
};

DownloadStatus GetDownloadStatus();
bool DownloadBusy();

// Asks for a link: a text box on Android (filled with the clipboard), the clipboard on PC. Returns false on PC
// when the clipboard holds no Google Drive link.
bool AskDriveLink();

// A Google Drive link to a file (downloads it into the mods folder) or a folder (lists its mods and remembers it).
// Returns false when the text is not a Drive link.
bool OpenDriveLink(const std::string& link);

// The mods of the remembered folder, as last listed.
std::vector<DriveEntry> GetDriveFolder();
bool HasDriveFolder();
void RefreshDriveFolder();
void ForgetDriveFolder();
void DownloadDriveFile(const DriveEntry& entry);

// A mod file of this name is in the mods folder (on or off).
bool IsInstalled(const std::string& appShortName, const std::string& fileName);

} // namespace ModInstaller

#endif // MOD_INSTALLER_H
