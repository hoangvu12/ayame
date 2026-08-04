// ayame EOS proxy — drop-in replacement for EOSSDK-Win64-Shipping.dll.
//
// The installer renames the game's original SDK to EOSSDK-Win64-Shipping.of
// and drops this DLL in its place. Every export forwards to the original;
// a handful of Connect/Lobby entry points are intercepted to swap the login
// from a Steam session ticket to a DeviceID access token, which lets EOS
// multiplayer work without the game owning a valid Steam ticket for EOS.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/ay_config.h"
#include "../common/ay_log.h"
#include "../generated/exports.h"
#include "eos_types.h"

// ---------------------------------------------------------------------------
// Forwarding table
// ---------------------------------------------------------------------------
// g_ayame_procs is defined in generated/fwd_stub.asm; the jmp stubs exported
// under the original EOS names all tail-jump through it. Slot order matches
// ayame_forward_names.
extern void *g_ayame_procs[AYAME_NUM_FORWARDS];

// Universal fallback for exports missing from the original: returns 0 in rax,
// harmless for both EOS_EResult and pointer-returning functions (x64 ABI:
// caller cleans the stack).
static EOS_EResult ay_missing_export(void) { return EOS_NOT_FOUND; }

static HMODULE g_self;
static HMODULE g_orig;
static INIT_ONCE g_init_once = INIT_ONCE_STATIC_INIT;

typedef void *(*GetProcAddressFn)(HMODULE, const char *);

static BOOL CALLBACK do_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;

    wchar_t orig_path[MAX_PATH];
    if (!ay_module_dir(g_self, orig_path, MAX_PATH)) {
        ay_log("init: cannot resolve own directory");
        return TRUE;
    }
    wcscat_s(orig_path, MAX_PATH, L"EOSSDK-Win64-Shipping.of");

    g_orig = LoadLibraryW(orig_path);
    if (!g_orig) {
        ay_log("init: FATAL - missing %ws (err %lu). The original EOS SDK must "
               "be renamed to EOSSDK-Win64-Shipping.of next to the proxy.",
               orig_path, GetLastError());
        return TRUE;
    }
    ay_log("init: original loaded from %ws", orig_path);

    int resolved = 0;
    for (int i = 0; i < AYAME_NUM_FORWARDS; i++) {
        FARPROC p = GetProcAddress(g_orig, ayame_forward_names[i]);
        if (p) {
            g_ayame_procs[i] = (void *)p;
            resolved++;
        } else {
            ay_log("init: export missing from original: %s", ayame_forward_names[i]);
        }
    }
    ay_log("init: resolved %d/%d forwards", resolved, AYAME_NUM_FORWARDS);
    return TRUE;
}

static void ensure_init(void) {
    InitOnceExecuteOnce(&g_init_once, do_init, NULL, NULL);
}

static void *orig_proc(const char *name) {
    if (!g_orig) return NULL;
    return (void *)GetProcAddress(g_orig, name);
}

// ---------------------------------------------------------------------------
// Steam persona name (display name for the DeviceID login)
// ---------------------------------------------------------------------------
static FARPROC find_export_by_prefix(HMODULE mod, const char *prefix) {
    const BYTE *base = (const BYTE *)mod;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress) return NULL;

    const IMAGE_EXPORT_DIRECTORY *exp = (const IMAGE_EXPORT_DIRECTORY *)(base + dir->VirtualAddress);
    const DWORD *names = (const DWORD *)(base + exp->AddressOfNames);
    const WORD *ordinals = (const WORD *)(base + exp->AddressOfNameOrdinals);
    const DWORD *funcs = (const DWORD *)(base + exp->AddressOfFunctions);
    size_t prefix_len = strlen(prefix);

    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char *name = (const char *)(base + names[i]);
        if (strncmp(name, prefix, prefix_len) == 0)
            return (FARPROC)(base + funcs[ordinals[i]]);
    }
    return NULL;
}

