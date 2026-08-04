#include "ay_config.h"
#include "ay_log.h"

#include <stdio.h>

static ay_config g_cfg;
static BOOL g_cfg_done = FALSE;

BOOL ay_module_dir(HMODULE mod, wchar_t *out, DWORD out_len) {
    DWORD n = GetModuleFileNameW(mod, out, out_len);
    if (n == 0 || n >= out_len) return FALSE;
    wchar_t *slash = wcsrchr(out, L'\\');
    if (!slash || slash + 1 - out >= (ptrdiff_t)out_len) return FALSE;
    slash[1] = L'\0';
    return TRUE;
}

static BOOL ini_path(wchar_t *out, DWORD out_len, HMODULE self) {
    // Prefer the exe directory; the game launches with that as CWD-relative
    // context and OnlineFix-style configs live next to the exe.
    DWORD n = GetModuleFileNameW(NULL, out, out_len);
    if (n != 0 && n < out_len) {
        wchar_t *slash = wcsrchr(out, L'\\');
        if (slash && (size_t)(slash + 1 - out) < out_len) {
            wcscpy_s(slash + 1, out_len - (slash + 1 - out), AY_INI_NAME);
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
        }
    }
    // Fallback: next to our own DLL.
    if (ay_module_dir(self, out, out_len)) {
        wcscat_s(out, out_len, AY_INI_NAME);
        if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return TRUE;
    }
    return FALSE;
}

static BOOL get_bool(const wchar_t *path, const wchar_t *sec, const wchar_t *key, BOOL dflt) {
    wchar_t buf[32];
    GetPrivateProfileStringW(sec, key, dflt ? L"true" : L"false", buf, 32, path);
    return _wcsicmp(buf, L"true") == 0 || _wcsicmp(buf, L"1") == 0 || _wcsicmp(buf, L"yes") == 0;
}

static void get_str(const wchar_t *path, const wchar_t *sec, const wchar_t *key,
                    const wchar_t *dflt, char *out, size_t out_len) {
    wchar_t buf[256];
    GetPrivateProfileStringW(sec, key, dflt, buf, 256, path);
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)out_len, NULL, NULL);
    out[out_len - 1] = '\0';
}

const ay_config *ay_config_get(HMODULE self) {
    if (g_cfg_done) return &g_cfg;

    g_cfg.real_app_id = 0;
    g_cfg.fake_app_id = 480;
    strcpy_s(g_cfg.language, sizeof(g_cfg.language), "english");
    g_cfg.unlock_all_dlc = FALSE;
    g_cfg.steam_native = FALSE;
    g_cfg.device_id_auth = TRUE;
    g_cfg.loaded = FALSE;

    wchar_t path[MAX_PATH];
    if (ini_path(path, MAX_PATH, self)) {
        wchar_t buf[32];
        GetPrivateProfileStringW(L"Main", L"RealAppId", L"0", buf, 32, path);
        g_cfg.real_app_id = (DWORD)_wtoi(buf);
        GetPrivateProfileStringW(L"Main", L"FakeAppId", L"480", buf, 32, path);
        g_cfg.fake_app_id = (DWORD)_wtoi(buf);
        if (g_cfg.fake_app_id == 0) g_cfg.fake_app_id = 480;
        get_str(path, L"Main", L"Language", L"english", g_cfg.language, sizeof(g_cfg.language));
        g_cfg.unlock_all_dlc = get_bool(path, L"Main", L"UnlockAllDLC", FALSE);
        g_cfg.steam_native = get_bool(path, L"Main", L"SteamNative", FALSE);
        g_cfg.device_id_auth = get_bool(path, L"EOS", L"DeviceIdAuth", TRUE);
        g_cfg.loaded = TRUE;
        ay_log("config: %ws (RealAppId=%lu FakeAppId=%lu SteamNative=%d DeviceIdAuth=%d)",
               path, g_cfg.real_app_id, g_cfg.fake_app_id, g_cfg.steam_native, g_cfg.device_id_auth);
    } else {
        ay_log("config: ayame.ini not found, using defaults");
    }

    g_cfg_done = TRUE;
    return &g_cfg;
}
