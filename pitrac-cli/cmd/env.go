package cmd

import (
	"bufio"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(envCmd)
	envCmd.AddCommand(envSetupCmd)
	envCmd.AddCommand(envInitCmd)
	envCmd.AddCommand(envShowCmd)
	envCmd.AddCommand(envApplyCmd)
	envCmd.AddCommand(envResetCmd)
}

var envCmd = &cobra.Command{
	Use:   "env",
	Short: "Setup and manage PiTrac environment values",
}

// --- env setup ---

var envSetupCmd = &cobra.Command{
	Use:   "setup",
	Short: "Generate env file and update shell profile",
	RunE:  runEnvSetup,
}

func init() {
	envSetupCmd.Flags().Bool("force", false, "overwrite env file and re-append shell source line")
	envSetupCmd.Flags().String("pitrac-root", "", "path to repository root for PITRAC_ROOT")
	envSetupCmd.Flags().String("env-file", "", "path to generated env file")
	envSetupCmd.Flags().String("shell-file", "", "shell rc file to update")
}

func runEnvSetup(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	force, _ := cmd.Flags().GetBool("force")
	pitracRoot, _ := cmd.Flags().GetString("pitrac-root")
	envFile, _ := cmd.Flags().GetString("env-file")
	shellFile, _ := cmd.Flags().GetString("shell-file")

	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}
	if shellFile == "" {
		shellFile = defaultShellRC()
	}

	pitracRoot = resolvePitracRootValue(pitracRoot)

	shareDir, logDir, err := writeDefaultEnvFile(home, envFile, pitracRoot, force)
	if err != nil {
		return err
	}

	updated, err := applyEnvSourceToShell(envFile, shellFile, force)
	if err != nil {
		return err
	}

	printStatus(markSuccess(), "env_file", envFile)
	printStatus(markSuccess(), "created_dir", strings.TrimSuffix(shareDir, "/"))
	printStatus(markSuccess(), "created_dir", logDir)
	if updated {
		printStatus(markSuccess(), "shell_file", "updated "+shellFile)
	} else {
		printStatus(markSuccess(), "shell_file", "already contains source "+envFile)
	}
	fmt.Println("")
	fmt.Println("next:")
	fmt.Printf("  source %s\n", shellFile)
	fmt.Println("  pitrac-cli validate env")
	return nil
}

// --- env init (hidden) ---

var envInitCmd = &cobra.Command{
	Use:    "init",
	Short:  "Create env file without updating shell profile",
	Hidden: true,
	RunE:   runEnvInit,
}

func init() {
	envInitCmd.Flags().Bool("force", false, "overwrite env file if it already exists")
	envInitCmd.Flags().String("pitrac-root", "", "path to repository root for PITRAC_ROOT")
	envInitCmd.Flags().String("env-file", "", "path to generated env file")
}

func runEnvInit(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	force, _ := cmd.Flags().GetBool("force")
	pitracRoot, _ := cmd.Flags().GetString("pitrac-root")
	envFile, _ := cmd.Flags().GetString("env-file")

	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}

	pitracRoot = resolvePitracRootValue(pitracRoot)

	shareDir, logDir, err := writeDefaultEnvFile(home, envFile, pitracRoot, force)
	if err != nil {
		return err
	}

	printStatus(markSuccess(), "env_file", envFile)
	printStatus(markSuccess(), "created_dir", strings.TrimSuffix(shareDir, "/"))
	printStatus(markSuccess(), "created_dir", logDir)
	fmt.Println("")
	fmt.Println("next:")
	fmt.Printf("  source %s\n", envFile)
	fmt.Println("  pitrac-cli doctor")
	return nil
}

// --- env show (hidden) ---

var envShowCmd = &cobra.Command{
	Use:    "show",
	Short:  "Display current env values",
	Hidden: true,
	RunE:   runEnvShow,
}

func init() {
	envShowCmd.Flags().String("env-file", "", "path to env file")
}

