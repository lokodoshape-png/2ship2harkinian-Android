#ifndef BEN_TRANSLATION_H
#define BEN_TRANSLATION_H

#include <string>

// Menu translation (2Ship MM3D port). Menu code keeps its English text, which stays the key for CVars, search
// paths and window names; the widgets translate it only when drawing. Text without a translation is shown as is.
namespace BenGui {

#define CVAR_MENU_LANGUAGE "gSettings.MenuLanguage"

enum MenuLanguage {
    MENU_LANGUAGE_PORTUGUESE_BR,
    MENU_LANGUAGE_ENGLISH,
};

// Text to show for `text` in the chosen menu language. A "##id" suffix is kept as is. The returned pointer stays
// valid for the rest of the program.
const char* Tr(const char* text);
std::string Tr(const std::string& text);

} // namespace BenGui

#endif // BEN_TRANSLATION_H
