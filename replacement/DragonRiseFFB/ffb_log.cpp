#include "ffb_log.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <mutex>

static std::mutex g_logMu;
static FILE* g_logFile = nullptr;
static bool g_enabled = false;
static bool g_inited = false;

bool FfbLogEnabled()
{
    return g_enabled && g_logFile;
}

void FfbLogInit()
{
    if (g_inited)
        return;
    g_inited = true;

#if defined(DRFFB_DEBUG) && DRFFB_DEBUG
    g_enabled = true;
#else
    char env[8] = {};
    if (GetEnvironmentVariableA("DRFFB_LOG", env, sizeof(env)) > 0 &&
        (env[0] == '1' || env[0] == 'y' || env[0] == 'Y'))
        g_enabled = true;
#endif
    if (!g_enabled)
        return;

    wchar_t path[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH)
        return;
    wcscat_s(path, L"DragonRiseFFB.log");
    // Avoid "ccs=UTF-8" — has crashed under DI COM load on some CRT builds.
    if (_wfopen_s(&g_logFile, path, L"a+") != 0)
        g_logFile = nullptr;
    if (g_logFile) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(g_logFile,
                "\n==== FFB log start %04u-%02u-%02u %02u:%02u:%02u ====\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        fflush(g_logFile);
    }
}

void FfbLogf(const char* fmt, ...)
{
    if (!g_inited)
        FfbLogInit();
    if (!FfbLogEnabled())
        return;
    std::lock_guard<std::mutex> lock(g_logMu);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_logFile, "%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logFile);
    fflush(g_logFile);
}

void FfbLogEffect(const char* stage, uint32_t handle, const char* klass,
                  int32_t magnitude, uint32_t durationUs, uint32_t gain,
                  uint8_t lf, uint8_t hf, uint32_t elapsedUs)
{
    FfbLogf("%s handle=%u class=%s mag=%d durUs=%u gain=%u elapsedUs=%u LF=%u HF=%u",
            stage, handle, klass ? klass : "?", magnitude, durationUs, gain,
            elapsedUs, lf, hf);
}

void FfbLogPacket(const char* tag, const uint8_t pkt[8])
{
    if (!pkt)
        return;
    FfbLogf("%s packet %02X %02X %02X %02X %02X %02X %02X %02X",
            tag, pkt[0], pkt[1], pkt[2], pkt[3], pkt[4], pkt[5], pkt[6], pkt[7]);
}
