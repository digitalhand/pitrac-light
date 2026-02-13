package cmd

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:           "pitrac-cli",
	Short:         "PiTrac setup helper",
	Long:          "pitrac-cli - PiTrac setup helper (" + resolvedVersion() + ")",
	SilenceUsage:  true,
	SilenceErrors: true,
	Run: func(cmd *cobra.Command, args []string) {
		printStyledHelp()
	},
	CompletionOptions: cobra.CompletionOptions{
		HiddenDefaultCmd: false,
	},
}

func init() {
	// Override help for root only; subcommands get cobra defaults.
	rootCmd.SetHelpFunc(func(cmd *cobra.Command, args []string) {
		if cmd == rootCmd {
			printStyledHelp()
		} else {
			// Use cobra's built-in help for subcommands.
			cmd.InitDefaultHelpFlag()
			cobra.CheckErr(cmd.UsageFunc()(cmd))
		}
	})
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, colorize(ansiRed, "error: ")+err.Error())
		os.Exit(1)
	}
}

func printStyledHelp() {
	groups := []helpGroup{
		{
			title: "Setup",
			commands: []helpEntry{
				{"doctor", "Pre-flight check (tools, files, env vars)"},
				{"env setup", "Generate env file and update shell profile"},
			},
		},
		{
			title: "Validation",
			commands: []helpEntry{
				{"validate env", "Confirm required env vars are set"},
				{"validate config", "Validate runtime config completeness"},
				{"validate install", "Check which dependencies are installed"},
			},
		},
		{
			title: "Configuration",
			commands: []helpEntry{
				{"config init", "Copy config to ~/.pitrac/config/"},
				{"config args", "Output runtime arguments from env variables"},
			},
		},
		{
			title: "Installation",
			commands: []helpEntry{
				{"install list", "Show available install targets and profiles"},
				{"install <target>", "Install a dependency (--dry-run, --yes)"},
				{"install base", "Install base profile (4 targets)"},
				{"install full", "Install all dependencies"},
				{"install clean", "Show reclaimable build artifacts"},
				{"install clean all", "Remove all build artifacts"},
			},
		},
		{
			title: "Build",
			commands: []helpEntry{
				{"build", "Build PiTrac from source (meson + ninja)"},
			},
		},
		{
			title: "Run",
			commands: []helpEntry{
				{"run pulse-test", "Strobe/shutter pulse test"},
				{"run still", "Capture a single camera still"},
				{"run ball-location", "Continuous ball detection loop"},
				{"run cam", "Normal camera operation"},
				{"run calibrate", "Manual focal-length calibration"},
				{"run auto-calibrate", "Automatic calibration"},
				{"run shutdown", "Shutdown all PiTrac instances"},
			},
		},
		{
			title: "Service",
			commands: []helpEntry{
				{"service start", "Start broker + both cameras"},
				{"service stop", "Stop cameras + broker"},
				{"service status", "Show status of all services"},
				{"service broker start", "Start the ActiveMQ broker"},
				{"service broker stop", "Stop the ActiveMQ broker"},
				{"service broker status", "Check broker status"},
				{"service broker setup", "Create systemd unit + remote access"},
				{"service lm start", "Start camera processes (--camera 1|2)"},
				{"service lm stop", "Stop camera processes"},
				{"service lm status", "Check camera process status"},
			},
		},
		{
			title: "Other",
			commands: []helpEntry{
				{"version", "Print CLI version and build metadata"},
				{"completion", "Generate shell completions"},
			},
		},
	}

	fmt.Printf("pitrac-cli - PiTrac setup helper (%s)\n", resolvedVersion())
	printGroupedHelp(groups)

	fmt.Println(headerText("Quick Start"))
	fmt.Println("  pitrac-cli env setup --force")
	fmt.Printf("  source %s\n", defaultShellRC())
	fmt.Println("  pitrac-cli validate env")
	fmt.Println()
}