func runEnvShow(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	envFile, _ := cmd.Flags().GetString("env-file")
	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}

	values := map[string]string{}
	if st, statErr := os.Stat(envFile); statErr == nil && !st.IsDir() {
		fileValues, loadErr := loadEnvFile(envFile)
		if loadErr != nil {
			return fmt.Errorf("failed to read env file %s: %w", envFile, loadErr)
		}
		values = fileValues
	}

	fmt.Printf("env_file: %s\n\n", envFile)
	for _, key := range allEnvVars() {
		value := strings.TrimSpace(values[key])
		if value == "" {
			value = strings.TrimSpace(os.Getenv(key))
		}
		if value == "" {
			printStatus(markWarning(), key, "not set")
		} else {
			printStatus(markSuccess(), key, value)
		}
	}
	return nil
}

// --- env apply (hidden) ---

var envApplyCmd = &cobra.Command{
	Use:    "apply",
	Short:  "Append source line to shell profile",
	Hidden: true,
	RunE:   runEnvApply,
}

func init() {
	envApplyCmd.Flags().String("env-file", "", "path to env file")
	envApplyCmd.Flags().String("shell-file", "", "shell rc file to update")
	envApplyCmd.Flags().Bool("force", false, "append source line even if a similar line exists")
}

func runEnvApply(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	envFile, _ := cmd.Flags().GetString("env-file")
	shellFile, _ := cmd.Flags().GetString("shell-file")
	force, _ := cmd.Flags().GetBool("force")

	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}
	if shellFile == "" {
		shellFile = defaultShellRC()
	}

	updated, err := applyEnvSourceToShell(envFile, shellFile, force)
	if err != nil {
		return err
	}

	if updated {
		printStatus(markSuccess(), "shell_file", "updated "+shellFile)
	} else {
		printStatus(markSuccess(), "shell_file", "already contains source "+envFile)
	}
	fmt.Println("next:")
	fmt.Printf("  source %s\n", shellFile)
	fmt.Println("  pitrac-cli doctor")
	return nil
}

// --- helpers ---

func defaultShellRC() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return filepath.Join("~", ".bashrc")
	}
	switch filepath.Base(os.Getenv("SHELL")) {
	case "zsh":
		return filepath.Join(home, ".zshrc")
	case "fish":
		return filepath.Join(home, ".config", "fish", "config.fish")
	default:
		return filepath.Join(home, ".bashrc")
	}
}

func resolvePitracRootValue(input string) string {
	if strings.TrimSpace(input) != "" {
		return input
	}

	detected, err := detectRepoRoot()
	if err == nil {
		return detected
	}
	return "/path/to/pitrac-light"
}

func writeDefaultEnvFile(home, envFile, pitracRoot string, force bool) (string, string, error) {
	shareDir := filepath.Join(home, "LM_Shares", "Images") + "/"
	logDir := filepath.Join(home, "LM_Shares", "PiTracLogs")

	content := buildEnvFile(map[string]string{
		"PITRAC_ROOT":                    pitracRoot,
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     shareDir,
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  logDir,
		"PITRAC_WEB_SERVER_URL":          "http://localhost:8080",
	})

	if err := os.MkdirAll(filepath.Dir(envFile), 0o755); err != nil {
		return "", "", fmt.Errorf("failed to create env directory: %w", err)
	}

	if !force {
		if _, err := os.Stat(envFile); err == nil {
			return "", "", fmt.Errorf("env file already exists: %s (use --force to overwrite)", envFile)
		}
	}

	if err := os.WriteFile(envFile, []byte(content), 0o644); err != nil {
		return "", "", fmt.Errorf("failed to write env file: %w", err)
	}

	_ = os.MkdirAll(strings.TrimSuffix(shareDir, "/"), 0o755)
	_ = os.MkdirAll(logDir, 0o755)

	return shareDir, logDir, nil
}

