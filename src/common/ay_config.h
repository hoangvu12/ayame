#pragma once

#include <windows.h>

#define AY_INI_NAME L"ayame.ini"

typedef struct ay_config {
    DWORD real_app_id;   // [Main] RealAppId
    DWORD fake_app_id;   // [Main] FakeAppId (default 480)
    char language[64];   // [Main] Language (default "english")
    char name[128];      // [Main] Name (EOS display name; empty = steam persona)
    BOOL unlock_all_dlc; // [Main] UnlockAllDLC
    BOOL device_id_auth; // [EOS] DeviceIdAuth (default true)
    BOOL loaded;
} ay_config;

// Loads ayame.ini from the exe directory (fallback: directory of the given
// module). Safe to call repeatedly; result is cached after first success.
const ay_config *ay_config_get(HMODULE self);

// Resolves the directory (with trailing backslash) of the given module into
// out. Returns FALSE on failure.
BOOL ay_module_dir(HMODULE mod, wchar_t *out, DWORD out_len);
