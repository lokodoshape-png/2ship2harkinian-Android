#ifndef SONGS_H
#define SONGS_H

void RegisterEnableSunsSong();
void RegisterFasterSongPlayback();
void RegisterZoraEggCount();
void RegisterPauseOwlWarp();
void RegisterSkipScarecrowSong();
void RegisterDoubleTimeHourSelect();

#ifdef __cplusplus
extern "C" {
#endif

bool PauseOwlWarp_IsOwlWarpEnabled(void);
void DoubleTimeHourSelect_Init(void);
void DoubleTimeHourSelect_Update(struct Input* input);
void DoubleTimeHourSelect_Apply(void);
void DoubleTimeHourSelect_Cancel(void);

#ifdef __cplusplus
}
#endif

#endif // SONGS_H
