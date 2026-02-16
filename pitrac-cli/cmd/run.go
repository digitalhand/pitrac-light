package cmd

import (
	"fmt"
	"net"
	"net/url"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(runCmd)

	runCmd.PersistentFlags().Int("camera", 1, "which camera to use (1 or 2)")
	runCmd.PersistentFlags().Bool("dry-run", false, "print the command instead of executing")

	runCmd.AddCommand(runPulseTestCmd)
	runCmd.AddCommand(runStillCmd)
	runCmd.AddCommand(runBallLocationCmd)
	runCmd.AddCommand(runCamCmd)
	runCmd.AddCommand(runCalibrateCmd)
	runCmd.AddCommand(runAutoCalibrateCmd)
	runCmd.AddCommand(runShutdownCmd)
	runCmd.AddCommand(runCalibrateGUICmd)

	runCalibrateGUICmd.Flags().String("mode", "full", "calibration mode: intrinsic, extrinsic, or full")
}

var runCmd = &cobra.Command{
	Use:   "run",
	Short: "Run pitrac_lm in various modes",
}

var runPulseTestCmd = &cobra.Command{
	Use:   "pulse-test",
	Short: "Strobe/shutter pulse test",
	RunE:  makeRunFunc("pulse-test"),
}

var runStillCmd = &cobra.Command{
	Use:   "still",
	Short: "Capture a single camera still",
	RunE:  makeRunFunc("still"),
}

var runBallLocationCmd = &cobra.Command{
	Use:   "ball-location",
	Short: "Continuous ball detection loop",
	RunE:  makeRunFunc("ball-location"),
}

var runCamCmd = &cobra.Command{
	Use:   "cam",
	Short: "Normal camera operation",
	RunE:  makeRunFunc("cam"),
}

var runCalibrateCmd = &cobra.Command{
	Use:   "calibrate",
	Short: "Manual focal-length calibration",
	RunE:  makeRunFunc("calibrate"),
}

var runAutoCalibrateCmd = &cobra.Command{
	Use:   "auto-calibrate",
	Short: "Automatic calibration",
	RunE:  makeRunFunc("auto-calibrate"),
}

var runShutdownCmd = &cobra.Command{
	Use:   "shutdown",
	Short: "Shutdown all PiTrac instances",
	RunE:  makeRunFunc("shutdown"),
}

var runCalibrateGUICmd = &cobra.Command{
	Use:   "calibrate-gui",
	Short: "Launch camera calibration GUI (CharucoBoard + extrinsic)",
	RunE:  runCalibrateGUI,
}

func runCalibrateGUI(cmd *cobra.Command, args []string) error {
	cameraNum, _ := cmd.Flags().GetInt("camera")
	dryRun, _ := cmd.Flags().GetBool("dry-run")
	mode, _ := cmd.Flags().GetString("mode")

	if cameraNum != 1 && cameraNum != 2 {
		return fmt.Errorf("--camera must be 1 or 2, got %d", cameraNum)
	}

	pitracRoot := strings.TrimSpace(os.Getenv("PITRAC_ROOT"))
	if pitracRoot == "" {
		detected, err := detectRepoRoot()
		if err != nil {
			return fmt.Errorf("PITRAC_ROOT not set and could not detect repo root: %w", err)
		}
		pitracRoot = detected
	}

	pitracCal := filepath.Join(pitracRoot, "pitrac_cal")
	if _, err := os.Stat(filepath.Join(pitracCal, "__main__.py")); err != nil {
		return fmt.Errorf("pitrac_cal not found at %s\nEnsure PITRAC_ROOT is correct", pitracCal)
	}

	configPath, err := resolveConfigFile(pitracRoot)
	if err != nil {
		return err
	}

	pyArgs := []string{
		"-m", "pitrac_cal",
		"--camera", fmt.Sprintf("%d", cameraNum),
		"--mode", mode,
		"--config", configPath,
	}

	if dryRun {
		fmt.Println("python3 " + strings.Join(pyArgs, " "))
		return nil
	}

	c := exec.Command("python3", pyArgs...)
	c.Dir = pitracRoot
	c.Stdin = os.Stdin
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	c.Env = append(os.Environ(), "DISPLAY=:0.0")

	if err := c.Run(); err != nil {
		return fmt.Errorf("pitrac-cal failed: %w", err)
	}
	return nil
}

func makeRunFunc(mode string) func(*cobra.Command, []string) error {
	return func(cmd *cobra.Command, args []string) error {
		camera, _ := cmd.Flags().GetInt("camera")
		dryRun, _ := cmd.Flags().GetBool("dry-run")

		if camera != 1 && camera != 2 {
			return fmt.Errorf("--camera must be 1 or 2, got %d", camera)
		}

		modeArgs := buildModeArgs(mode, camera)

		return execPitracLM(modeArgs, dryRun)
	}
}

func cameraStr(camera int) string {
	if camera == 2 {
		return "camera2"
	}
	return "camera1"
}

func buildModeArgs(mode string, camera int) []string {
	cam := cameraStr(camera)

	switch mode {
	case "pulse-test":
		return []string{"--pulse_test", "--system_mode", cam}
	case "still":
		return []string{"--system_mode", cam, "--cam_still_mode"}
	case "ball-location":
		return []string{"--system_mode", cam + "_ball_location"}
	case "cam":
		return []string{"--system_mode", cam}
	case "calibrate":
		return []string{"--system_mode", cam + "Calibrate"}
	case "auto-calibrate":
		return []string{"--system_mode", cam + "AutoCalibrate"}
	case "shutdown":
		return []string{"--shutdown"}
	default:
		return nil
	}
}

func execPitracLM(modeArgs []string, dryRun bool) error {
	binary, err := resolvePitracBinary()
	if err != nil {
		return err
	}

	values := envMapFromProcess()
	commonArgs, err := buildCommonArgs(values)
	if err != nil {
		return fmt.Errorf("failed to build common args: %w", err)
	}

	allArgs := append(commonArgs, modeArgs...)

	if dryRun {
		fmt.Println(binary + " " + strings.Join(allArgs, " "))
		return nil
	}

	// Pre-flight: check message broker is reachable
	if broker := strings.TrimSpace(values["PITRAC_MSG_BROKER_FULL_ADDRESS"]); broker != "" {
		if err := checkBrokerReachable(broker); err != nil {
			return err
		}
	}

	c := exec.Command(binary, allArgs...)
	c.Stdin = os.Stdin
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := c.Run(); err != nil {
		return fmt.Errorf("pitrac_lm failed: %w", err)
	}
	return nil
}

// checkBrokerReachable does a quick TCP dial to verify the message broker
// is listening before launching pitrac_lm. The address is expected in
// "tcp://host:port" format.
func checkBrokerReachable(brokerURL string) error {
	u, err := url.Parse(brokerURL)
	if err != nil {
		return fmt.Errorf("invalid broker address %q: %w", brokerURL, err)
	}

	host := u.Host
	if host == "" {
		host = u.Opaque
	}

	conn, err := net.DialTimeout("tcp", host, 2*time.Second)
	if err != nil {
		return fmt.Errorf("message broker not reachable at %s\n  is ActiveMQ running? try: sudo /opt/apache-activemq/bin/activemq start", host)
	}
	conn.Close()
	return nil
}