func applyEnvSourceToShell(envFile, shellFile string, force bool) (bool, error) {
	if st, statErr := os.Stat(envFile); statErr != nil || st.IsDir() {
		return false, fmt.Errorf("env file does not exist: %s", envFile)
	}

	line := "source " + envFile

	existing := ""
	if data, readErr := os.ReadFile(shellFile); readErr == nil {
		existing = string(data)
	} else if !errors.Is(readErr, os.ErrNotExist) {
		return false, fmt.Errorf("failed to read shell file %s: %w", shellFile, readErr)
	}

	if !force && strings.Contains(existing, line) {
		return false, nil
	}

	if err := os.MkdirAll(filepath.Dir(shellFile), 0o755); err != nil {
		return false, fmt.Errorf("failed to create shell file directory: %w", err)
	}

	f, openErr := os.OpenFile(shellFile, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if openErr != nil {
		return false, fmt.Errorf("failed to open shell file %s: %w", shellFile, openErr)
	}
	defer f.Close()

	if existing != "" && !strings.HasSuffix(existing, "\n") {
		if _, err := f.WriteString("\n"); err != nil {
			return false, fmt.Errorf("failed writing newline to shell file: %w", err)
		}
	}

	if _, err := f.WriteString("\n# PiTrac environment\n" + line + "\n"); err != nil {
		return false, fmt.Errorf("failed writing source line to shell file: %w", err)
	}

	return true, nil
}

func detectRepoRoot() (string, error) {
	wd, err := os.Getwd()
	if err != nil {
		return "", err
	}

	candidates := []string{
		wd,
		filepath.Dir(wd),
		filepath.Dir(filepath.Dir(wd)),
	}
	for _, c := range candidates {
		if c == "" || c == "." || c == "/" {
			continue
		}
		if hasRepoMarkers(c) {
			return c, nil
		}
	}

	return "", errors.New("could not detect repository root")
}

func hasRepoMarkers(path string) bool {
	srcMeson := filepath.Join(path, "src", "meson.build")
	readme := filepath.Join(path, "README.md")

	if st, err := os.Stat(srcMeson); err != nil || st.IsDir() {
		return false
	}
	if st, err := os.Stat(readme); err != nil || st.IsDir() {
		return false
	}
	return true
}

func buildEnvFile(values map[string]string) string {
	var b strings.Builder
	b.WriteString("# PiTrac environment generated by pitrac-cli env init\n")
	b.WriteString("# source this file before using legacy run scripts\n")
	b.WriteString("\n")

	writeExport := func(key string) {
		b.WriteString("export ")
		b.WriteString(key)
		b.WriteString("=\"")
		b.WriteString(strings.ReplaceAll(values[key], "\"", "\\\""))
		b.WriteString("\"\n")
	}

	writeExport("PITRAC_ROOT")
	writeExport("PITRAC_MSG_BROKER_FULL_ADDRESS")
	writeExport("PITRAC_WEBSERVER_SHARE_DIR")
	writeExport("PITRAC_BASE_IMAGE_LOGGING_DIR")
	b.WriteString("\n")
	b.WriteString("# Optional overrides\n")
	b.WriteString("# export PITRAC_GSPRO_HOST_ADDRESS=\"10.0.0.51\"\n")
	writeExport("PITRAC_WEB_SERVER_URL")
	b.WriteString("\n")

	return b.String()
}

func defaultEnvFilePath(home string) string {
	return filepath.Join(home, ".pitrac", "config", "pitrac.env")
}

func confirmProceed(prompt string) bool {
	fmt.Print(prompt)
	reader := bufio.NewReader(os.Stdin)
	line, err := reader.ReadString('\n')
	if err != nil {
		return false
	}
	answer := strings.TrimSpace(strings.ToLower(line))
	return answer == "y" || answer == "yes"
}

// --- env reset ---

var envResetCmd = &cobra.Command{
	Use:   "reset",
	Short: "Remove PiTrac environment configuration and clean shell profile",
	Long: `Removes PiTrac environment files and cleans up shell profile.

This command will:
  1. Remove ~/.pitrac/config/pitrac.env file
  2. Remove source line from shell RC file (~/.bashrc, ~/.zshrc, etc.)
  3. Display commands to unset environment variables in current session

Use this when switching repositories or fixing incorrect PITRAC_ROOT paths.`,
	RunE: runEnvReset,
}

func init() {
	envResetCmd.Flags().Bool("yes", false, "skip confirmation prompt")
	envResetCmd.Flags().String("shell-file", "", "shell rc file to clean (default: auto-detect)")
	envResetCmd.Flags().Bool("keep-dirs", false, "keep created directories (shares, logs)")
}

func runEnvReset(cmd *cobra.Command, args []string) error {
	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	yes, _ := cmd.Flags().GetBool("yes")
	shellFile, _ := cmd.Flags().GetString("shell-file")
	keepDirs, _ := cmd.Flags().GetBool("keep-dirs")

	if shellFile == "" {
		shellFile = defaultShellRC()
	}

	envFile := defaultEnvFilePath(home)
	pitracDir := filepath.Join(home, ".pitrac")

	printHeader("Reset PiTrac Environment")

	// Check what exists
	envFileExists := pathExists(envFile)
	shellFileExists := pathExists(shellFile)
	pitracDirExists := pathExists(pitracDir)

	if !envFileExists && !shellFileExists && !pitracDirExists {
		fmt.Println("✓ No PiTrac environment configuration found")
		return nil
	}

	// Show what will be removed
	fmt.Println("The following will be removed:")
	if envFileExists {
		fmt.Printf("  • %s\n", envFile)
	}
	if shellFileExists {
		fmt.Printf("  • source line in %s\n", shellFile)
	}
	if pitracDirExists && !keepDirs {
		fmt.Printf("  • %s (entire directory)\n", pitracDir)
	}
	fmt.Println()

	// Confirm
	if !yes {
		if !confirmProceed("Proceed? [y/N]: ") {
			fmt.Println("Cancelled")
			return nil
		}
	}

	// Remove env file
	if envFileExists {
		if err := os.Remove(envFile); err != nil {
			return fmt.Errorf("failed to remove env file: %w", err)
		}
		printStatus(markSuccess(), "removed", envFile)
	}

	// Clean shell RC file
	if shellFileExists {
		cleaned, err := removeEnvSourceFromShell(envFile, shellFile)
		if err != nil {
			return fmt.Errorf("failed to clean shell file: %w", err)
		}
		if cleaned {
			printStatus(markSuccess(), "cleaned", shellFile)
		} else {
			printStatus(markInfo(), "no_change", shellFile+" (no source line found)")
		}
	}

	// Remove .pitrac directory if empty or requested
	if pitracDirExists && !keepDirs {
		if err := os.RemoveAll(pitracDir); err != nil {
			return fmt.Errorf("failed to remove .pitrac directory: %w", err)
		}
		printStatus(markSuccess(), "removed", pitracDir)
	}

	fmt.Println()
	fmt.Println("Environment reset complete!")
	fmt.Println()
	fmt.Println("To unset variables in current session, run:")
	fmt.Println("  unset PITRAC_ROOT PITRAC_MSG_BROKER_FULL_ADDRESS PITRAC_WEBSERVER_SHARE_DIR PITRAC_BASE_IMAGE_LOGGING_DIR")
	fmt.Println()
	fmt.Println("To set up new environment:")
	fmt.Printf("  cd /path/to/pitrac-light\n")
	fmt.Println("  pitrac-cli env setup")
	fmt.Printf("  source %s\n", shellFile)

	return nil
}

func removeEnvSourceFromShell(envFile, shellFile string) (bool, error) {
	content, err := os.ReadFile(shellFile)
	if err != nil {
		if os.IsNotExist(err) {
			return false, nil
		}
		return false, err
	}

	lines := strings.Split(string(content), "\n")
	var newLines []string
	removed := false

	for _, line := range lines {
		// Skip lines that source the env file
		if strings.Contains(line, envFile) && strings.Contains(line, "source") {
			removed = true
			continue
		}
		// Also skip the comment line if present
		if strings.Contains(line, "PiTrac environment") {
			continue
		}
		newLines = append(newLines, line)
	}

	if !removed {
		return false, nil
	}

	newContent := strings.Join(newLines, "\n")
	if err := os.WriteFile(shellFile, []byte(newContent), 0644); err != nil {
		return false, err
	}

	return true, nil
}

func pathExists(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}
