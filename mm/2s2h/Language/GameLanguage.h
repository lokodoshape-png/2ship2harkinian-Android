#ifndef GAME_LANGUAGE_H
#define GAME_LANGUAGE_H

#include <string>
#include <vector>

// Game text language. The game itself only ships English text; another language comes from a translation
// pack the player puts in <app folder>/traducoes/<code>/ (any .o2r, .zip or .otr). Packs are loaded as
// archives after the mods, so their text (and any font textures they carry) take priority.
//
// Archives are chosen before the resource manager exists, so the choice is kept in a small file next to
// the config instead of only in a CVar; changing it takes effect on the next launch.
namespace GameLanguage {

enum Language {
    LANGUAGE_ENGLISH,
    LANGUAGE_PORTUGUESE_BR,
};

// Language saved for the next launch.
Language GetSaved(const std::string& appShortName);
void Save(Language language, const std::string& appShortName);

// Translation pack archives for the saved language, sorted by file name; empty for English.
std::vector<std::string> GetPackArchives(const std::string& appShortName);

// Folder the player puts the packs for a language in, e.g. ".../2S2H-MM3D/traducoes/pt-BR".
std::string GetPackFolder(Language language, const std::string& appShortName);

} // namespace GameLanguage

#endif // GAME_LANGUAGE_H
