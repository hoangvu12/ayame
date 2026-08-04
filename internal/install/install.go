// Package install applies or removes the ayame fix inside a game folder.
package install

import (
	"bytes"
	"crypto/sha256"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/hoangvu12/ayame/internal/payloads"
)

const (
	EosName    = "EOSSDK-Win64-Shipping.dll"
	EosBackup  = "EOSSDK-Win64-Shipping.of"
	IniName    = "ayame.ini"
	WinmmName  = "winmm.dll"
	MarkerFile = "ayame_installed.txt"
)

// skipDirs are never scanned for EOS DLLs.
var skipDirs = map[string]bool{
	"_commonredist": true,
	"__redist":      true,
	"redists":       true,
	"directx":       true,
	"vcredist":      true,
	"support":       true,
	"saves":         true,
	"savedgames":    true,
}

type Options struct {
	AppID        uint32
	Language     string
	UnlockAllDLC bool
	ForceIni     bool
}

type Report struct {
	PatchedEos []string
	WinmmDir   string
	IniPath    string
	// Mode is "eos" (EOS proxy + loader) or "steam-native" (loader only:
	// the game keeps the real steam_api and rides the real Steam client
	// under FakeAppId, Spacewar by default).
	Mode string
}

// Install applies the fix to gameDir.
func Install(gameDir string, opts Options) (*Report, error) {
	gameDir, err := filepath.Abs(gameDir)
	if err != nil {
		return nil, err
	}
	if st, err := os.Stat(gameDir); err != nil || !st.IsDir() {
		return nil, fmt.Errorf("not a directory: %s", gameDir)
	}

	rep := &Report{}

	rep.Mode = "eos"
	eosTargets, err := findEosDlls(gameDir)
	if err != nil {
		return nil, err
	}
	// No EOS SDK: fall back to steam-native mode — the game keeps the real
	// steam_api.dll and the winmm loader masks the appid so SteamAPI_Init
	// succeeds against the running Steam client (Spacewar by default).
	// The EOS proxy is simply skipped.
	if len(eosTargets) == 0 {
		rep.Mode = "steam-native"
	}
	for _, target := range eosTargets {
		if err := installEosProxy(target); err != nil {
			return nil, fmt.Errorf("%s: %w", target, err)
		}
		rep.PatchedEos = append(rep.PatchedEos, target)
	}

	exeDir, err := findExeDir(gameDir)
	if err != nil {
		return nil, err
	}
	if exeDir != "" {
		if err := installWinmm(exeDir); err != nil {
			return nil, err
		}
		rep.WinmmDir = exeDir

		iniPath := filepath.Join(exeDir, IniName)
		if err := writeIni(iniPath, opts, rep.Mode == "steam-native"); err != nil {
			return nil, err
		}
		rep.IniPath = iniPath

		if err := os.WriteFile(filepath.Join(exeDir, MarkerFile),
			[]byte("ayame v0 install: winmm.dll, ayame.ini\n"), 0o644); err != nil {
			return nil, err
		}
	}

	// Goldberg (gbe_fork) detours connect()/sendto()/WSAConnect() process-wide
	// for its LAN emulation, which makes EOS's HTTPS to Epic fail with
	// connection-refused. Its configs.main.ini opt-out is disable_lan_only=1.
	for _, target := range eosTargets {
		patchGoldbergConfig(filepath.Dir(target))
	}

	return rep, nil
}

// patchGoldbergConfig adds disable_lan_only=1 to a gbe_fork steam_settings
// config sitting next to an EOS DLL (Goldberg installs its settings beside
// the steam_api it replaced). Missing files/folders are ignored — this only
// applies when Goldberg is present.
func patchGoldbergConfig(dir string) {
	cfg := filepath.Join(dir, "steam_settings", "configs.main.ini")
	data, err := os.ReadFile(cfg)
	if err != nil {
		return
	}
	if strings.Contains(string(data), "disable_lan_only") {
		return
	}
	out := string(data)
	if strings.Contains(out, "[main::connectivity]") {
		out = strings.Replace(out, "[main::connectivity]",
			"[main::connectivity]\r\ndisable_lan_only=1", 1)
	} else {
		out += "\r\n[main::connectivity]\r\ndisable_lan_only=1\r\n"
	}
	if err := os.WriteFile(cfg, []byte(out), 0o644); err == nil {
		fmt.Printf("  goldberg    : disable_lan_only=1 added to %s\n", cfg)
	}
}

func installEosProxy(dllPath string) error {
	dir := filepath.Dir(dllPath)
	backup := filepath.Join(dir, EosBackup)

	if _, err := os.Stat(backup); os.IsNotExist(err) {
		// First install: move the original SDK aside.
		if err := os.Rename(dllPath, backup); err != nil {
			return fmt.Errorf("rename original: %w", err)
		}
	} else {
		// A .of already exists. If the current dll is the original (same
		// size family), refuse rather than destroy it; otherwise overwrite.
		// We detect our own proxy by the embedded marker string.
		if !fileContains(dllPath, "ayame eos proxy") {
			return fmt.Errorf("%s exists but %s does not look like an ayame proxy; "+
				"remove it manually and re-run", EosBackup, filepath.Base(dllPath))
		}
	}

	return os.WriteFile(dllPath, payloads.EosProxy, 0o644)
}

func installWinmm(exeDir string) error {
	dst := filepath.Join(exeDir, WinmmName)

	if existing, err := os.ReadFile(dst); err == nil {
		if bytes.Equal(existing, payloads.Winmm) {
			return nil // already ours
		}
		if !bytes.Contains(existing, []byte("ayame")) {
			bak := dst + ".bak"
			if _, err := os.Stat(bak); os.IsNotExist(err) {
				if err := os.Rename(dst, bak); err != nil {
					return fmt.Errorf("backup existing winmm.dll: %w", err)
				}
			}
		}
	}
	return os.WriteFile(dst, payloads.Winmm, 0o644)
}

