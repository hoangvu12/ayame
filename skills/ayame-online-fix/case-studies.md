# ayame case studies (verified evidence)

## Contents

- Big Walk (1478500) — EOS-based, the reference success
- Pummel Party (880940) — steam-native (no EOS SDK)
- LOCKDOWN Protocol (2780980) — EOS present but unused; Goldberg A/B lesson
- Among Us 3D (3168600) — backend-gated, out of scope
- Heave Ho 2 (2802740) — backend-gated (Coherence), out of scope
- Crime Simulator Prologue (2928280) — backend-gated (Photon NameServer), out of scope
- Crime Simulator (2737070) — how OnlineFix defeats the Photon gate (own servers)
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

## Crime Simulator Prologue (2928280) — backend-gated (Photon, out of scope)

Unity Mono (CookieDev), PUN2/Photon Realtime + Photon Voice, Steamworks
plugin, no EOS SDK → steam-native install works cleanly (loader attached,
restart hook applied, pristine steam_api, game reaches its menu).

- Blocker is at the very first multiplayer handshake:
  `OperationResponse 230: ReturnCode: 32755 (Failed(2): error response
  from Steam) Server: NameServer Address: ns.photonengine.io:5058`.
  Op 230 = Photon NameServer Authenticate; the game passes its Steam auth
  ticket as the Photon auth token, and Photon validates it server-side
  against the game's real appid. Masked ticket (appid 480) → rejected
  before any room exists.
- Diagnostic signature for the class: Photon `ReturnCode: 32755` /
  "error response from Steam" from the NameServer, nothing else wrong in
  Player.log. Distinguish from Among Us 3D, where the gate is the
  publisher's own backend issuing the Photon token instead.
- Rule: Photon Steam-authenticated games are backend-gated regardless of
  who runs the Photon server — no local fix.

## Crime Simulator (2737070) — how OnlineFix defeats the Photon gate

The paid game (prologue = 2928280, same Fusion+PhotonVoice stack) is gated
exactly like the prologue. OnlineFix's release for it works — dissected live
(`Crime.Simulator.v1.42a-OFME`, launcher window: "Connect to crim server #1").

- Stack: `OnlineFix64.dll`/`SteamOverlay64.dll` (their steam_emu, Spacewar
  mask: `RealAppId=2737070 FakeAppId=480`), their `winmm.dll` loader, a WPF
  `Launcher.exe` ("PhotonLauncher") for server/region choice, and `Custom.dll`
  — a Harmony patcher ("Patcher for Crime Simulator") loaded via
  `BepInEx\core\BepInEx.Preloader.dll` that patches
  `Fusion.Photon.Realtime` auth (`AuthenticationValues`, `set_AuthType`,
  `CustomAuthenticationType`) and reads a `customAppID` ini value.
  `dlllist.txt` lists `Assembly-CSharp.dll`/`Fusion.Realtime.dll` renamed
  `.net.org` at launch; `OnlineFix.ini` exposes unused-looking knobs
  `PhotonIntegration` / `EmulateTicket`.
- **The real trick is infrastructure, not the patch**: the game never talks
  to Photon cloud. Live netstat: NameServer protocol to `91.243.81.244:5056`
  (vanilla Photon NS is port 5058) and game/master traffic to
  `91.243.81.177:27002` — OnlineFix runs their own Photon-Server-SDK
  cluster. Their NS evidently accepts the client without validating any
  Steam ticket.
- Player.log proof: `Lobby created -- SUCCESS!`, lobby name
  `d2uy3cje-asia`, then `OpJoinOrCreateRoom 'd2uy3cje-asia_voice' region
  asia` — Steam-emu lobby is the rendezvous layer, their Photon carries
  game state + voice. Multiplayer confirmed live.
- Consequence for ayame: this class cannot be matched with drop-in DLLs.
  It requires operating Photon-compatible servers plus a per-game auth
  patch. ayame keeps diagnosing the class (Photon `ReturnCode: 32755` at
  the NameServer) and pointing users at OnlineFix for these titles.

### Reuse probe (bare Photon client vs their cluster)

Question tested: can an ayame-style client simply talk to OnlineFix's
servers, without any of their binaries? Partial answer, all evidence live
from a standalone C# client built on the game's own `PhotonRealtime.dll`:

- AppId discovery: brute-forced GUID candidates from the prologue's assets
  against Photon's own NameServer (`ns-as.photonengine.io:5058`). The
  prologue's real Photon AppId is
  `8cf6e908-c9a6-44ad-b458-594063098e87` — Photon cloud answers
  `32755 Authentication type 'None' not supported` for it (Steam custom
  auth mandatory). That exact error shape is a reliable appid-validity
  oracle for future games.
- OnlineFix's NS (`91.243.81.244:5056`) accepts bare ENet connections from
  any client and **knows both AppIds**: the prologue's official one and
  their own per-server one (`ecb8a292-3004-4fa9-82ff-72011aa4cadd` for
  "Crime Simulator #1", siblings `9e732327…`/`4bfd0b60…`). Wrong AppIds get
  `Invalid application id`; known ones get `Non token authentication is not
  allowed` — i.e. the gate is AppId allowlist + required token auth, NOT
  Steam-ticket validation.
- What did not work: no AuthValues, Custom(0) with arbitrary/derived
  tokens (params 216/214, token param 221, AuthType=Steam(1) with fake
  hex) → all `InvalidAuthentication`. Parameter codes verified from the
  real library (AppId=224, AppVersion=220, Region=210, AuthType=217,
  AuthParams=216, AuthData=214, Token=221, UserId=225).
- Where the token lives: the launcher passes only
  `online-fix-photon=<appid>`, `online-fix-game`, `online-fix-launcher=<pid>`
  to the game (PEB env dump); launcher fetches the server list live from
  online-fix.me (Cloudflare); per-game state under
  `C:\Users\Public\Documents\OnlineFix\<steam-appid>` (Saves/Stats only).
  The accepted auth payload is generated inside the packed launcher/patcher
  stack and was not extractable from heap/env/disk.
- Verdict: reusing their cluster standalone requires their auth payload —
  captured only by wire-level interception or unpacking their launcher.
  For ayame the answer stays: diagnose the Photon gate, point users at the
  OnlineFix launcher for these titles.

## MECCHA CHAMELEON — round-trip safety

`ayame install` → `ayame uninstall` restores the original EOS SDK
byte-exact; ayame files (winmm, ini, marker) are removed cleanly. Safe to
run over existing OnlineFix installations.
