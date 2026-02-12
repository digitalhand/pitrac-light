package cmd

import (
	"errors"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(validateCmd)
	validateCmd.AddCommand(validateEnvCmd)
	validateCmd.AddCommand(validateConfigCmd)
	validateCmd.AddCommand(validateInstallCmd)
}

var validateCmd = &cobra.Command{
	Use:   "validate",
	Short: "Validate environment, config, or install status",
}

// --- validate env ---

var validateEnvCmd = &cobra.Command{
	Use:   "env",
	Short: "Confirm required env vars are set",
	RunE:  runValidateEnv,
}

func init() {
	validateEnvCmd.Flags().String("env-file", "", "path to env file")
	validateEnvCmd.Flags().String("shell-file", "", "shell rc file expected to source env file")
}

func runValidateEnv(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	envFile, _ := cmd.Flags().GetString("env-file")
	shellFile, _ := cmd.Flags().GetString("shell-file")

	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}
	if shellFile == "" {
		shellFile = defaultShellRC()
	}

	values := envMapFromProcess()
	if st, statErr := os.Stat(envFile); statErr == nil && !st.IsDir() {
		fileValues, loadErr := loadEnvFile(envFile)
		if loadErr != nil {
			return fmt.Errorf("failed to read env file %s: %w", envFile, loadErr)
		}
		for k, v := range fileValues {
			values[k] = v
		}
	} else {
		printStatus(markWarning(), "env_file", "not found: "+envFile)
	}

	failures := 0
	for _, key := range requiredEnvVars {
		val := strings.TrimSpace(values[key])
		if val == "" {
			printStatus(markFailure(), key, "not set")
			failures++
		} else {
			printStatus(markSuccess(), key, val)
		}
	}

	sourceLine := "source " + envFile
	if data, readErr := os.ReadFile(shellFile); readErr == nil {
		if strings.Contains(string(data), sourceLine) {
			printStatus(markSuccess(), "shell_file", "contains "+sourceLine)
		} else {
			printStatus(markWarning(), "shell_file", "missing "+sourceLine)
		}
	} else if errors.Is(readErr, os.ErrNotExist) {
		printStatus(markWarning(), "shell_file", "not found: "+shellFile)
	} else {
		printStatus(markWarning(), "shell_file", "read error: "+readErr.Error())
	}

	if failures > 0 {
		fmt.Println("")
		fmt.Println("env validate failed")
		fmt.Println("next:")
		fmt.Println("  pitrac-cli env setup --force")
		return fmt.Errorf("env validate found %d issue(s)", failures)
	}

	fmt.Println("")
	fmt.Println("env validate passed")
	return nil
}

// --- validate config ---

var validateConfigCmd = &cobra.Command{
	Use:   "config",
	Short: "Validate runtime config completeness",
	RunE:  runValidateConfig,
}

func init() {
	validateConfigCmd.Flags().String("env-file", "", "path to env file")
}

func runValidateConfig(cmd *cobra.Command, args []string) error {
	values, err := resolveConfigEnv(cmd)
	if err != nil {
		return fmt.Errorf("failed to resolve config values: %w", err)
	}

	failures := 0
	for _, key := range requiredEnvVars {
		val := strings.TrimSpace(values[key])
		if val == "" {
			printStatus(markFailure(), key, "not set")
			failures++
		} else {
			printStatus(markSuccess(), key, val)
		}
	}

	if root := strings.TrimSpace(values["PITRAC_ROOT"]); root != "" {
		configPath := filepath.Join(root, "src", "golf_sim_config.json")
		if st, statErr := os.Stat(configPath); statErr != nil || st.IsDir() {
			printStatus(markFailure(), "golf_sim_config", "missing: "+configPath)
			failures++
		} else {
			printStatus(markSuccess(), "golf_sim_config", configPath)
		}
	}

	if share := strings.TrimSpace(values["PITRAC_WEBSERVER_SHARE_DIR"]); share != "" {
		if !strings.HasSuffix(share, "/") {
			printStatus(markWarning(), "PITRAC_WEBSERVER_SHARE_DIR", "does not end with '/'")
		}
	}

	if failures > 0 {
		return fmt.Errorf("config validate found %d required issue(s)", failures)
	}

	fmt.Println("\nconfig validate passed")
	return nil
}

// --- validate install ---

type depCheck struct {
	id    string
	check func() bool
}

var depChecks = []depCheck{
	{"source-deps", func() bool { return dpkgInstalled("build-essential") }},
	{"boost", func() bool { return dpkgInstalled("libboost-all-dev") }},
	{"java", func() bool { return javaVersionOK() }},
	{"msgpack-cxx", func() bool { return dpkgInstalled("libmsgpack-dev") }},
	{"mq-broker", func() bool { return fileExists("/opt/apache-activemq/bin/activemq") }},
	{"activemq-cpp", func() bool { return ldconfigHas("libactivemq-cpp") }},
	{"lgpio", func() bool { return ldconfigHas("liblgpio") }},
	{"libcamera", func() bool { return pkgConfigExists("libcamera") }},
	{"opencv", func() bool { return pkgConfigExists("opencv4") }},
	{"onnx", func() bool { return ldconfigHas("onnxruntime") }},
}

var validateInstallCmd = &cobra.Command{
	Use:   "install",
	Short: "Check which dependencies are installed",
	RunE:  runValidateInstall,
}

func runValidateInstall(cmd *cobra.Command, args []string) error {
	printHeader("Dependency Status")
	fmt.Println()

	installed := 0
	total := len(depChecks)

	for _, dc := range depChecks {
		target, found := findInstallTarget(dc.id)
		desc := dc.id
		if found {
			desc = target.description
		}

		if dc.check() {
			printStatus(markSuccess(), dc.id, desc)
			installed++
		} else {
			printStatus(markFailure(), dc.id, desc)
		}
	}

	fmt.Printf("\n%s %d/%d installed\n", markInfo(), installed, total)

	if installed < total {
		return fmt.Errorf("%d/%d dependencies missing", total-installed, total)
	}
	return nil
}

// --- detection helpers ---

func dpkgInstalled(pkg string) bool {
	out, err := exec.Command("dpkg", "-l", pkg).CombinedOutput()
	if err != nil {
		return false
	}
	return strings.Contains(string(out), "ii")
}

func javaVersionOK() bool {
	out, err := exec.Command("java", "--version").CombinedOutput()
	if err != nil {
		return false
	}
	// Look for "21" or higher in the version output
	s := string(out)
	for _, v := range []string{"21", "22", "23", "24", "25"} {
		if strings.Contains(s, v) {
			return true
		}
	}
	return false
}

func fileExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && !st.IsDir()
}

func ldconfigHas(lib string) bool {
	out, err := exec.Command("ldconfig", "-p").CombinedOutput()
	if err != nil {
		return false
	}
	return strings.Contains(string(out), lib)
}

func pkgConfigExists(pkg string) bool {
	return exec.Command("pkg-config", "--exists", pkg).Run() == nil
}
