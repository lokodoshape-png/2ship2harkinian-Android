#ifndef BUTTON_PROMPTS_H
#define BUTTON_PROMPTS_H

#include "ultra64.h"

// Button icons that follow the controller in use (Enhancements > Controls > Button Icons).
//
// The style is picked from the last device that was used (Xbox, PlayStation, Nintendo, keyboard) or set in the menu,
// and each N64 button is drawn as the physical button it is mapped to. The icons are drawn by the game itself, not
// taken from any asset, and are served as high resolution textures over the original texture slots: the dialogue
// button glyphs, the pause menu prompts, the ocarina notes and small badges on the HUD buttons.
// With the N64 style everything keeps its original look.

typedef enum {
    BUTTON_PROMPT_A,
    BUTTON_PROMPT_B,
    BUTTON_PROMPT_C, // "a C button" in text
    BUTTON_PROMPT_L,
    BUTTON_PROMPT_R,
    BUTTON_PROMPT_Z,
    BUTTON_PROMPT_CUP,
    BUTTON_PROMPT_CDOWN,
    BUTTON_PROMPT_CLEFT,
    BUTTON_PROMPT_CRIGHT,
    BUTTON_PROMPT_STICK,
    BUTTON_PROMPT_START,
    BUTTON_PROMPT_MAX
} ButtonPrompt;

typedef enum {
    BUTTON_PROMPT_PAUSE_A, // 24x16 ia8
    BUTTON_PROMPT_PAUSE_C, // 48x16 ia8, C-left, C-down and C-right
    BUTTON_PROMPT_PAUSE_Z, // 24x32 ia8
    BUTTON_PROMPT_PAUSE_R, // 24x32 ia8
    BUTTON_PROMPT_PAUSE_MAX
} ButtonPromptPauseTexture;

#ifdef __cplusplus
extern "C" {
#endif

// True when the icons replace the original art (any style but N64).
s32 ButtonPrompts_Active(void);

// Texture for a dialogue button character (0xB0 to 0xBB) in the font's 16x16 i4 slot, or NULL to keep the original.
const char* ButtonPrompts_MessageGlyph(u8 character);

// Icon in a 16x16 ia8 slot (ocarina notes, HUD badges), or NULL to keep the original.
const char* ButtonPrompts_Icon(ButtonPrompt prompt);

// Pause menu prompt texture, or NULL to keep the original.
const char* ButtonPrompts_PauseTexture(ButtonPromptPauseTexture texture);

// Small icons on the corners of the HUD's A, B and C buttons.
void ButtonPrompts_DrawItemButtonBadges(struct PlayState* play);
void ButtonPrompts_DrawAButtonBadge(struct PlayState* play);

#ifdef __cplusplus
}

#include <string>

namespace ButtonPrompts {

// Mounts the archive that serves the icons. Called once at startup, after the resource manager.
void Mount();

// Name of the style in use, for the menu.
std::string GetActiveStyleName();

// A 128x16 ia8 texture of "Hold [B] to speed up" (or "Speeding up" when `active`) in the menu language.
const char* GetCutsceneHint(bool active);

} // namespace ButtonPrompts
#endif

#endif // BUTTON_PROMPTS_H
