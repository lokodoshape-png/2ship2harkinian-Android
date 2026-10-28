#include <libultraship/bridge/consolevariablebridge.h>
#include "MM3D.h"

extern "C" {
#include <z64.h>
#include <macros.h>
#include "z64environment.h"
#include "z64light.h"
}

#define CVAR_LIGHTING(name) "gEnhancements.MM3D.Lighting." name

// Majora's Mask 3D renders most scenes brighter and with much less distance fog than the N64 version.
// This approximates that look on top of the vanilla light settings, after the game has blended them
// for the current time of day, so day/night transitions and scene-specific lighting still apply.
extern "C" void MM3DLighting_Apply(EnvironmentContext* envCtx, LightContext* lightCtx) {
    if (!CVarGetInteger(CVAR_LIGHTING("Enabled"), 0)) {
        return;
    }

    // Push the fog start towards its maximum distance, proportionally to the slider
    s32 fogReduction = CLAMP(CVarGetInteger(CVAR_LIGHTING("FogReduction"), 50), 0, 100);
    s32 fogNear = lightCtx->fogNear;
    if (fogNear < ENV_FOGNEAR_MAX) {
        // Stop just short of the maximum: a fog near equal to the far plane makes the fog math degenerate
        fogNear += ((ENV_FOGNEAR_MAX - 4 - fogNear) * fogReduction) / 100;
        lightCtx->fogNear = CLAMP(fogNear, 0, ENV_FOGNEAR_MAX);
    }

    // Raise the ambient light and scale the directional lights
    s32 ambientBoost = CLAMP(CVarGetInteger(CVAR_LIGHTING("AmbientBoost"), 20), 0, 100);
    s32 lightScale = CLAMP(CVarGetInteger(CVAR_LIGHTING("DirectionalScale"), 110), 50, 200);
    for (s32 i = 0; i < 3; i++) {
        lightCtx->ambientColor[i] = CLAMP(lightCtx->ambientColor[i] + ambientBoost, 0, 255);
        envCtx->dirLight1.params.dir.color[i] =
            CLAMP((envCtx->dirLight1.params.dir.color[i] * lightScale) / 100, 0, 255);
        envCtx->dirLight2.params.dir.color[i] =
            CLAMP((envCtx->dirLight2.params.dir.color[i] * lightScale) / 100, 0, 255);
    }
}
