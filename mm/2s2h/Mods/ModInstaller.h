#ifndef MOD_INSTALLER_H
#define MOD_INSTALLER_H

#include <string>
#include <vector>

// "Mods" menu: lists the archives in <app folder>/mods, turns them on and off, and installs new ones from a file
// the player picks. Mods are loaded when the game starts, so every change takes effect on the next launch.
namespace ModInstaller {

struct ModFile {
    std::string path;
    std::string name; // file name without the ".desativado" suffix
    bool enabled;
};

std::string GetModsFolder(const std::string& appShortName);

// Archives in the mods folder (enabled ones end in .o2r, .otr or .zip; disabled ones also end in ".desativado"),
// sorted by name.
std::vector<ModFile> List(const std::string& appShortName);

// Renames the file to add or remove the ".desativado" suffix. Returns false if the rename failed.
bool SetEnabled(const ModFile& mod, bool enabled);

// Opens the system file picker. On Android the app copies the chosen file into the mods folder, taking the .o2r
// and .otr files out of a .zip that carries them. On PC the chosen file is copied here; the returned text says
// what happened (empty if the picker was closed or the copy happens later, on Android).
std::string OpenPicker(const std::string& appShortName);

} // namespace ModInstaller

#endif // MOD_INSTALLER_H
