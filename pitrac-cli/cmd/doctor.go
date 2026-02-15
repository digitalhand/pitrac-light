package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	"github.com/spf13/cobra"
)

type checkResult struct {
	name     string
	required bool
	ok       bool
	detail   string
}

func init() {
	rootCmd.AddCommand(doctorCmd)
}

var doctorCmd = &cobra.Command{
	Use:   "doctor",
	Short: "Pre-flight check (tools, files, env vars)",
	RunE: func(cmd *cobra.Command, args []string) error {
		return runDoctor()
	},
}

func runDoctor() error {
	results := make([]checkResult, 0)
	repoRoot, err := detectRepoRoot()
	if err != nil {
		if wd, wdErr := os.Getwd(); wdErr == nil {
			repoRoot = wd
		} else {
			repoRoot = "."
		}
	}

	requiredTools := []string{"meson", "ninja", "cmake", "pkg-config", "g++"}
	for _, tool := range requiredTools {
		if path, err := exec.LookPath(tool); err == nil {
			results = append(results, checkResult{
				name:     "tool:" + tool,
				required: true,
				ok:       true,
				detail:   path,
			})
		} else {
			results = append(results, checkResult{
				name:     "tool:" + tool,
				required: true,
				ok:       false,
				detail:   "not found in PATH",
			})
		}
	}

	requiredFiles := []string{
		"src/meson.build",
		"src/golf_sim_config.json",
		"assets/models/best.onnx",
		"assets/CameraTools/imx296_trigger",
	}
	for _, file := range requiredFiles {
		abs := filepath.Join(repoRoot, file)
		if st, err := os.Stat(abs); err == nil && !st.IsDir() {
			results = append(results, checkResult{
				name:     "file:" + file,
				required: true,
				ok:       true,
				detail:   abs,
			})
		} else {
			results = append(results, checkResult{
				name:     "file:" + file,
				required: true,
				ok:       false,
				detail:   "missing at " + abs,
			})
		}
	}

	for _, key := range requiredEnvVars {
		value := strings.TrimSpace(os.Getenv(key))
		ok := value != ""
		detail := "not set"
		if ok {
			detail = value
		}
		results = append(results, checkResult{
			name:     "env:" + key,
			required: true,
			ok:       ok,
			detail:   detail,
		})
	}

	for _, key := range optionalEnvVars {
		value := strings.TrimSpace(os.Getenv(key))
		ok := value != ""
		detail := "not set (optional)"
		if ok {
			detail = value
		}
		results = append(results, checkResult{
			name:     "env:" + key,
			required: false,
			ok:       ok,
			detail:   detail,
		})
	}

	if broker := strings.TrimSpace(os.Getenv("PITRAC_MSG_BROKER_FULL_ADDRESS")); broker != "" {
		brokerCheck := checkResult{
			name:     "service:activemq",
			required: true,
			ok:       false,
			detail:   "not reachable at " + broker,
		}
		if err := checkBrokerReachable(broker); err == nil {
			brokerCheck.ok = true
			brokerCheck.detail = "listening at " + broker
		}
		results = append(results, brokerCheck)
	}

	// Boot config check (Pi 5)
	bootConfig := "/boot/firmware/config.txt"
	if data, err := os.ReadFile(bootConfig); err == nil {
		content := string(data)
		cameraAutoDetect := strings.Contains(content, "camera_auto_detect=1")
		detail := "enabled"
		if !cameraAutoDetect {
			detail = "not set in " + bootConfig
		}
		results = append(results, checkResult{
			name:     "boot:camera_auto_detect",
			required: true,
			ok:       cameraAutoDetect,
			detail:   detail,
		})
	}
	// If the file doesn't exist we're not on a Pi — skip silently.

	if root := strings.TrimSpace(os.Getenv("PITRAC_ROOT")); root != "" {
		rootCheck := checkResult{
			name:     "path:PITRAC_ROOT/src",
			required: true,
			ok:       false,
			detail:   "missing",
		}
		if st, err := os.Stat(filepath.Join(root, "src")); err == nil && st.IsDir() {
			rootCheck.ok = true
			rootCheck.detail = "present"
		}
		results = append(results, rootCheck)
	}

	printHeader("PiTrac Doctor")
	fmt.Printf("repo_root: %s\n\n", repoRoot)

	passed, warned, failed := 0, 0, 0
	for _, r := range results {
		status := markSuccess()
		if !r.ok && r.required {
			status = markFailure()
			failed++
		} else if !r.ok {
			status = markWarning()
			warned++
		} else {
			passed++
		}
		printStatus(status, r.name, r.detail)
	}

	printSummaryBox(passed, warned, failed)

	if failed > 0 {
		return fmt.Errorf("doctor found %d required issue(s)", failed)
	}

	fmt.Println("doctor passed")
	return nil
}