func writeIni(path string, opts Options, steamNative bool) error {
	if _, err := os.Stat(path); err == nil && !opts.ForceIni {
		return nil // keep the user's existing config
	}

	lang := opts.Language
	if lang == "" {
		lang = "english"
	}
	unlock := "false"
	if opts.UnlockAllDLC {
		unlock = "true"
	}

	var b strings.Builder
	b.WriteString("; ayame config - see docs/DESIGN.md\r\n")
	b.WriteString("[Main]\r\n")
	fmt.Fprintf(&b, "RealAppId=%d\r\n", opts.AppID)
	b.WriteString("FakeAppId=480\r\n")
	fmt.Fprintf(&b, "Language=%s\r\n", lang)
	fmt.Fprintf(&b, "UnlockAllDLC=%s\r\n", unlock)
	if steamNative {
		b.WriteString("; steam-native: no EOS SDK in this game. The loader masks\r\n")
		b.WriteString("; the appid to FakeAppId and the game rides the real Steam\r\n")
		b.WriteString("; client for matchmaking (keep Steam running).\r\n")
		b.WriteString("SteamNative=true\r\n")
	} else {
		b.WriteString("\r\n[EOS]\r\nDeviceIdAuth=true\r\n")
	}

	return os.WriteFile(path, []byte(b.String()), 0o644)
}

// Uninstall restores the original EOS SDKs and removes ayame files.
func Uninstall(gameDir string) (*Report, error) {
	gameDir, err := filepath.Abs(gameDir)
	if err != nil {
		return nil, err
	}
	rep := &Report{}

	// Restore EOS originals.
	backups, err := findNamed(gameDir, EosBackup)
	if err != nil {
		return nil, err
	}
	for _, backup := range backups {
		dir := filepath.Dir(backup)
		current := filepath.Join(dir, EosName)
		if !fileContains(current, "ayame eos proxy") {
			return nil, fmt.Errorf("%s is not an ayame proxy; refusing to overwrite it", current)
		}
		if err := os.Remove(current); err != nil && !os.IsNotExist(err) {
			return nil, err
		}
		if err := os.Rename(backup, current); err != nil {
			return nil, err
		}
		rep.PatchedEos = append(rep.PatchedEos, current)
	}

	// Remove winmm.dll + ini if ours.
	markers, err := findNamed(gameDir, MarkerFile)
	if err != nil {
		return nil, err
	}
	for _, marker := range markers {
		dir := filepath.Dir(marker)
		if winmmPath := filepath.Join(dir, WinmmName); fileContains(winmmPath, "ayame winmm loader") {
			os.Remove(winmmPath)
			rep.WinmmDir = dir
		}
		os.Remove(marker)
		if rep.IniPath == "" {
			rep.IniPath = filepath.Join(dir, IniName)
		}
	}

	return rep, nil
}

func findEosDlls(root string) ([]string, error) {
	return findNamed(root, EosName)
}

// FindNamedExport is findNamed exported for the CLI's status command.
func FindNamedExport(root, name string) ([]string, error) {
	return findNamed(root, name)
}

// FileContainsExport exposes fileContains for sibling packages (diagnose).
func FileContainsExport(path, needle string) bool {
	return fileContains(path, needle)
}

func findNamed(root, name string) ([]string, error) {
	var out []string
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil // soft-walk: skip unreadable entries
		}
		if d.IsDir() {
			if strings.EqualFold(d.Name(), name) {
				out = append(out, path)
			}
			if skipDirs[strings.ToLower(d.Name())] {
				return filepath.SkipDir
			}
			return nil
		}
		if strings.EqualFold(d.Name(), name) {
			out = append(out, path)
		}
		return nil
	})
	return out, err
}

// findExeDir picks the directory holding the game executable: the dir with
// the largest .exe (crash handlers and redist launchers are tiny).
func findExeDir(root string) (string, error) {
	best := ""
	var bestSize int64 = -1
	err := filepath.WalkDir(root, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if d.IsDir() {
			if skipDirs[strings.ToLower(d.Name())] {
				return filepath.SkipDir
			}
			return nil
		}
		if !strings.EqualFold(filepath.Ext(d.Name()), ".exe") {
			return nil
		}
		name := strings.ToLower(d.Name())
		if strings.Contains(name, "crash") || strings.Contains(name, "redist") ||
			strings.Contains(name, "setup") || strings.Contains(name, "unins") ||
			strings.Contains(name, "dxsetup") || strings.Contains(name, "vcredist") {
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return nil
		}
		if info.Size() > bestSize {
			bestSize = info.Size()
			best = filepath.Dir(path)
		}
		return nil
	})
	if err != nil {
		return "", err
	}
	return best, nil
}

func fileContains(path, needle string) bool {
	f, err := os.Open(path)
	if err != nil {
		return false
	}
	defer f.Close()

	buf := make([]byte, 512*1024)
	need := []byte(needle)
	var tail []byte
	for {
		n, err := f.Read(buf)
		if n > 0 {
			chunk := append(append([]byte{}, tail...), buf[:n]...)
			if bytes.Contains(chunk, need) {
				return true
			}
			if len(chunk) > len(need) {
				tail = chunk[len(chunk)-len(need):]
			} else {
				tail = chunk
			}
		}
		if err == io.EOF {
			return false
		}
		if err != nil {
			return false
		}
	}
}

// sha256sum is a small helper for future integrity checks.
func sha256sum(b []byte) string {
	h := sha256.Sum256(b)
	return fmt.Sprintf("%x", h)
}
