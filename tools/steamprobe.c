// steamprobe - does the real steam_api64.dll init against the running Steam
// client for a FakeAppId (480/Spacewar) from an externally launched process?
//
//   steamprobe.exe <path\to\steam_api64.dll> [appid]
//
// Dev diagnostic for the Steam-native (no-EOS) support question.
#include <stdio.h>
#include <windows.h>

typedef int(__cdecl *SteamAPI_InitFn)(void);
typedef void(__cdecl *SteamAPI_ShutdownFn)(void);
typedef int(__cdecl *RestartFn)(unsigned int);
typedef void *(*FriendsAccessorFn)(void);
typedef const char *(*GetPersonaNameFn)(void *);
typedef unsigned long long (*SteamUser_GetSteamIDFn)(void *);

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: steamprobe <steam_api64.dll> [appid]\n");
        return 2;
    }
    const char *appid = argc > 2 ? argv[2] : "480";
    SetEnvironmentVariableA("SteamAppId", appid);
    SetEnvironmentVariableA("SteamGameId", appid);

    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }

    RestartFn restart = (RestartFn)GetProcAddress(m, "SteamAPI_RestartAppIfNecessary");
    printf("RestartAppIfNecessary(%s) = %d\n", appid, restart ? restart((unsigned int)atoi(appid)) : -1);

    SteamAPI_InitFn init = (SteamAPI_InitFn)GetProcAddress(m, "SteamAPI_Init");
    if (!init) {
        printf("no SteamAPI_Init export\n");
        return 1;
    }
    int ok = init();
    printf("SteamAPI_Init(appid=%s) = %d\n", appid, ok);
    if (!ok) return 3;

    FriendsAccessorFn friends = (FriendsAccessorFn)GetProcAddress(m, "SteamFriends");
    GetPersonaNameFn persona =
        (GetPersonaNameFn)GetProcAddress(m, "SteamAPI_ISteamFriends_GetPersonaName");
    if (friends && persona) {
        void *f = friends();
        const char *name = f ? persona(f) : NULL;
        printf("persona = \"%s\"\n", name ? name : "(null)");
    }
    SteamAPI_ShutdownFn shutdown = (SteamAPI_ShutdownFn)GetProcAddress(m, "SteamAPI_Shutdown");
    if (shutdown) shutdown();
    return 0;
}
