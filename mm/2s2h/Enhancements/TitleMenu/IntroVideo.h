#ifndef TITLE_MENU_INTRO_VIDEO_H
#define TITLE_MENU_INTRO_VIDEO_H

// PC: the opening when the game opens, over the game while it boots muted, the same as the Android app's
// (Android/app/src/main/java/com/dishii/mm/IntroPlayer.java): black, the edition's logo (overlays/titlemenu/
// intro_logo.jpg) fades in for 1.5 s, stays 3 s, fades out in 1 s, then the opening video plays full screen at its own
// proportions with its own sound and subtitles. Every way out (the end, any key, button, click or tap, a video that
// cannot be read) goes through the same moon transition: dark, the moon's red eyes burn, pulse and go out, and the
// title menu fades in.
//
// The video is abertura.mpg (MPEG-1 video and MP2 sound, decoded by pl_mpeg) and its subtitles abertura.srt, beside
// the game (the release zips have both; intro-video.yml makes the video). Without the video the logo is shown alone.
// Options: gEnhancements.TitleMenu.Intro, IntroEveryLaunch and IntroSkippable, as on Android.

namespace IntroVideo {

// Draws the opening on top of everything; true while it is on screen (the title menu takes no input then).
bool Draw();
bool Active();
// The game was reset: the opening plays again, from the logo (Android: through MainActivity.ReplayIntro).
void Replay();

} // namespace IntroVideo

#endif // TITLE_MENU_INTRO_VIDEO_H
