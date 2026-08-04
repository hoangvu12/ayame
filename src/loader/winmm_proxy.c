// ayame winmm loader — drop-in winmm.dll proxy.
//
// Many Unity/UE games load winmm.dll from the game directory before anything
// else, which makes it a reliable load-order hook. This DLL forwards every
// winmm export to the real system winmm.dll and, on a worker thread, watches
// for the game's steam_api64.dll so it can patch the Steam entry points that
// an online fix needs (restart suppression + appid environment).
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

#include "../common/ay_config.h"
#include "../common/ay_hook.h"
#include "../common/ay_log.h"
#include "../generated/winmm_exports.h"

// Defined in generated/winmm_fwd_stub.asm; jmp stubs tail-jump through it.
extern void *g_ayame_procs[AYAME_NUM_FORWARDS];

static HMODULE g_self;
static HMODULE g_real_winmm;
static volatile LONG g_booted;

// Fallback so an early call never jumps through NULL (returns 0 in rax).
static LRESULT ay_winmm_missing(void) { return 0; }

static void resolve_winmm(void) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscat_s(sys, MAX_PATH, L"\\winmm.dll");
    g_real_winmm = LoadLibraryW(sys);
    if (!g_real_winmm) {
        ay_log("loader: cannot load system winmm (err %lu)", GetLastError());
        return;
    }
    int n = 0;
    for (int i = 0; i < AYAME_NUM_FORWARDS; i++) {
        FARPROC p = GetProcAddress(g_real_winmm, ayame_forward_names[i]);
        if (p) {
            g_ayame_procs[i] = (void *)p;
            n++;
        }
    }
    ay_log("loader: forwarded %d/%d winmm exports", n, AYAME_NUM_FORWARDS);
}

// ---------------------------------------------------------------------------
// Steam hooks
// ---------------------------------------------------------------------------
// Never bounce through a Steam restart: the game is already running under the
// mask appid and RestartAppIfNecessary(realAppId) would loop forever.
static int __cdecl hk_RestartAppIfNecessary(unsigned int appid) {
    (void)appid;
    ay_log("steam: RestartAppIfNecessary(%u) suppressed", appid);
    return 0;
}

static void apply_steam_env(const ay_config *cfg) {
    wchar_t buf[32], obuf[32];
    _snwprintf_s(buf, 32, _TRUNCATE, L"%lu", cfg->fake_app_id);
    SetEnvironmentVariableW(L"SteamAppId", buf);
    SetEnvironmentVariableW(L"SteamGameId", buf);
    obuf[0] = L'\0';
    if (cfg->real_app_id) {
        _snwprintf_s(obuf, 32, _TRUNCATE, L"%lu", cfg->real_app_id);
        // Overlay/screenshots tag the real game for display purposes.
        SetEnvironmentVariableW(L"SteamOverlayGameId", obuf);
    }
    ay_log("steam: env SteamAppId=%ls SteamOverlayGameId=%ls", buf, obuf);
}

static void install_steam_hooks(void) {
    HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
    if (!sa) sa = GetModuleHandleW(L"steam_api.dll");
    if (!sa) return;

    ay_hook_export(sa, "SteamAPI_RestartAppIfNecessary", hk_RestartAppIfNecessary);
}

static BOOL steam_client_running(void) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    PROCESSENTRY32W pe = {sizeof(pe)};
    BOOL found = FALSE;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"steam.exe") == 0) {
                found = TRUE;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Steam normally injects the overlay into games it launched itself; for a
// standalone launch we do it manually (parity with OnlineFix's overlay).
static void inject_steam_overlay(void) {
    if (GetModuleHandleW(L"GameOverlayRenderer64.dll")) {
        ay_log("overlay: renderer already loaded");
        return;
    }
    if (!steam_client_running()) {
        ay_log("overlay: Steam client not running, skipping");
        return;
    }

    wchar_t steam_path[MAX_PATH];
    DWORD cb = sizeof(steam_path);
    LSTATUS st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam",
                              L"SteamPath", RRF_RT_REG_SZ, NULL, steam_path, &cb);
    if (st != ERROR_SUCCESS) {
        ay_log("overlay: Steam install not found in registry (err %ld)", st);
        return;
    }
    for (wchar_t *p = steam_path; *p; p++) {
        if (*p == L'/') *p = L'\\';
    }
    wcscat_s(steam_path, MAX_PATH, L"\\GameOverlayRenderer64.dll");
    HMODULE ov = LoadLibraryW(steam_path);
    if (!ov) {
        ay_log("overlay: failed to load %ws (err %lu)", steam_path, GetLastError());
        return;
    }
    ay_log("overlay: GameOverlayRenderer64.dll loaded (shift+tab may now work)");
}

static DWORD WINAPI steam_watch_thread(LPVOID param) {
    (void)param;
    const ay_config *cfg = ay_config_get(g_self);
    apply_steam_env(cfg);

    // Load the overlay renderer as early as possible: it must hook the
    // renderer's device-creation calls before the game creates its D3D
    // device, otherwise the overlay has no surface to draw into. Steam's
    // own injection happens later (session recognition) and misses it.
    if (cfg->steam_native) {
        inject_steam_overlay();
    }

    // Wait for the game to load its Steam API DLL, then patch it.
    for (int i = 0; i < 600; i++) { // up to ~60s
        HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
        if (!sa) sa = GetModuleHandleW(L"steam_api.dll");
        if (sa) {
            install_steam_hooks();
            return 0;
        }
        Sleep(100);
    }
    ay_log("steam: steam_api*.dll never loaded; hooks not installed");
    return 0;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        ay_log_open();
        ay_log("ayame winmm loader attached (pid %lu)", GetCurrentProcessId());

        // Seed every forward slot with a safe fallback before resolution.
        for (int i = 0; i < AYAME_NUM_FORWARDS; i++) g_ayame_procs[i] = (void *)ay_winmm_missing;

        resolve_winmm();

        if (InterlockedCompareExchange(&g_booted, 1, 0) == 0) {
            HANDLE t = CreateThread(NULL, 0, steam_watch_thread, NULL, 0, NULL);
            if (t) CloseHandle(t);
        }
    }
    return TRUE;
}
