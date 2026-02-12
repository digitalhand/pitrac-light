package cmd

import (
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
		"--msg_broker_address", values["PITRAC_MSG_BROKER_FULL_ADDRESS"],
		"--web_server_share_dir", values["PITRAC_WEBSERVER_SHARE_DIR"],
		"--base_image_logging_dir", values["PITRAC_BASE_IMAGE_LOGGING_DIR"],
	}

	if gspro := strings.TrimSpace(values["PITRAC_GSPRO_HOST_ADDRESS"]); gspro != "" {
		cmdArgs = append(cmdArgs, "--gspro_host_address", gspro)
	}

	return cmdArgs, nil
}
