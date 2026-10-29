#include "StartupLog.h"

#include <cstdarg>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <unwind.h>

// Must match DATA_FOLDER in MainActivity.java, which starts the file on each launch.
static const char* kLogPath = "/storage/emulated/0/2S2H-MM3D/inicio.log";
static int sFd = -1;

static void WriteRaw(const char* text) {
    if (sFd >= 0) {
        (void)!write(sFd, text, strlen(text));
    }
}

static void WriteLine(const char* text) {
    char stamp[32];
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    strftime(stamp, sizeof(stamp), "[%H:%M:%S] ", &local);
    WriteRaw(stamp);
    WriteRaw(text);
    WriteRaw("\n");
    __android_log_print(ANDROID_LOG_INFO, "2ShipStartup", "%s", text);
}

struct Backtrace {
    void** frames;
    int count;
    int max;
};

static _Unwind_Reason_Code CollectFrame(struct _Unwind_Context* context, void* arg) {
    Backtrace* bt = (Backtrace*)arg;
    uintptr_t pc = _Unwind_GetIP(context);
    if (pc != 0 && bt->count < bt->max) {
        bt->frames[bt->count++] = (void*)pc;
    }
    return bt->count < bt->max ? _URC_NO_REASON : _URC_END_OF_STACK;
}

static void WriteBacktrace() {
    void* frames[48];
    Backtrace bt = { frames, 0, 48 };
    _Unwind_Backtrace(CollectFrame, &bt);
    for (int i = 0; i < bt.count; i++) {
        Dl_info info = {};
        char line[512];
        if (dladdr(frames[i], &info) != 0 && info.dli_fname != nullptr) {
            const char* lib = strrchr(info.dli_fname, '/') ? strrchr(info.dli_fname, '/') + 1 : info.dli_fname;
            uintptr_t offset = (uintptr_t)frames[i] - (uintptr_t)info.dli_fbase;
            snprintf(line, sizeof(line), "  #%02d %s+0x%zx %s", i, lib, (size_t)offset,
                     info.dli_sname != nullptr ? info.dli_sname : "");
        } else {
            snprintf(line, sizeof(line), "  #%02d %p", i, frames[i]);
        }
        WriteLine(line);
    }
}

static struct sigaction sPrevious[32];

static void OnSignal(int sig, siginfo_t* info, void* context) {
    char line[128];
    snprintf(line, sizeof(line), "FECHOU: sinal %d (%s), endereço %p", sig, strsignal(sig),
             info != nullptr ? info->si_addr : nullptr);
    WriteLine(line);
    WriteBacktrace();
    // Hand the signal on, so Android still reports the crash as before.
    sigaction(sig, &sPrevious[sig], nullptr);
    raise(sig);
}

static void OnTerminate() {
    std::string message = "FECHOU: exceção não tratada";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            message += ": ";
            message += e.what();
        } catch (...) {}
    }
    WriteLine(message.c_str());
    WriteBacktrace();
    abort();
}

extern "C" void StartupLog_Init(void) {
    if (sFd >= 0) {
        return;
    }
    sFd = open(kLogPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
    WriteLine("Início do código nativo");

    static char altStack[64 * 1024];
    stack_t ss = {};
    ss.ss_sp = altStack;
    ss.ss_size = sizeof(altStack);
    sigaltstack(&ss, nullptr);

    struct sigaction action = {};
    action.sa_sigaction = OnSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int sig : { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP }) {
        sigaction(sig, &action, &sPrevious[sig]);
    }
    std::set_terminate(OnTerminate);
    atexit([]() { WriteLine("Saiu (exit)"); });
}

extern "C" void StartupLog_Step(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    WriteLine(line);
}

#else

extern "C" void StartupLog_Init(void) {
}

extern "C" void StartupLog_Step(const char* format, ...) {
}

#endif
