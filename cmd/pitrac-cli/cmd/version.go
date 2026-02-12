package cmd

import (
	"fmt"
	"runtime/debug"
	"strings"

	"github.com/spf13/cobra"
)

// These are set at build time via -ldflags, e.g.:
// go build -ldflags "-X cmd.Version=v0.1.0 -X cmd.Commit=$(git rev-parse --short HEAD)"
var (
	Version = "dev"
	Commit  = ""
	Date    = ""
)

func init() {
	rootCmd.AddCommand(versionCmd)
}

var versionCmd = &cobra.Command{
	Use:   "version",
	Short: "Print CLI version and build metadata",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Printf("pitrac-cli %s\n", resolvedVersion())
	},
}

func resolvedVersion() string {
	v := Version
	c := Commit
	d := Date

	if info, ok := debug.ReadBuildInfo(); ok {
		if (v == "" || v == "dev") && info.Main.Version != "" && info.Main.Version != "(devel)" {
			v = info.Main.Version
		}

		if c == "" || d == "" {
			for _, s := range info.Settings {
				switch s.Key {
				case "vcs.revision":
					if c == "" {
						c = s.Value
					}
				case "vcs.time":
					if d == "" {
						d = s.Value
					}
				}
			}
		}
	}

	if c != "" && len(c) > 12 {
		c = c[:12]
	}

	parts := []string{v}
	if c != "" {
		parts = append(parts, c)
	}
	if d != "" {
		parts = append(parts, d)
	}
	return strings.Join(parts, " ")
}
