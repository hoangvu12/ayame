// ayame - an open-source online-fix alternative.
//
// Usage:
//
//	ayame install <gameDir> [--appid N] [--language S] [--unlock-dlc] [--force-ini]
//	ayame uninstall <gameDir>
//	ayame status <gameDir>
package main

import (
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/hoangvu12/ayame/internal/install"
)

const version = "0.1.0"

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}

	switch os.Args[1] {
	case "install":
		cmdInstall(os.Args[2:])
	case "uninstall":
		cmdUninstall(os.Args[2:])
	case "status":
		cmdStatus(os.Args[2:])
	case "version", "--version", "-v":
		fmt.Println("ayame", version)
	case "help", "--help", "-h":
		usage()
	default:
		fmt.Fprintf(os.Stderr, "unknown command: %s\n\n", os.Args[1])
		usage()
		os.Exit(2)
	}
}

func usage() {
	fmt.Print(`ayame - open-source online-fix alternative (EOS DeviceID + Steam mask)

Commands:
  install <gameDir>     Apply the fix (renames the original EOS SDK to .of,
                        drops the proxy DLLs and ayame.ini)
    --appid N           The game's real Steam AppID (required for the ini)
    --language S        Game language (default: english)
    --unlock-dlc        Answer "owned" to all DLC checks
    --force-ini         Overwrite an existing ayame.ini
  uninstall <gameDir>   Restore the original EOS SDK and remove ayame files
  status <gameDir>      Show whether the fix is installed
  version               Print version
`)
}

func cmdInstall(args []string) {
	fs := flag.NewFlagSet("install", flag.ExitOnError)
	appid := fs.Uint("appid", 0, "real Steam AppID")
	language := fs.String("language", "", "game language")
	unlock := fs.Bool("unlock-dlc", false, "unlock all DLC checks")
	forceIni := fs.Bool("force-ini", false, "overwrite ayame.ini")
	fs.Parse(reorderFlagsFirst(args))

	if fs.NArg() != 1 {
		fmt.Fprintln(os.Stderr, "usage: ayame install <gameDir> [--appid N] ...")
		os.Exit(2)
	}

	rep, err := install.Install(fs.Arg(0), install.Options{
		AppID:        uint32(*appid),
		Language:     *language,
		UnlockAllDLC: *unlock,
		ForceIni:     *forceIni,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, "install failed:", err)
		os.Exit(1)
	}

	fmt.Println("ayame installed:", rep.Mode)
	for _, p := range rep.PatchedEos {
		fmt.Printf("  eos proxy : %s\n", p)
	}
	if rep.WinmmDir != "" {
		fmt.Printf("  loader    : %s\n", filepath.Join(rep.WinmmDir, install.WinmmName))
	}
	if rep.IniPath != "" {
		fmt.Printf("  config    : %s\n", rep.IniPath)
	}
	if rep.Mode == "steam-native" {
		fmt.Println("\nNo EOS SDK found - steam-native mode: the game keeps the real")
		fmt.Println("steam_api and rides your running Steam client under the mask")
		fmt.Println("appid (FakeAppId, Spacewar 480 by default). Keep Steam running")
		fmt.Println("and logged in, launch the game, host/join as usual.")
	} else {
		fmt.Println("\nKeep Steam running, launch the game, and host/join as usual.")
	}
}

func cmdUninstall(args []string) {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: ayame uninstall <gameDir>")
		os.Exit(2)
	}
	rep, err := install.Uninstall(args[0])
	if err != nil {
		fmt.Fprintln(os.Stderr, "uninstall failed:", err)
		os.Exit(1)
	}
	for _, p := range rep.PatchedEos {
		fmt.Printf("restored: %s\n", p)
	}
	if rep.WinmmDir != "" {
		fmt.Printf("removed loader from: %s\n", rep.WinmmDir)
	}
	fmt.Println("done")
}

func cmdStatus(args []string) {
	if len(args) != 1 {
		fmt.Fprintln(os.Stderr, "usage: ayame status <gameDir>")
		os.Exit(2)
	}
	dir := args[0]

	backups, _ := install.FindNamedExport(dir, install.EosBackup)
	proxies, _ := install.FindNamedExport(dir, install.EosName)
	winmms, _ := install.FindNamedExport(dir, install.WinmmName)

	fmt.Printf("game dir      : %s\n", dir)
	fmt.Printf("eos originals : %d (.of backups)\n", len(backups))
	fmt.Printf("eos dlls      : %d\n", len(proxies))
	fmt.Printf("winmm loaders : %d\n", len(winmms))
	switch {
	case len(backups) > 0 && len(backups) == len(proxies):
		fmt.Println("status        : INSTALLED")
	case len(backups) == 0 && len(winmms) > 0:
		fmt.Println("status        : INSTALLED (steam-native: no EOS SDK, loader only)")
	case len(backups) == 0:
		fmt.Println("status        : not installed")
	default:
		fmt.Println("status        : partially installed - inspect manually")
	}
}

// reorderFlagsFirst moves flag arguments (--x, -x, --x=v) ahead of
// positional arguments so the user can write `ayame install <dir> --appid N`.
func reorderFlagsFirst(args []string) []string {
	boolFlags := map[string]bool{"unlock-dlc": true, "force-ini": true}
	var flags, positional []string
	for i := 0; i < len(args); i++ {
		a := args[i]
		if strings.HasPrefix(a, "-") {
			flags = append(flags, a)
			base := strings.TrimLeft(a, "-")
			base = strings.SplitN(base, "=", 2)[0]
			// A value-bearing flag written as "--appid 480" keeps its value
			// adjacent: carry the next non-dash token along.
			if !boolFlags[base] && !strings.Contains(a, "=") && i+1 < len(args) &&
				!strings.HasPrefix(args[i+1], "-") {
				flags = append(flags, args[i+1])
				i++
			}
			continue
		}
		positional = append(positional, a)
	}
	return append(flags, positional...)
}