static void steam_persona_name(char *out, size_t out_len) {
    out[0] = '\0';
    HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
    if (!sa) sa = GetModuleHandleW(L"steam_api.dll");
    if (!sa) return;

    typedef const char *(*GetPersonaNameFn)(void *);
    GetPersonaNameFn get_name =
        (GetPersonaNameFn)GetProcAddress(sa, "SteamAPI_ISteamFriends_GetPersonaName");
    if (!get_name) return;

    void *friends = NULL;
    typedef void *(*FriendsAccessor)(void);
    FriendsAccessor legacy = (FriendsAccessor)GetProcAddress(sa, "SteamFriends");
    if (legacy) friends = legacy();
    if (!friends) {
        FriendsAccessor versioned = (FriendsAccessor)find_export_by_prefix(sa, "SteamAPI_SteamFriends_v");
        if (versioned) friends = versioned();
    }
    if (!friends) return;

    const char *name = get_name(friends);
    if (name && *name) {
        strncpy_s(out, out_len, name, _TRUNCATE);
    }
}

// ---------------------------------------------------------------------------
// Connect login interception
// ---------------------------------------------------------------------------
typedef struct login_ctx {
    EOS_HConnect handle;
    EOS_Connect_OnLoginCb cb;
    void *cb_data;
    int32_t login_api_ver;
    int32_t creds_api_ver;
    int32_t user_info_api_ver; // 0 if the game passed none
    BOOL had_user_info;
    char display_name[128];
} login_ctx;

static void on_login_done(const EOS_Connect_LoginCallbackInfo *info) {
    login_ctx *ctx = (login_ctx *)info->ClientData;
    EOS_Connect_LoginCallbackInfo out = *info;
    out.ClientData = ctx->cb_data;
    if (ctx->cb) ctx->cb(&out);
    free(ctx);
}

static void on_create_device_id_done(const EOS_Connect_CreateDeviceIdCallbackInfo *info) {
    login_ctx *ctx = (login_ctx *)info->ClientData;
    EOS_Connect_Login_t real_login = (EOS_Connect_Login_t)orig_proc("EOS_Connect_Login");

    BOOL ready = info->ResultCode == EOS_SUCCESS || info->ResultCode == EOS_DUPLICATE_NOT_ALLOWED;
    if (!ready || !real_login) {
        EOS_Connect_LoginCallbackInfo fail;
        memset(&fail, 0, sizeof(fail));
        fail.ResultCode = info->ResultCode;
        fail.ClientData = ctx->cb_data;
        ay_log("Connect_CreateDeviceId failed: %d", info->ResultCode);
        if (ctx->cb) ctx->cb(&fail);
        free(ctx);
        return;
    }

    EOS_Connect_Credentials creds;
    creds.ApiVersion = ctx->creds_api_ver;
    creds.Token = NULL;
    creds.Type = EOS_ECT_DEVICEID_ACCESS_TOKEN;

    EOS_Connect_UserLoginInfo who;
    memset(&who, 0, sizeof(who));
    who.ApiVersion = ctx->had_user_info ? ctx->user_info_api_ver : 1;
    who.DisplayName = ctx->display_name;

    EOS_Connect_LoginOptions opts;
    opts.ApiVersion = ctx->login_api_ver;
    opts.Credentials = &creds;
    opts.UserLoginInfo = &who;

    ay_log("Connect_Login: DeviceID login as \"%s\" (login.v=%d creds.v=%d)",
           ctx->display_name, ctx->login_api_ver, ctx->creds_api_ver);
    real_login(ctx->handle, &opts, ctx, on_login_done);
}

// OnlineFix behavior: the game's own DisplayName wins (handled by the caller);
// otherwise the Steam persona name, otherwise a fixed fallback.
static void choose_display_name(char *out, size_t out_len, const EOS_Connect_LoginOptions *opts) {
    steam_persona_name(out, out_len);
    if (!out[0]) {
        strncpy_s(out, out_len, "Unknown Player", _TRUNCATE);
    }
    (void)opts;
}

