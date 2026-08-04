# ayame design notes

## Goal

Replicate what the `OnlineFix` patches from online-fix.me do, but as
open-source, self-hosted artifacts that a downloader like `lua-dl` can generate
offline — no dependency on the site's cookie-gated downloads.

Observed OnlineFix layout per game (studied from real installs):

| File | Role |
|---|---|
| `winmm.dll` (~500 KB, identical across games) | generic loader proxy |
| `OnlineFix64.dll` (~13 MB, per game) | Steam API emulator/hooks |
| `OnlineFix.ini` | `RealAppId`, `FakeAppId=480`, `[Interfaces]`, `[Hashes]`, `[EOSAuthWrapper] DeviceIdAuth=true` |
| `EOSSDK-Win64-Shipping.dll` | EOS proxy (only in EOS games) |
| `EOSSDK-Win64-Shipping.of` | the game's original EOS SDK, renamed |

## The actual multiplayer trick

For EOS-based games the load-bearing step is small and generic:

1. Games log into EOS with `EOS_Connect_Login`, normally passing
   `EOS_ECT_STEAM_SESSION_TICKET` credentials (a Steam auth ticket validated
   against Epic's backend). Without a valid owned-game ticket this fails.
2. Epic also accepts `EOS_ECT_DEVICEID_ACCESS_TOKEN` (10) — an anonymous,
   device-bound identity created via `EOS_Connect_CreateDeviceId`. No Steam
   ticket, no Epic account required.
3. So: intercept `EOS_Connect_Login`, discard the Steam credential, create the
   device id, and log in with DeviceID + a display name. All lobby/session/P2P
   traffic still runs through Epic's real backend, so cross-play with legit
   players works.

This is exactly what eos-proxy ships and what ReFix/OnlineFix's EOS component
does (see the `[EOSAuthWrapper] DeviceIdAuth=true` ini key).

## Architecture decisions

### Drop-in proxy over injection

We replace `EOSSDK-Win64-Shipping.dll` (renaming the original to `.of`) rather
than injecting into the process. Reasons:

- No injector binary, no AV-alarms from remote thread creation.
- The proxy is present before any game code runs; no race hooking exports.
- Uninstall is a clean file swap.

The same pattern covers the loader: a `winmm.dll` proxy next to the exe loads
with the process on both Unity and UE games.

### Generated forwarding tables

EOS SDK exports change between versions (679 → 746 across the builds we
sampled). Hand-maintained export lists rot. Instead `tools/genexports` reads
the export directory of one or more real SDK DLLs, merges them into a master
list, and emits:

- `fwd_stub.asm` — one MASM `jmp [g_ayame_procs + i*8]` trampoline per export
- `exports.def` — maps each public EOS name to its trampoline
- `exports.h` — name table used to `GetProcAddress` the originals at runtime

Slots whose export doesn't exist in the game's particular SDK build keep a
fallback stub (returns `EOS_NOT_FOUND`) instead of a NULL pointer, so a superset
proxy never crashes a game built against an older SDK.

Forwarding is a tail-jump through a pointer table — no stack manipulation, so
it works for every calling signature without knowing it.

### Intercept set

Only functions where behavior must change are intercepted (everything else
forwards untouched):

- `EOS_Connect_Login` — credential swap (the fix itself)
- `EOS_Connect_CreateDeviceId` / `DeleteDeviceId` — pass-through hooks kept for
  logging and future fallback handling
- `EOS_Lobby_CreateLobby` / `JoinLobby` / `JoinLobbyById` — clear
  `bPresenceEnabled` (requires an Epic account)
- `EOS_IntegratedPlatformOptionsContainer_Add` — stubbed success; registering
  Steam as an integrated platform would make the SDK mint Steam-ticket logins
  behind our back
- `EOS_Initialize` / `EOS_Shutdown` — pass-through used as the init trigger

### ApiVersion discipline

EOS structs grow by appending fields; options carry an `ApiVersion`. We never
invent versions for options the game built — only the credential type pointer
is swapped, and the one struct we construct (`EOS_Connect_CreateDeviceIdOptions`)
uses `EOS_CONNECT_CREATEDEVICEID_API_LATEST`. Struct mirrors only declare
fields up to the last one we touch, which is safe under the append-only ABI.

### Steam side (winmm.dll)

EOS DeviceID login removes the need for Steam entirely on the EOS path, but
games often still initialize Steamworks. The winmm loader:

- sets `SteamAppId`/`SteamGameId` to the mask appid (default 480, Spacewar,
  which every Steam account owns) and `SteamOverlayGameId` to the real appid;
- patches `SteamAPI_RestartAppIfNecessary` to return false, preventing the
  restart loop when the mask appid differs from the real one.

This keeps the single-player path and achievements working through the real
Steam client while multiplayer flows through EOS DeviceID.

## Threat model / failure modes

- **Original missing** (`_of` deleted): every forward hits the fallback stub;
  the proxy logs a fatal line and the game degrades instead of hard-crashing.
- **New SDK export the master list lacks**: the game wouldn't have loaded the
  original either; regenerate the table from the new DLL.
- **Loader-lock safety**: all heavy init (loading the `.of`, resolving exports)
  happens on a worker thread or via `InitOnceExecuteOnce` on first use;
  `DllMain` only seeds the pointer table and spawns the thread.

## Goldberg (gbe_fork) compatibility — required when used with lua-dl

lua-dl installs Goldberg as the Steam layer. gbe_fork's `base.cpp` installs
process-wide Detours on `connect`, `sendto`, `WSAConnect` and
`WinHttpConnect` for its LAN emulation ("lan only" mode, default on), which
makes the EOS SDK's HTTPS to `api.epicgames.dev` fail with
`EOS_NoConnection` (libcurl error 7, immediate TCP refuse). Verified on Big
Walk (EOS 1.19): with the hooks active every EOS request dies; with
`[main::connectivity] disable_lan_only=1` in `steam_settings/configs.main.ini`
the SDK reaches Epic and the DeviceID login succeeds.

`ayame install` therefore patches that setting automatically whenever a
`steam_settings/configs.main.ini` sits next to an EOS DLL it installs.

This is also why every reference project avoids Goldberg: OnlineFix hooks the
real steam_api64.dll, LumaCore/ReFix-proxy use the real Steam client. None of
them place a network hooking emulator in the process.

## Steam-native games (no EOS SDK)

Games whose multiplayer is pure Steamworks were v1's out-of-scope case —
until the discovery that no emulation is needed at all:

- The real `steam_api64.dll` initializes fine from an externally launched
  process when `SteamAppId=480` (Spacewar — launchable by every account):
  `tools/steamprobe.c` shows `SteamAPI_Init(480) = 1` with the real account
  id, against the running client. The only blocker is
  `SteamAPI_RestartAppIfNecessary` returning true, which the winmm loader
  already suppresses.
- So steam-native mode is just the existing loader with no DLL swap: the
  depot's original `steam_api64.dll` stays, the loader env-masks the appid
  and kills the restart check. All lobby/P2P runs through the user's real
  Steam client under appid 480 (identity = real persona, friends list real,
  overlay works).
- Overlay parity with OnlineFix: the loader preload
  `GameOverlayRenderer64.dll` from the Steam install (winmm watch thread,
  before the game creates its D3D device — Steam's own late injection misses
  the device hooks).

Requirements: real Steam installed, running, logged in. Players meet only
other masked sessions (other ayame/OnlineFix users), never legit buyers on
the real appid. Installer behavior: EOS DLLs present → eos mode; none →
steam-native mode (winmm + ini with `[Main] SteamNative=true`).

## Verified in the wild

- Big Walk (1478500, Unity IL2CPP, EOS 1.19, PlayEveryWare plugin):
  lua-dl depot download → Goldberg → ayame install → launch:
  `EOS Connect login succeeded`, Stomp messaging connected,
  `Current Auth Status: LoggedIn`. Display name follows OnlineFix behavior:
  the game's own name if it passes one, else the Steam persona name.
  (2026-08-05: hosted a lobby live — `Lobby created successfully`, lobby
  socket to api.epicgames.dev established, zero EOS errors.)
- MECCHA CHAMELEON layout: install/uninstall round-trip restores the
  original SDK byte-exact.
- Pummel Party (880940, Unity/Mono, Steamworks.NET, Steam-native): lua-dl
  depot download → `ayame install` (steam-native auto-detected) → launch with
  real Steam running: Steam shows "in-game: Spacewar", in-game name = real
  Steam persona, lobby created, friend invites work, Shift+Tab overlay opens.
  No file of the game was modified except adding winmm.dll + ayame.ini.

## Out of scope (v1)

- 32-bit games (all reference targets are x64).
- Anti-cheat titles (EAC/BattlEye will reject the device identity).


