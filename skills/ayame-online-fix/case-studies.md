# ayame case studies (verified evidence)

## Contents

- Big Walk (1478500) — EOS-based, the reference success
- Pummel Party (880940) — steam-native (no EOS SDK)
- LOCKDOWN Protocol (2780980) — EOS present but unused; Goldberg A/B lesson
- Among Us 3D (3168600) — backend-gated, out of scope
- Heave Ho 2 (2802740) — backend-gated (Coherence), out of scope
- MECCHA CHAMELEON — install/uninstall round-trip safety

## Big Walk (1478500) — EOS-based

Unity IL2CPP, EOS 1.19 via PlayEveryWare plugin, EOS DLL next to the exe.

- lua-dl download → Goldberg → `ayame install` → launch:
  `Connect_Login intercepted (cred type=13)` → DeviceID login, Stomp
  connected, `Current Auth Status: LoggedIn`.
- Hosted a lobby live: `Lobby created successfully`, lobby WebSocket to
  `api.epicgames.dev`, Mirror + Dissonance voice servers up.
- Goldberg was required with `disable_lan_only=1` (its LAN sockets hooks
  otherwise kill EOS HTTPS with connection-refused).
- Display name precedence (matches OnlineFix): game-provided DisplayName →
  Steam persona → "Unknown Player".
- EOS identity is per Windows user via Credential Manager — two local
  instances cannot be distinct EOS users; a real friend is needed for the
  join side.
- Join-proof with a friend: still open.

## Pummel Party (880940) — steam-native

Unity/Mono, Steamworks.NET (`CreateLobby/JoinLobby`, `SendP2PPacket`),
no EOS DLLs anywhere.

- `ayame install` detects no EOS DLL and installs loader-only with
  `[Main] SteamNative=true`.
- With the real Steam client running: `SteamAPI_Init(480)` succeeds (Spacewar
  mask), `RestartAppIfNecessary` suppressed by the loader, Steam shows
  "in-game: Spacewar", in-game name = real Steam persona, lobby created,
  friend invites work.
- Overlay: the loader must preload `GameOverlayRenderer64.dll` from the Steam
  install before the game creates its D3D device; Steam's own late injection
  misses the device hooks and Shift+Tab does nothing.

## LOCKDOWN Protocol (2780980) — EOS unused

UE5. EOS SDK ships in `Engine\Binaries\Win64\`, game exe in
`LockdownProtocol\Binaries\Win64\` (winmm lands with the largest exe).

- Live result: joined a real public session.
- `EOS_Connect_Login` never fired — sessions run on the game's own servers;
  the winmm loader (Spacewar mask + restart suppression) alone does the work.
- Real Steam persona name and lobby list confirmed with the pristine
  `steam_api64.dll` (engine plugin redistributable).
- **Goldberg A/B**: lua-dl applied Goldberg on re-download; lobby list +
  persona regressed. Restoring the `.bak` fixed both. Lesson: prefer pristine
  steam_api whenever online identity matters.
- Prior OnlineFix patches coexist safely: their winmm gets renamed to
  `.bak`, OnlineFix64.dll is inert without their loader.

## Among Us 3D (3168600) — backend-gated (out of scope)

Unity IL2CPP VR title, EOS 1.19 + libtox + Photon Voice, EOS DLL under
`AmongUs3D_Data\Plugins\x86_64\`.

- EOS side worked fully: DeviceID login succeeded, Stomp connected, ToxMod
  initialized.
- Blocker: the game posts its Steam ticket to
  `backend.innersloth.com/.../steam-get-user-info`; the publisher validates
  it server-side and issues the Photon Voice token. Fake ticket →
  `400 user_steam_error 3404` → "Failed Connection: Photon Authentication",
  hard block before any menu.
- Rule: if multiplayer needs the publisher's own backend to vouch for the
  Steam identity, it is out of scope. Same class: other Innersloth titles.

## Heave Ho 2 (2802740) — backend-gated (out of scope)

Unity IL2CPP (6000.3.0f1), Devolver Digital / Le Cartel Studio. No EOS
SDK anywhere → `ayame install` picks steam-native mode; game rides the real
Steam client under the Spacewar mask.

- Install + launch are clean: loader attached, `RestartAppIfNecessary`
  hooked, pristine `steam_api`, game reaches its menus, offline play fine.
- Blocker: online multiplayer runs through the **Coherence** cloud backend,
  not Steamworks lobbies. Player.log shows
  `POST /login/steam → 400 Invalid authentication credentials`
  (`errorCode=InvalidCredentials`, `Coherence.Cloud.AuthClient` →
  `[OnlineManager] Failed to login with Steam: InvalidCredentials`) →
  in-game "connection failed".
- The Coherence server validates the Steam auth ticket against the game's
  real appid server-side; the Spacewar-masked ticket (appid 480) is
  rejected. Nothing local can change that verdict.
- Rule: third-party multiplayer backends (Coherence, Photon, Epic
  account-gated) that vouch for the Steam ticket server-side are the same
  out-of-scope class as publisher backends.

## MECCHA CHAMELEON — round-trip safety

`ayame install` → `ayame uninstall` restores the original EOS SDK
byte-exact; ayame files (winmm, ini, marker) are removed cleanly. Safe to
run over existing OnlineFix installations.
