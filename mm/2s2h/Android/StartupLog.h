#ifndef STARTUP_LOG_H
#define STARTUP_LOG_H

// A log of the app's start on Android, from the first native line, in <data folder>/inicio.log. The game's own log
// only begins once the resource manager is up, so a crash before that (while making mm.o2r, for one) left no trace.
// It records each startup step, and a crash signal or an uncaught exception with its backtrace.
// Does nothing on other platforms.

#ifdef __cplusplus
extern "C" {
#endif

void StartupLog_Init(void);
void StartupLog_Step(const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif // STARTUP_LOG_H
