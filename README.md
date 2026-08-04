# ayame

An open-source **online-fix alternative** for Windows Steam games — both
EOS-based multiplayer and Steam-native (Steamworks) multiplayer.

**EOS games:** many Steam games gate their multiplayer behind a valid Steam
auth ticket handed to EOS (`EOS_ECT_STEAM_SESSION_TICKET`). ayame drops a
proxy in place of the game's EOS SDK and swaps that login to a **DeviceID
access token** (`EOS_ECT_DEVICEID_ACCESS_TOKEN`), which Epic issues to any
client without needing a Steam ticket. Matchmaking, lobbies, and P2P still
run through Epic's real servers.

**Steam-native games:** games whose multiplayer is pure Steamworks (lobbies/P2P
against Valve's backend). ayame detects the missing EOS SDK and switches to
steam-native mode: the game keeps its real `steam_api64.dll` and the winmm
loader masks the appid to Spacewar (480), so `SteamAPI_Init` succeeds against
your running Steam client and matchmaking runs through it — real persona name,
friends list, and overlay included. Requires the real Steam client, running
and logged in.

This is the same technique used by `OnlineFix`, `eos-proxy`, and `ReFix`,
rebuilt clean-room and driven by a single Go installer.

> **Disclaimer:** research and educational use only. You are responsible for
> local laws, platform terms of service, and software licenses. You should own
> the games you use this on.

## How it works

Two DLLs are placed into the game folder:

- **`EOSSDK-Win64-Shipping.dll`** — the EOS proxy. The game's original SDK is
  renamed to `EOSSDK-Win64-Shipping.of` and every one of its ~700 exports is
  forwarded to it through generated assembly trampolines. A handful of entry
  points are intercepted:
  - `EOS_Initialize` / `EOS_Shutdown` — pass through, but boot our init.
  - `EOS_Connect_Login` — the core fix: swaps Steam-ticket credentials to a
    DeviceID login, preserving the game's struct ApiVersions.
  - `EOS_Connect_CreateDeviceId` / `DeleteDeviceId` — pass through.
  - `EOS_Lobby_CreateLobby` / `JoinLobby` / `JoinLobbyById` — clear
    `bPresenceEnabled`, which needs an Epic account we don't have.
  - `EOS_IntegratedPlatformOptionsContainer_Add` — stubbed to success, since
    registering Steam would make the SDK mint its own Steam-ticket logins.
- **`winmm.dll`** — a loader proxy. Games load `winmm.dll` from the game dir
  early, so this forwards all 180 winmm exports to the system DLL and, on a
  worker thread, sets the Steam appid environment and patches
  `SteamAPI_RestartAppIfNecessary` so the game doesn't loop-restart.

The forwarding tables are **generated**, not hand-written: `tools/genexports`
parses the export table of real EOS SDK builds (and `winmm.dll`) and emits the
MASM trampolines + linker def + name list. The merged master list covers
several SDK versions, so one proxy works across games; exports absent from a
given SDK resolve to a safe stub instead of crashing.

## Layout

```
ayame/
  cmd/ayame/           Go CLI (install / uninstall / status)
  internal/
    install/           install + uninstall logic
    payloads/          embedded DLLs (built by build.bat)
  src/
    common/            log / ini-config / inline-hook helpers
    eos/eos_proxy.c    EOS proxy (drop-in EOSSDK-Win64-Shipping.dll)
    loader/winmm_proxy.c  winmm loader proxy
    generated/         generated MASM stubs + def + name lists
  tools/
    genexports/        PE export-table parser + stub emitter
    eossmoke.c         runtime smoke test for the proxy
  build.bat            MSVC x64 build (DLLs + CLI)
```

## Requirements

- Windows x64
- Visual Studio 2022 Build Tools (MSVC v143 + MASM) — found automatically
- Go 1.21+ (for the CLI and `genexports`)

## Build

```bat
build.bat
```

Outputs in `dist/`:
- `EOSSDK-Win64-Shipping.dll`
- `winmm.dll`
- `ayame.exe` (the CLI, with both DLLs embedded)

## Install into a game

```bat
dist\ayame.exe install "C:\path\to\game" --appid <RealAppId>
```

This finds every `EOSSDK-Win64-Shipping.dll`, renames the original to `.of`,
drops the proxy, places `winmm.dll` next to the game executable, and writes
`ayame.ini`. If no EOS SDK is found, it installs in **steam-native mode**
(winmm loader + ini only, `[Main] SteamNative=true`): the game keeps its real
steam_api and rides your running Steam client under the mask appid. Options:

```
--appid N        the game's real Steam AppID
--language S     game language (default: english)
--unlock-dlc     answer "owned" to DLC checks
--force-ini      overwrite an existing ayame.ini
```

Remove again with:

```bat
dist\ayame.exe uninstall "C:\path\to\game"
```

And after a launch, get machine-readable evidence of what ran and what to do
next:

```bat
dist\ayame.exe diagnose "C:\path\to\game" --json
```

### Goldberg users

If the game's Steam layer is Goldberg (gbe_fork, e.g. installed by lua-dl),
its default LAN hooks (`connect`/`sendto`/…) break EOS's HTTPS to Epic.
`ayame install` detects `steam_settings/configs.main.ini` next to the EOS DLL
and adds `disable_lan_only=1` automatically. Online-fix style setups that use
the real Steam client don't need this.

## Regenerating the export tables

Only needed when supporting a new EOS SDK build. Provide one or more real
EOS DLLs (and optionally `.txt` name lists) and the names to intercept:

```bat
go run .\tools\genexports src\generated ^
  "C:\...\EOSSDK-Win64-Shipping.of" ^
  tools\genexports\data\refix_eos_export_names.txt ^
  -- EOS_Initialize EOS_Shutdown EOS_Connect_Login EOS_Connect_CreateDeviceId ^
     EOS_Connect_DeleteDeviceId EOS_IntegratedPlatformOptionsContainer_Add ^
     EOS_Lobby_CreateLobby EOS_Lobby_JoinLobby EOS_Lobby_JoinLobbyById
```

Then rebuild with `build.bat`.

## Verified

Smoke-tested against real EOS SDK builds (see `tools/eossmoke.c`):
- export forwarding returns real SDK results,
- unknown exports hit the safe fallback,
- `EOS_Connect_Login` with a Steam-ticket credential is intercepted and the
  DeviceID swap path runs through the real SDK's callback.

## Credits / prior art

- `OnlineFix` (Ran-Mewo) and `eos-proxy` (yesyes0649) for the DeviceID approach
- `ReFix` (Coronitaa) and `SFF`/`LumaCore` (Midrags) for the DLL-proxy /
  forwarding architecture
- The EOS SDK public headers for struct layouts
