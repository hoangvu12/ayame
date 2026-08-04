---
name: ayame-online-fix
description: Fix online multiplayer for Windows Steam games using ayame (open-source online-fix replacement) and lua-dl (Steam depot downloader). Use when the user wants to make a game's online multiplayer work, test hosting/joining, install ayame on a game, download a Steam game by appid, or diagnose why multiplayer fails (EOS login, lobby visibility, persona name, Spacewar masking).
license: MIT
---

# ayame online-fix

Make online multiplayer work for Steam games: EOS-based (DeviceID login swap)
and Steam-native (Spacewar mask over the real Steam client).

## Fixed locations

- ayame CLI (prebuilt): `C:/Users/ADMIN/Desktop/nguyenvu/ayame/dist/ayame.exe`
- ayame repo (rebuild with `build.bat`): `C:/Users/ADMIN/Desktop/nguyenvu/ayame`
- lua-dl CLI: `C:/Users/ADMIN/Desktop/nguyenvu/lua-dl/lua-dl.exe`
- game folders: `C:/Users/ADMIN/Desktop/games`
- full design + verified evidence: `C:/Users/ADMIN/Desktop/nguyenvu/ayame/docs/DESIGN.md`
- per-game evidence summaries: `case-studies.md` next to this file

## The workflow (follow exactly)

Run all commands from `C:\Users\ADMIN\Desktop\games` unless stated otherwise.

1. **Download** a game by Steam appid (creates a folder in the games dir,
   auto-applies Goldberg):

   ```powershell
   & "C:\Users\ADMIN\Desktop\nguyenvu\lua-dl\lua-dl.exe" download <APPID>
   ```

2. **Install** the fix (auto-detects mode: EOS DLLs present → eos mode;
   absent → steam-native mode):

   ```powershell
   & "C:\Users\ADMIN\Desktop\nguyenvu\ayame\dist\ayame.exe" install "<gameDir>" --appid <APPID>
   ```

3. **Launch** the game with the real Steam client running and logged in
   (the Steam client is required for both modes; identity comes from it).

4. **Diagnose** after launch — this is the branching point:

   ```powershell
   & "C:\Users\ADMIN\Desktop\nguyenvu\ayame\dist\ayame.exe" diagnose "<gameDir>" --json
   ```

## Branch on diagnose output

| JSON fact | Meaning | Next action |
|---|---|---|
| `connect_login_intercepted: true` + `device_id_login_as` set | EOS DeviceID login succeeded; multiplayer path live | done; host/join test |
| proxy attached but `connect_login_intercepted: false` | EOS unused by the game; the Spacewar mask alone carries multiplayer (LOCKDOWN-class) | done if lobbies work |
| `steam_layer: "goldberg"` + `pristine_steam_api_baks` non-empty | Goldberg overrides the pristine steam_api | restore pristine (see rule below), relaunch, re-diagnose |
| `install_state: "not-installed"` | install step missing or folder wrong | run install |
| no `logs` at all | game never launched with ayame loaded | launch once, then re-diagnose |

## Hard rules (learned from live testing)

1. **Goldberg is never the answer for online multiplayer.** It severs the real
   Steam client (no Spacewar session, no real persona, lobby regressions). If
   diagnose shows goldberg + pristine `.bak` files, restore each
   `steam_api*.dll.bak` over its dll and delete the sibling `steam_settings`.
2. If Goldberg must stay (EOS games where the user wants it), every
   `steam_settings/configs.main.ini` needs `[main::connectivity]`
   `disable_lan_only=1`, or EOS HTTPS to Epic dies. `ayame install` patches the
   configs next to EOS DLLs automatically.
3. **Backend-gated games are out of scope**: if the game's logs show the
   publisher backend rejecting the Steam identity (e.g. Innersloth
   `user_steam_error 3404`, "Failed Connection: Photon Authentication"),
   no local fix works — the check runs on their servers. Say so and stop.
4. All ayame logs collect in `ayame.log` next to the game **exe** (by design).
   Unity game logs: `%USERPROFILE%\AppData\LocalLow\<Studio>\<Game>\Player.log`.
5. Never paste or commit the baked online-fix.me credentials inside lua-dl
   (`internal/onlinefix/`, `bot/src/onlinefix.ts`).

## Rebuilding ayame after code changes

```powershell
cd C:\Users\ADMIN\Desktop\nguyenvu\ayame
.\build.bat
```

Outputs `dist/ayame.exe`, `dist/winmm.dll`, `dist/EOSSDK-Win64-Shipping.dll`.

## Case studies

See `case-studies.md` for the verified per-game evidence: Big Walk (EOS live),
Pummel Party (steam-native), LOCKDOWN Protocol (EOS unused, Goldberg A/B),
Among Us 3D (backend-gated), MECCHA CHAMELEON (round-trip safety).
