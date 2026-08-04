#include "ay_log.h"

#include <windows.h>
#include <stdio.h>
#include <time.h>

static CRITICAL_SECTION g_cs;
static BOOL g_cs_init = FALSE;
static HANDLE g_file = INVALID_HANDLE_VALUE;
static BOOL g_open_attempted = FALSE;

static void ensure_open(void) {
    if (g_open_attempted) return;
    g_open_attempted = TRUE;

    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"ayame.log");

    g_file = CreateFileW(path, FILE_APPEND_DATA,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

void ay_log_open(void) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cs);
        g_cs_init = TRUE;
    }
    EnterCriticalSection(&g_cs);
    ensure_open();
    LeaveCriticalSection(&g_cs);
}

void ay_logv(const char *fmt, va_list ap) {
    if (!g_cs_init) {
        InitializeCriticalSection(&g_cs);
        g_cs_init = TRUE;
    }
    EnterCriticalSection(&g_cs);
    ensure_open();
    if (g_file != INVALID_HANDLE_VALUE) {
        char line[1024];
        char msg[900];
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);

        SYSTEMTIME st;
        GetLocalTime(&st);
        int len = _snprintf_s(line, sizeof(line), _TRUNCATE,
                              "[%02d:%02d:%02d.%03d] %s\r\n",
                              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        if (len > 0) {
            DWORD written;
            WriteFile(g_file, line, (DWORD)len, &written, NULL);
        }
    }
    LeaveCriticalSection(&g_cs);
}

void ay_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ay_logv(fmt, ap);
    va_end(ap);
}
