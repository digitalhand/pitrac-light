package cmd

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(configCmd)
	configCmd.AddCommand(configArgsCmd)
	configCmd.AddCommand(configInitCmd)
	configCmd.AddCommand(configDetectionCmd)
}

var configCmd = &cobra.Command{
	Use:   "config",
	Short: "Build runtime arguments from env variables",
}

var configArgsCmd = &cobra.Command{
	Use:   "args",
	Short: "Output runtime arguments from env variables",
	RunE:  runConfigArgs,
}

var configInitCmd = &cobra.Command{
	Use:   "init",
	Short: "Copy golf_sim_config.json to ~/.pitrac/config/",
	RunE:  runConfigInit,
}

func init() {
	configArgsCmd.Flags().String("env-file", "", "path to env file")
	configInitCmd.Flags().Bool("force", false, "overwrite config if it already exists")
	configDetectionCmd.Flags().String("method", "", "flight detection method: legacy, experimental, experimental_sahi")
	configDetectionCmd.Flags().String("placement-method", "", "placed ball detection method: legacy, experimental")
}

func runConfigArgs(cmd *cobra.Command, args []string) error {
	values, err := resolveConfigEnv(cmd)
	if err != nil {
		return fmt.Errorf("failed to resolve config values: %w", err)
	}

	cmdArgs, err := buildCommonArgs(values)
	if err != nil {
		return err
	}

	fmt.Println(strings.Join(cmdArgs, " "))
	return nil
}

func runConfigInit(cmd *cobra.Command, args []string) error {
	pitracRoot := strings.TrimSpace(os.Getenv("PITRAC_ROOT"))
	if pitracRoot == "" {
		detected, err := detectRepoRoot()
		if err != nil {
			return fmt.Errorf("PITRAC_ROOT not set and could not detect repo root: %w", err)
		}
		pitracRoot = detected
	}

	srcConfig := filepath.Join(pitracRoot, "src", "golf_sim_config.json")
	if _, err := os.Stat(srcConfig); err != nil {
		return fmt.Errorf("source config not found: %s", srcConfig)
	}

	home, err := os.UserHomeDir()
	if err != nil {
		return fmt.Errorf("failed to resolve home dir: %w", err)
	}

	destDir := filepath.Join(home, ".pitrac", "config")
	destFile := filepath.Join(destDir, "golf_sim_config.json")

	force, _ := cmd.Flags().GetBool("force")
	if !force {
		if _, err := os.Stat(destFile); err == nil {
			return fmt.Errorf("config already exists: %s (use --force to overwrite)", destFile)
		}
	}

	if err := os.MkdirAll(destDir, 0o755); err != nil {
		return fmt.Errorf("failed to create config directory: %w", err)
	}

	if err := copyFile(srcConfig, destFile); err != nil {
		return fmt.Errorf("failed to copy config: %w", err)
	}

	printStatus(markSuccess(), "config", destFile)
	return nil
}

func copyFile(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()

	out, err := os.Create(dst)
	if err != nil {
		return err
	}
	defer out.Close()

	if _, err := io.Copy(out, in); err != nil {
		return err
	}
	return out.Close()
}

func resolveConfigEnv(cmd *cobra.Command) (map[string]string, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return nil, err
	}

	envFile, _ := cmd.Flags().GetString("env-file")
	if envFile == "" {
		envFile = defaultEnvFilePath(home)
	}

	values := envMapFromProcess()
	if st, statErr := os.Stat(envFile); statErr == nil && !st.IsDir() {
		fileValues, loadErr := loadEnvFile(envFile)
		if loadErr != nil {
			return nil, loadErr
		}
		for k, v := range fileValues {
			values[k] = v
		}
	}

	return values, nil
}

// resolveConfigFile returns the path to golf_sim_config.json, preferring
// ~/.pitrac/config/ if it exists, falling back to $PITRAC_ROOT/src/.
func resolveConfigFile(pitracRoot string) string {
	if home, err := os.UserHomeDir(); err == nil {
		userConfig := filepath.Join(home, ".pitrac", "config", "golf_sim_config.json")
		if _, err := os.Stat(userConfig); err == nil {
			return userConfig
		}
	}
	return filepath.Join(pitracRoot, "src", "golf_sim_config.json")
}

func buildCommonArgs(values map[string]string) ([]string, error) {
	for _, key := range requiredEnvVars {
		if strings.TrimSpace(values[key]) == "" {
			return nil, errors.New("missing required env var: " + key)
		}
	}

	configFile := resolveConfigFile(values["PITRAC_ROOT"])

	cmdArgs := []string{
		"--config_file", configFile,
		"--run_single_pi",
		"--msg_broker_address", values["PITRAC_MSG_BROKER_FULL_ADDRESS"],
		"--web_server_share_dir", values["PITRAC_WEBSERVER_SHARE_DIR"],
		"--base_image_logging_dir", values["PITRAC_BASE_IMAGE_LOGGING_DIR"],
	}

	// PITRAC_SIM_HOST_ADDRESS is the preferred env var; fall back to
	// the deprecated PITRAC_GSPRO_HOST_ADDRESS for backward compatibility.
	sim := strings.TrimSpace(values["PITRAC_SIM_HOST_ADDRESS"])
	if sim == "" {
		sim = strings.TrimSpace(values["PITRAC_GSPRO_HOST_ADDRESS"])
	}
	if sim != "" {
		cmdArgs = append(cmdArgs, "--gspro_host_address", sim)
	}

	return cmdArgs, nil
}

