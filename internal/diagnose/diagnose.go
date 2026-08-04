// Package diagnose reports the observable state of an ayame installation:
// what is on disk, what the runtime logs prove, and what to do next.
// It exists so a human or an agent can drive the fix without tribal
// knowledge: install -> launch -> diagnose -> branch on the facts.
package diagnose

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/hoangvu12/ayame/internal/install"
)

// LogFacts are the runtime claims extracted from one ayame.log.
type LogFacts struct {
	Path                    string   `json:"path"`
	LoaderAttached          bool     `json:"loader_attached"`
	ProxyAttached           bool     `json:"eos_proxy_attached"`
	RestartHookApplied      bool     `json:"restart_hook_applied"`
	ConnectLoginIntercepted bool     `json:"connect_login_intercepted"`
	CredType                int      `json:"cred_type,omitempty"`
	DeviceIDLoginAs         string   `json:"device_id_login_as,omitempty"`
	Errors                  []string `json:"errors,omitempty"`
}

// Report is everything an agent needs to branch on.
type Report struct {
	GameDir           string     `json:"game_dir"`
	InstallState      string     `json:"install_state"` // installed-eos | installed-steam-native | not-installed | partial
	EosProxyPaths     []string   `json:"eos_proxy_paths,omitempty"`
	WinmmPaths        []string   `json:"winmm_paths,omitempty"`
	SteamLayer        string     `json:"steam_layer"` // goldberg | pristine | unknown
	PristineSteamBaks []string   `json:"pristine_steam_api_baks,omitempty"`
	Logs              []LogFacts `json:"logs,omitempty"`
	Advice            []string   `json:"advice,omitempty"`
}

// Run inspects gameDir and returns the report.
func Run(gameDir string) (*Report, error) {
	gameDir, err := filepath.Abs(gameDir)
	if err != nil {
		return nil, err
	}
	if st, err := os.Stat(gameDir); err != nil || !st.IsDir() {
		return nil, fmt.Errorf("not a directory: %s", gameDir)
	}
	rep := &Report{GameDir: gameDir, SteamLayer: "unknown"}

	backups, _ := install.FindNamedExport(gameDir, install.EosBackup)
	dlls, _ := install.FindNamedExport(gameDir, install.EosName)
	winmms, _ := install.FindNamedExport(gameDir, install.WinmmName)
	inis, _ := install.FindNamedExport(gameDir, install.IniName)

	steamNative := false
	for _, ini := range inis {
		if b, err := os.ReadFile(ini); err == nil && strings.Contains(string(b), "SteamNative=true") {
			steamNative = true
		}
	}
	for _, dll := range dlls {
		if install.FileContainsExport(dll, "ayame eos proxy") {
			rep.EosProxyPaths = append(rep.EosProxyPaths, dll)
		}
	}
	for _, w := range winmms {
		if install.FileContainsExport(w, "ayame winmm loader") {
			rep.WinmmPaths = append(rep.WinmmPaths, w)
		}
	}

	switch {
	case len(rep.EosProxyPaths) > 0 && len(rep.EosProxyPaths) == len(backups):
		rep.InstallState = "installed-eos"
	case len(backups) == 0 && len(rep.WinmmPaths) > 0 && steamNative:
		rep.InstallState = "installed-steam-native"
	case len(rep.EosProxyPaths) == 0 && len(rep.WinmmPaths) == 0:
		rep.InstallState = "not-installed"
	default:
		rep.InstallState = "partial"
	}

	// Steam layer: any steam_settings dir means Goldberg (gbe_fork) was
	// applied; sibling .bak files are the pristine steam_api backups.
	steamSettings, _ := install.FindNamedExport(gameDir, "steam_settings")
	if len(steamSettings) > 0 {
		rep.SteamLayer = "goldberg"
	}
	_ = filepath.WalkDir(gameDir, func(path string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		n := strings.ToLower(d.Name())
		if strings.HasSuffix(n, ".bak") && strings.HasPrefix(n, "steam_api") {
			rep.PristineSteamBaks = append(rep.PristineSteamBaks, path)
			if rep.SteamLayer == "unknown" {
				rep.SteamLayer = "pristine" // a backup only exists where an emulator overwrote
			}
		}
		return nil
	})
	if rep.SteamLayer == "unknown" && len(steamSettings) == 0 {
		rep.SteamLayer = "pristine"
	}

	logs, _ := install.FindNamedExport(gameDir, "ayame.log")
	for _, l := range logs {
		rep.Logs = append(rep.Logs, parseLog(l))
	}

	rep.Advice = advice(rep)
	return rep, nil
}

func parseLog(path string) LogFacts {
	f := LogFacts{Path: path}
	fh, err := os.Open(path)
	if err != nil {
		return f
	}
	defer fh.Close()

	sc := bufio.NewScanner(fh)
	sc.Buffer(make([]byte, 1024*1024), 1024*1024)
	for sc.Scan() {
		line := sc.Text()
		switch {
		case strings.Contains(line, "winmm loader attached"):
			f.LoaderAttached = true
		case strings.Contains(line, "eos proxy attached"):
			f.ProxyAttached = true
		case strings.Contains(line, "hook: SteamAPI_RestartAppIfNecessary"):
			f.RestartHookApplied = true
		case strings.Contains(line, "Connect_Login intercepted"):
			f.ConnectLoginIntercepted = true
			if i := strings.Index(line, "cred type="); i >= 0 {
				n, _ := strconv.Atoi(strings.TrimSpace(line[i+len("cred type="):]))
				f.CredType = n
			}
		case strings.Contains(line, "Connect_Login: DeviceID login as"):
			if a := strings.Index(line, `"`); a >= 0 {
				if b := strings.Index(line[a+1:], `"`); b >= 0 {
					f.DeviceIDLoginAs = line[a+1 : a+1+b]
				}
			}
		case strings.Contains(line, "failed") || strings.Contains(line, "fatal"):
			f.Errors = append(f.Errors, line)
		}
	}
	return f
}

func advice(rep *Report) []string {
	var out []string
	launched := len(rep.Logs) > 0
	loginFired := false
	loginOK := false
	for _, l := range rep.Logs {
		if l.ConnectLoginIntercepted {
			loginFired = true
		}
		if l.DeviceIDLoginAs != "" {
			loginOK = true
		}
	}

	if rep.InstallState == "not-installed" {
		out = append(out, "run: ayame install <gameDir> --appid <real Steam AppID>")
		return out
	}
	if rep.InstallState == "partial" {
		out = append(out, "install state is partial - inspect eos proxies/winmm/ini manually")
	}
	if !launched {
		out = append(out, "no ayame.log yet - launch the game once, then re-run diagnose")
		return out
	}

	if rep.InstallState == "installed-eos" {
		switch {
		case loginOK:
			out = append(out, "EOS DeviceID login succeeded - multiplayer path is live")
		case loginFired:
			out = append(out, "Connect_Login intercepted but no successful DeviceID login yet - check log errors")
		default:
			out = append(out, "EOS proxy loaded but the game never called EOS_Connect_Login: "+
				"EOS is effectively unused and online runs through the Steam mask alone (LOCKDOWN-class game)")
		}
	}
	if rep.SteamLayer == "goldberg" && len(rep.PristineSteamBaks) > 0 {
		out = append(out, "Goldberg overrides the pristine steam_api. For online multiplayer prefer the "+
			"pristine API + Spacewar mask (real persona name, real lobby visibility): rename each .bak back over its dll")
	}
	return out
}