// __declspec(dllexport) on the intercepted names; exports.def lists them too.
__declspec(dllexport) void EOS_Connect_Login(EOS_HConnect handle, const EOS_Connect_LoginOptions *opts,
                                             void *cb_data, EOS_Connect_OnLoginCb cb) {
    ensure_init();
    const ay_config *cfg = ay_config_get(g_self);
    EOS_Connect_Login_t real_login = (EOS_Connect_Login_t)orig_proc("EOS_Connect_Login");
    EOS_Connect_CreateDeviceId_t real_create =
        (EOS_Connect_CreateDeviceId_t)orig_proc("EOS_Connect_CreateDeviceId");

    if (!cfg->device_id_auth || !real_login || !real_create) {
        if (real_login) {
            ay_log("Connect_Login: passthrough (device_id_auth=%d)", cfg->device_id_auth);
            real_login(handle, opts, cb_data, cb);
            return;
        }
        EOS_Connect_LoginCallbackInfo info;
        memset(&info, 0, sizeof(info));
        info.ResultCode = EOS_NOT_FOUND;
        info.ClientData = cb_data;
        if (cb) cb(&info);
        return;
    }

    // Games already logging in with DeviceID need no surgery.
    if (opts && opts->Credentials && opts->Credentials->Type == EOS_ECT_DEVICEID_ACCESS_TOKEN) {
        ay_log("Connect_Login: already DeviceID, passthrough");
        real_login(handle, opts, cb_data, cb);
        return;
    }

    ay_log("Connect_Login intercepted (cred type=%d)",
           opts && opts->Credentials ? opts->Credentials->Type : -1);

    login_ctx *ctx = (login_ctx *)calloc(1, sizeof(login_ctx));
    if (!ctx) {
        EOS_Connect_LoginCallbackInfo info;
        memset(&info, 0, sizeof(info));
        info.ClientData = cb_data;
        if (cb) cb(&info);
        return;
    }
    ctx->handle = handle;
    ctx->cb = cb;
    ctx->cb_data = cb_data;
    // Preserve every ApiVersion the game passed; we only swap the credential.
    ctx->login_api_ver = opts ? opts->ApiVersion : 1;
    ctx->creds_api_ver = (opts && opts->Credentials) ? opts->Credentials->ApiVersion : 1;
    ctx->had_user_info = opts && opts->UserLoginInfo;
    ctx->user_info_api_ver = ctx->had_user_info ? opts->UserLoginInfo->ApiVersion : 0;
    const char *game_name = ctx->had_user_info ? opts->UserLoginInfo->DisplayName : NULL;
    if (game_name && *game_name) {
        strncpy_s(ctx->display_name, sizeof(ctx->display_name), game_name, _TRUNCATE);
    } else {
        choose_display_name(ctx->display_name, sizeof(ctx->display_name), opts);
    }

    EOS_Connect_CreateDeviceIdOptions create;
    create.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
    create.DeviceModel = "PC";
    real_create(handle, &create, ctx, on_create_device_id_done);
}

__declspec(dllexport) void EOS_Connect_CreateDeviceId(EOS_HConnect handle, const void *options,
                                                      void *cb_data, EOS_Connect_OnCreateDeviceIdCb cb) {
    ensure_init();
    EOS_Connect_CreateDeviceId_t real_create =
        (EOS_Connect_CreateDeviceId_t)orig_proc("EOS_Connect_CreateDeviceId");
    if (real_create) {
        real_create(handle, (const EOS_Connect_CreateDeviceIdOptions *)options, cb_data, cb);
        return;
    }
    EOS_Connect_CreateDeviceIdCallbackInfo info = {EOS_SUCCESS, cb_data};
    if (cb) cb(&info);
}

__declspec(dllexport) void EOS_Connect_DeleteDeviceId(EOS_HConnect handle, const void *options,
                                                      void *cb_data,
                                                      void (*cb)(const void *)) {
    ensure_init();
    EOS_Connect_DeleteDeviceId_t real_delete =
        (EOS_Connect_DeleteDeviceId_t)orig_proc("EOS_Connect_DeleteDeviceId");
    if (real_delete) {
        real_delete(handle, options, cb_data, cb);
        return;
    }
    EOS_Connect_CreateDeviceIdCallbackInfo info = {EOS_SUCCESS, cb_data};
    if (cb) cb(&info);
}

