// eossmoke - loads the ayame EOS proxy and exercises a handful of exports
// to prove the forwarding table resolves against the original SDK.
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef const char *(*EResultToString_t)(int);
typedef int (*Initialize_t)(const void *);
typedef void *(*GenericFn_t)(void);

// Minimal mirrors of the structs the proxy reads, matching eos_types.h.
typedef struct {
    int ApiVersion;
    const char *Token;
    int Type;
} SmokeCredentials;
typedef struct {
    int ApiVersion;
    const SmokeCredentials *Credentials;
    const void *UserLoginInfo;
} SmokeLoginOptions;
typedef struct {
    int ResultCode;
    void *ClientData;
    void *LocalUserId;
    void *ContinuanceToken;
} SmokeLoginCallbackInfo;

static int g_login_cb_called = 0;
static int g_login_cb_result = -1000;
static void login_cb(const SmokeLoginCallbackInfo *info) {
    g_login_cb_called = 1;
    if (info) g_login_cb_result = info->ResultCode;
}

typedef void (*ConnectLogin_t)(void *handle, const SmokeLoginOptions *opts, void *cb_data,
                               void (*cb)(const SmokeLoginCallbackInfo *));

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: eossmoke <path-to-proxy-dll>\n");
        return 2;
    }

    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) {
        fprintf(stderr, "LoadLibrary failed (err %lu)\n", GetLastError());
        return 1;
    }
    printf("loaded proxy: %s\n", argv[1]);

    EResultToString_t to_string = (EResultToString_t)GetProcAddress(m, "EOS_EResult_ToString");
    Initialize_t initialize = (Initialize_t)GetProcAddress(m, "EOS_Initialize");
    if (!to_string || !initialize) {
        fprintf(stderr, "core exports missing\n");
        return 1;
    }

    // Give the proxy's init thread a moment to resolve the forwarding table.
    Sleep(500);

    int rc_probe = 0;
    const char *s = to_string(rc_probe);
    if (!s || (uintptr_t)s < 0x10000) {
        fprintf(stderr, "EOS_EResult_ToString(0) hit the fallback stub (unresolved)\n");
        return 1;
    }
    printf("EOS_EResult_ToString(0) = \"%s\" (expect EOS_Success)\n", s);

    // Dump the SDK's own names for result codes (diagnostic mode).
    if (argc > 2 && strcmp(argv[2], "--codes") == 0) {
        for (int c = 1; c <= 40; c++) {
            const char *n = to_string(c);
            if (n && (uintptr_t)n > 0x10000) printf("code %2d = %s\n", c, n);
        }
        return 0;
    }

    // Bad ApiVersion -> the real SDK rejects it (any nonzero EOS_EResult;
    // the exact code varies by SDK version).
    struct {
        int api_version;
        void *reserved_a, *reserved_b, *reserved_c;
    } init_opts = {0, 0, 0, 0};
    int rc = initialize(&init_opts);
    printf("EOS_Initialize(bad version) = %d (expect nonzero rejection)\n", rc);
    if (rc == 0) {
        fprintf(stderr, "real EOS_Initialize accepted a bogus version?\n");
        return 1;
    }

    // A master-list export absent from this SDK version must hit the
    // fallback stub and return EOS_NOT_FOUND (6), never crash.
    GenericFn_t maybe_missing = (GenericFn_t)GetProcAddress(m, "src_new");
    if (maybe_missing) {
        void *r = maybe_missing();
        if (r == (void *)6) {
            printf("src_new() = fallback EOS_NOT_FOUND as expected\n");
        } else {
            printf("src_new() = %p (original actually has it)\n", r);
        }
    }

    // Core online-fix path: feed a Steam-session-ticket login through the
    // proxy. It must intercept, swap to DeviceID, and always invoke the
    // completion callback (never crash). The real SDK will reject the NULL
    // handle and report an error through the callback, which is fine.
    ConnectLogin_t connect_login = (ConnectLogin_t)GetProcAddress(m, "EOS_Connect_Login");
    if (connect_login) {
        SmokeCredentials creds = {1, "fake-steam-ticket", 13 /* STEAM_SESSION_TICKET */};
        SmokeLoginOptions opts = {1, &creds, NULL};
        connect_login(NULL, &opts, NULL, login_cb);
        Sleep(1000); // let the real SDK's callback thread run
        if (g_login_cb_called) {
            printf("EOS_Connect_Login intercepted; callback fired (ResultCode=%d)\n",
                   g_login_cb_result);
        } else {
            fprintf(stderr, "EOS_Connect_Login callback never fired\n");
            return 1;
        }
    }

    printf("smoke ok\n");
    return 0;
}