// ─── config detection ────────────────────────────────────────────────

var configDetectionCmd = &cobra.Command{
	Use:   "detection",
	Short: "Show or set ball detection method in golf_sim_config.json",
	Long: `Show or set ball detection methods in golf_sim_config.json.

Without flags, prints the current detection methods.

Flags:
  --method           Flight detection: legacy, experimental, experimental_sahi
  --placement-method Placed ball detection: legacy, experimental`,
	RunE: runConfigDetection,
}

var validFlightMethods = []string{"legacy", "experimental", "experimental_sahi"}
var validPlacementMethods = []string{"legacy", "experimental"}

func runConfigDetection(cmd *cobra.Command, args []string) error {
	pitracRoot := strings.TrimSpace(os.Getenv("PITRAC_ROOT"))
	if pitracRoot == "" {
		detected, err := detectRepoRoot()
		if err != nil {
			return fmt.Errorf("PITRAC_ROOT not set and could not detect repo root: %w", err)
		}
		pitracRoot = detected
	}

	configPath := resolveConfigFile(pitracRoot)

	method, _ := cmd.Flags().GetString("method")
	placementMethod, _ := cmd.Flags().GetString("placement-method")

	// Validate flags before touching the file.
	if method != "" && !stringInSlice(method, validFlightMethods) {
		return fmt.Errorf("invalid --method %q; allowed: %s", method, strings.Join(validFlightMethods, ", "))
	}
	if placementMethod != "" && !stringInSlice(placementMethod, validPlacementMethods) {
		return fmt.Errorf("invalid --placement-method %q; allowed: %s", placementMethod, strings.Join(validPlacementMethods, ", "))
	}

	data, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}

	var config map[string]interface{}
	if err := json.Unmarshal(data, &config); err != nil {
		return fmt.Errorf("failed to parse config JSON: %w", err)
	}

	ballID, err := configNestedMap(config, "gs_config", "ball_identification")
	if err != nil {
		return fmt.Errorf("config structure error: %w", err)
	}

	// If no flags, show current values and return.
	if method == "" && placementMethod == "" {
		cur := stringVal(ballID, "kDetectionMethod", "legacy")
		curPlacement := stringVal(ballID, "kBallPlacementDetectionMethod", "legacy")
		printStatus(markInfo(), "kDetectionMethod", cur)
		printStatus(markInfo(), "kBallPlacementDetectionMethod", curPlacement)
		fmt.Println()
		fmt.Printf("flight methods:    %s\n", strings.Join(validFlightMethods, ", "))
		fmt.Printf("placement methods: %s\n", strings.Join(validPlacementMethods, ", "))
		return nil
	}

	// Apply changes.
	if method != "" {
		ballID["kDetectionMethod"] = method
	}
	if placementMethod != "" {
		ballID["kBallPlacementDetectionMethod"] = placementMethod
	}

	out, err := json.MarshalIndent(config, "", "    ")
	if err != nil {
		return fmt.Errorf("failed to marshal config: %w", err)
	}
	// json.MarshalIndent doesn't add a trailing newline.
	out = append(out, '\n')

	if err := os.WriteFile(configPath, out, 0o644); err != nil {
		return fmt.Errorf("failed to write config: %w", err)
	}

	if method != "" {
		printStatus(markSuccess(), "kDetectionMethod", method)
	}
	if placementMethod != "" {
		printStatus(markSuccess(), "kBallPlacementDetectionMethod", placementMethod)
	}
	fmt.Printf("updated %s\n", configPath)
	return nil
}

// configNestedMap traverses nested JSON maps and returns the innermost one.
func configNestedMap(root map[string]interface{}, keys ...string) (map[string]interface{}, error) {
	current := root
	for _, k := range keys {
		val, ok := current[k]
		if !ok {
			return nil, fmt.Errorf("key %q not found", k)
		}
		next, ok := val.(map[string]interface{})
		if !ok {
			return nil, fmt.Errorf("key %q is not an object", k)
		}
		current = next
	}
	return current, nil
}

func stringVal(m map[string]interface{}, key, fallback string) string {
	if v, ok := m[key]; ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return fallback
}

func stringInSlice(s string, list []string) bool {
	for _, v := range list {
		if v == s {
			return true
		}
	}
	return false
}
