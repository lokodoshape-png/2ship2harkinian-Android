#ifndef MM3D_H
#define MM3D_H

void RegisterEarlyBombersNotebook();
void RegisterTimeSongVariantsInPause();

#ifdef __cplusplus
extern "C" {
#endif

int TimeSongVariantsInPause_GetSongIndex(int defaultSongIndex, int cursor);

#ifdef __cplusplus
}
#endif

#endif // MM3D_H
