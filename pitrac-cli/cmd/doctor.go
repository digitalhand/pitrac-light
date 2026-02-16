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

	// Boot config checks
	bootConfig := "/boot/firmware/config.txt"
	if _, err := os.Stat(bootConfig); err != nil {
		bootConfig = "/boot/config.txt"
	}
	if data, err := os.ReadFile(bootConfig); err == nil {
		content := string(data)

		// Detect Pi model for model-specific checks
		piModel := "unknown"
		if cpuinfo, err := os.ReadFile("/proc/cpuinfo"); err == nil {
			cpuStr := string(cpuinfo)
			if strings.Contains(cpuStr, "Raspberry Pi") && strings.Contains(cpuStr, "5") {
				piModel = "pi5"
			} else if strings.Contains(cpuStr, "Raspberry Pi") && strings.Contains(cpuStr, "4") {
				piModel = "pi4"
			}
		}

		// Universal boot parameter checks
		bootChecks := []struct {
			name  string
			key   string
			value string
		}{
			{"boot:camera_auto_detect", "camera_auto_detect", "1"},
			{"boot:dtparam_i2c_arm", "dtparam=i2c_arm", "on"},
			{"boot:dtparam_spi", "dtparam=spi", "on"},
			{"boot:force_turbo", "force_turbo", "1"},
		}

		// Add model-specific checks
		if piModel == "pi5" {
			bootChecks = append(bootChecks, struct {
				name  string
				key   string
				value string
			}{"boot:arm_boost", "arm_boost", "1"})
		} else if piModel == "pi4" {
			bootChecks = append(bootChecks, struct {
				name  string
				key   string
				value string
			}{"boot:gpu_mem", "gpu_mem", "256"})
		}

		for _, bc := range bootChecks {
			found := false
			for _, line := range strings.Split(content, "\n") {
				line = strings.TrimSpace(line)
				if line == bc.key+"="+bc.value {
					found = true
					break
				}
			}
			detail := "enabled"
			if !found {
				detail = "not set in " + bootConfig
			}
			results = append(results, checkResult{
				name:     bc.name,
				required: true,
				ok:       found,
				detail:   detail,
			})
		}

		// IMX296 dtoverlay checks for dual-camera setup
		overlayChecks := []struct {
			name    string
			line    string
			detail  string
		}{
			{"boot:dtoverlay_imx296_cam0", "dtoverlay=imx296,cam0", "Camera 1 overlay (internal trigger)"},
			{"boot:dtoverlay_imx296_cam1", "dtoverlay=imx296,cam1,sync-sink", "Camera 2 overlay (external trigger)"},
		}
		for _, oc := range overlayChecks {
			found := false
			for _, line := range strings.Split(content, "\n") {
				if strings.TrimSpace(line) == oc.line {
					found = true
					break
				}
			}
			detail := oc.detail
			if !found {
				detail = "missing: " + oc.line
			}
			results = append(results, checkResult{
				name:     oc.name,
				required: true,
				ok:       found,
				detail:   detail,
			})
		}

		// Warn about legacy overlay without cam1 specifier
		for _, line := range strings.Split(content, "\n") {
			if strings.TrimSpace(line) == "dtoverlay=imx296,sync-sink" {
				results = append(results, checkResult{
					name:     "boot:dtoverlay_legacy_sync_sink",
					required: true,
					ok:       false,
					detail:   "found legacy 'dtoverlay=imx296,sync-sink' (missing cam1) — run 'pitrac-cli install' to fix",
				})
				break
			}
		}
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

	results = append(results, cameraDoctorChecks(repoRoot)...)

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