// Registering Steam as an integrated platform makes the SDK mint its own
// Steam-ticket logins, which is exactly what we are trying to avoid.
__declspec(dllexport) EOS_EResult EOS_IntegratedPlatformOptionsContainer_Add(
    EOS_HIntegratedPlatformOptionsContainer handle, const void *options) {
    ensure_init();
    (void)handle;
    (void)options;
    ay_log("IntegratedPlatformOptionsContainer_Add: stubbed to success");
    return EOS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Lobby presence stripping
// ---------------------------------------------------------------------------
// bPresenceEnabled requires an Epic account presence we don't have; clear it.
// flag_offset: where the flag sits in the options struct. min_api_ver: the
// ApiVersion that first added the flag; older structs don't contain it.
static void strip_presence(const void *opts, size_t flag_offset, int32_t min_api_ver) {
    if (!opts) return;
    if (*(const int32_t *)opts < min_api_ver) return;
    EOS_Bool *flag = (EOS_Bool *)((uintptr_t)opts + flag_offset);
    if (*flag) {
        *flag = 0;
        ay_log("lobby: cleared bPresenceEnabled");
    }
}

static void lobby_op(EOS_HLobby h, const void *opts, void *cb_data, void *cb, const char *real_name,
                     size_t flag_offset, int32_t min_api_ver) {
    ensure_init();
    strip_presence(opts, flag_offset, min_api_ver);
    EOS_Lobby_OpFn_t real_op = (EOS_Lobby_OpFn_t)orig_proc(real_name);
    if (real_op) {
        real_op(h, opts, cb_data, cb);
        return;
    }
    // No original — report success for create/join so the game keeps going;
    // lobby search results will simply be empty.
    EOS_GenericCallbackInfo_Head info;
    memset(&info, 0, sizeof(info));
    info.ApiVersion = 1;
    info.ResultCode = EOS_SUCCESS;
    info.ClientData = cb_data;
    if (cb) ((void (*)(const void *))cb)(&info);
}

__declspec(dllexport) void EOS_Lobby_CreateLobby(EOS_HLobby h, const void *opts, void *cb_data,
                                                 void *cb) {
    lobby_op(h, opts, cb_data, cb, "EOS_Lobby_CreateLobby",
             offsetof(EOS_Lobby_CreateLobbyOptions_Partial, bPresenceEnabled), 2);
}

__declspec(dllexport) void EOS_Lobby_JoinLobby(EOS_HLobby h, const void *opts, void *cb_data,
                                               void *cb) {
    lobby_op(h, opts, cb_data, cb, "EOS_Lobby_JoinLobby",
             offsetof(EOS_Lobby_JoinLobbyOptions_Partial, bPresenceEnabled), 2);
}

__declspec(dllexport) void EOS_Lobby_JoinLobbyById(EOS_HLobby h, const void *opts, void *cb_data,
                                                   void *cb) {
    lobby_op(h, opts, cb_data, cb, "EOS_Lobby_JoinLobbyById",
             offsetof(EOS_Lobby_JoinLobbyByIdOptions_Partial, bPresenceEnabled), 1);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
__declspec(dllexport) EOS_EResult EOS_Initialize(const void *options) {
    ensure_init();
    EOS_Initialize_t real_init = (EOS_Initialize_t)orig_proc("EOS_Initialize");
    if (real_init) return real_init(options);
    return EOS_SUCCESS;
}

__declspec(dllexport) EOS_EResult EOS_Shutdown(void) {
    ensure_init();
    EOS_Shutdown_t real_shutdown = (EOS_Shutdown_t)orig_proc("EOS_Shutdown");
    if (real_shutdown) return real_shutdown();
    return EOS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
static DWORD WINAPI init_thread(LPVOID param) {
    (void)param;
    ensure_init();
    return 0;
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        ay_log_open();
        ay_log("ayame eos proxy attached (pid %lu)", GetCurrentProcessId());
        // Pre-fill every forward slot with the fallback so an early call can
        // never jump through a NULL pointer; resolution fills real addresses.
        for (int i = 0; i < AYAME_NUM_FORWARDS; i++) {
            g_ayame_procs[i] = (void *)ay_missing_export;
        }
        HANDLE t = CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
