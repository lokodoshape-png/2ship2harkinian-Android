#ifndef MM3D_H
#define MM3D_H

void RegisterEarlyBombersNotebook();
void RegisterTimeSongVariantsInPause();

#ifdef __cplusplus
extern "C" {
#endif

struct EnvironmentContext;
struct LightContext;

int TimeSongVariantsInPause_GetSongIndex(int defaultSongIndex, int cursor);
void MM3DLighting_Apply(struct EnvironmentContext* envCtx, struct LightContext* lightCtx);

#ifdef __cplusplus
}
#endif

#endif // MM3D_H
