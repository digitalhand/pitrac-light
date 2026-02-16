package cmd

import (
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(serviceCmd)

	serviceCmd.AddCommand(serviceStartCmd)
	serviceCmd.AddCommand(serviceStopCmd)
	serviceCmd.AddCommand(serviceStatusCmd)

	serviceCmd.AddCommand(serviceBrokerCmd)
	serviceBrokerCmd.AddCommand(serviceBrokerStartCmd)
	serviceBrokerCmd.AddCommand(serviceBrokerStopCmd)
	serviceBrokerCmd.AddCommand(serviceBrokerStatusCmd)
	serviceBrokerCmd.AddCommand(serviceBrokerSetupCmd)

	serviceCmd.AddCommand(serviceLMCmd)
	serviceLMCmd.AddCommand(serviceLMStartCmd)
	serviceLMCmd.AddCommand(serviceLMStopCmd)
	serviceLMCmd.AddCommand(serviceLMStatusCmd)

	serviceLMStartCmd.Flags().Int("camera", 0, "start only camera 1 or 2 (default: both)")
	serviceLMStartCmd.Flags().Int("sim-port", 0, "simulator connect port (overrides config default)")
	serviceLMStartCmd.Flags().Bool("trace", false, "enable trace-level logging")
}

// --- parent commands ---

var serviceCmd = &cobra.Command{
	Use:   "service",
	Short: "Manage PiTrac background services",
}

// --- convenience commands ---

var serviceStartCmd = &cobra.Command{
	Use:   "start",
	Short: "Start broker + both cameras",
	RunE:  runServiceStart,
}

var serviceStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop cameras + broker",
	RunE:  runServiceStop,
}

var serviceStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "Show status of all services",
	RunE:  runServiceStatus,
}

// --- broker commands ---

var serviceBrokerCmd = &cobra.Command{
	Use:   "broker",
	Short: "Manage the ActiveMQ message broker",
}

var serviceBrokerStartCmd = &cobra.Command{
	Use:   "start",
	Short: "Start the ActiveMQ broker",
	RunE:  runBrokerStart,
}

var serviceBrokerStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop the ActiveMQ broker",
	RunE:  runBrokerStop,
}

var serviceBrokerStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "Check broker status",
	RunE:  runBrokerStatus,
}

var serviceBrokerSetupCmd = &cobra.Command{
	Use:   "setup",
	Short: "Create systemd unit + configure remote access",
	RunE:  runBrokerSetup,
}

// --- lm commands ---

var serviceLMCmd = &cobra.Command{
	Use:   "lm",
	Short: "Manage pitrac_lm camera processes",
}

var serviceLMStartCmd = &cobra.Command{
	Use:   "start",
	Short: "Start camera processes (--camera 1|2)",
	RunE:  runLMStart,
}

var serviceLMStopCmd = &cobra.Command{
	Use:   "stop",
	Short: "Stop camera processes",
	RunE:  runLMStop,
}

var serviceLMStatusCmd = &cobra.Command{
	Use:   "status",
	Short: "Check camera process status",
	RunE:  runLMStatus,
}

// ─── PID file management ─────────────────────────────────────────────

func pitracRunDir() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".pitrac", "run")
}

func pitracLogDir() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".pitrac", "log")
}

func cameraPIDPath(camera int) string {
	return filepath.Join(pitracRunDir(), fmt.Sprintf("camera%d.pid", camera))
}

func cameraLogPath(camera int) string {
	return filepath.Join(pitracLogDir(), fmt.Sprintf("camera%d.log", camera))
}

func writePID(path string, pid int) error {
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(path, []byte(strconv.Itoa(pid)), 0o644)
}

func readPID(path string) (int, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return 0, err
	}
	return strconv.Atoi(strings.TrimSpace(string(data)))
}

func processAlive(pid int) bool {
	proc, err := os.FindProcess(pid)
	if err != nil {
		return false
	}
	return proc.Signal(syscall.Signal(0)) == nil
}

// checkStaleAndClean reads the PID file. If the process is dead, it removes
// the stale file and returns 0. If alive, it returns the PID.
func checkStaleAndClean(pidPath string) int {
	pid, err := readPID(pidPath)
	if err != nil {
		return 0
	}
	if processAlive(pid) {
		return pid
	}
	_ = os.Remove(pidPath)
	return 0
}

// ─── Shared helpers ──────────────────────────────────────────────────

func requireSystemctl() error {
	if _, err := exec.LookPath("systemctl"); err != nil {
		return fmt.Errorf("systemctl not found in PATH; systemd is required for broker management")
	}
	return nil
}

func systemctlIsActive(unit string) bool {
	return exec.Command("systemctl", "is-active", "--quiet", unit).Run() == nil
}

func portReachable(addr string, timeout time.Duration) bool {
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return false
	}
	conn.Close()
	return true
}

func waitForPort(addr string, timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if portReachable(addr, time.Second) {
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}
	return fmt.Errorf("timed out waiting for %s after %s", addr, timeout)
}

func waitForProcessExit(pid int, timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if !processAlive(pid) {
			return true
		}
		time.Sleep(250 * time.Millisecond)
	}
	return false
}

// ─── Broker implementations ─────────────────────────────────────────

func runBrokerStart(cmd *cobra.Command, args []string) error {
	if err := requireSystemctl(); err != nil {
		return err
	}

	printHeader("Broker Start")

	if systemctlIsActive("activemq") {
		printStatus(markWarning(), "activemq", "already running")
		return nil
	}

	printStatus(markInfo(), "activemq", "starting via systemctl...")
	c := exec.Command("sudo", "systemctl", "start", "activemq")
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := c.Run(); err != nil {
		printStatus(markFailure(), "activemq", "failed to start")
		return fmt.Errorf("systemctl start activemq failed: %w", err)
	}

	printStatus(markInfo(), "activemq", "waiting for port 61616...")
	if err := waitForPort("localhost:61616", 10*time.Second); err != nil {
		printStatus(markFailure(), "activemq", "port 61616 not reachable")
		return fmt.Errorf("broker started but port not reachable: %w", err)
	}

	printStatus(markSuccess(), "activemq", "running")
	return nil
}

func runBrokerStop(cmd *cobra.Command, args []string) error {
	if err := requireSystemctl(); err != nil {
		return err
	}

	printHeader("Broker Stop")

	if !systemctlIsActive("activemq") {
		printStatus(markInfo(), "activemq", "not running")
		return nil
	}

	printStatus(markInfo(), "activemq", "stopping via systemctl...")
	c := exec.Command("sudo", "systemctl", "stop", "activemq")
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr
	if err := c.Run(); err != nil {
		printStatus(markFailure(), "activemq", "failed to stop")
		return fmt.Errorf("systemctl stop activemq failed: %w", err)
	}

	printStatus(markSuccess(), "activemq", "stopped")
	return nil
}

func runBrokerStatus(cmd *cobra.Command, args []string) error {
	printHeader("Broker Status")
	printBrokerStatus()
	return nil
}

func printBrokerStatus() {
	// systemd state
	if systemctlIsActive("activemq") {
		printStatus(markSuccess(), "systemd", "activemq is active")
	} else {
		printStatus(markFailure(), "systemd", "activemq is not active")
	}

	// TCP port 61616 (OpenWire)
	if portReachable("localhost:61616", 2*time.Second) {
		printStatus(markSuccess(), "tcp:61616", "OpenWire reachable")
	} else {
		printStatus(markFailure(), "tcp:61616", "OpenWire not reachable")
	}

	// TCP port 8161 (web console)
	if portReachable("localhost:8161", 2*time.Second) {
		printStatus(markSuccess(), "tcp:8161", "web console reachable")
	} else {
		printStatus(markWarning(), "tcp:8161", "web console not reachable")
	}
}

func runBrokerSetup(cmd *cobra.Command, args []string) error {
	if !fileExists("/opt/apache-activemq/bin/activemq") {
		return fmt.Errorf("ActiveMQ not found at /opt/apache-activemq\n  install first: pitrac-cli install mq-broker")
	}

	printHeader("Broker Setup")

	step := shellStep("configure remote access and systemd service", `
INSTALL_DIR="${INSTALL_DIR:-/opt/apache-activemq}"
JETTY_CONFIG="${INSTALL_DIR}/conf/jetty.xml"
SERVICE_FILE="/etc/systemd/system/activemq.service"

if [ -f "${JETTY_CONFIG}" ]; then
  sudo cp -n "${JETTY_CONFIG}" "${JETTY_CONFIG}.ORIGINAL" || true
  PI_IP="$(hostname -I | awk '{print $1}')"
  if [ -z "${PI_IP}" ]; then
    PI_IP="0.0.0.0"
  fi
  sudo sed -i "s/127\.0\.0\.1/${PI_IP}/g" "${JETTY_CONFIG}" || true
fi

if ! command -v systemctl >/dev/null 2>&1; then
  echo "systemctl not found; skipping service setup."
  exit 0
fi

sudo tee "${SERVICE_FILE}" >/dev/null <<EOF
[Unit]
Description=ActiveMQ Message Broker
After=network.target

[Service]
User=root
Type=forking
Restart=on-failure
RestartSec=10
ExecStart=${INSTALL_DIR}/bin/activemq start
ExecStop=${INSTALL_DIR}/bin/activemq stop
KillSignal=SIGTERM
TimeoutStopSec=30
WorkingDirectory=${INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable activemq
`)

	printStep(1, 1, step.name)
	printCommand(step.cmd)

	c := exec.Command(step.cmd[0], step.cmd[1:]...)
	c.Stdin = os.Stdin
	c.Stdout = os.Stdout
	c.Stderr = os.Stderr

	start := time.Now()
	if err := c.Run(); err != nil {
		printStatus(markFailure(), "broker_setup", "failed")
		return fmt.Errorf("broker setup failed: %w", err)
	}
	printDuration(time.Since(start))

	printStatus(markSuccess(), "broker_setup", "complete")
	return nil
}

// ─── Camera implementations ─────────────────────────────────────────

func buildCameraServiceArgs(camera, simPort int, trace bool) ([]string, error) {
	values := envMapFromProcess()
	commonArgs, err := buildCommonArgs(values)
	if err != nil {
		return nil, fmt.Errorf("failed to build common args: %w", err)
	}

	logLevel := "info"
	if trace {
		logLevel = "trace"
	}

	var modeArgs []string
	switch camera {
	case 1:
		modeArgs = []string{
			"--system_mode", "camera1",
			"--search_center_x", "850",
			"--search_center_y", "500",
			"--logging_level=" + logLevel,
			"--artifact_save_level=final_results_only",
			"--camera_gain", "1.1",
		}
	case 2:
		modeArgs = []string{
			"--system_mode", "camera2",
			"--camera_gain", "3.0",
			"--logging_level=" + logLevel,
			"--artifact_save_level=final_results_only",
		}
	default:
		return nil, fmt.Errorf("invalid camera: %d (must be 1 or 2)", camera)
	}

	if simPort > 0 {
		modeArgs = append(modeArgs, "--gspro_port", strconv.Itoa(simPort))
	}

	return append(commonArgs, modeArgs...), nil
}

func resolvePitracBinary() (string, error) {
	// 1. Try $PITRAC_ROOT/src/build/pitrac_lm (dev builds)
	pitracRoot := strings.TrimSpace(os.Getenv("PITRAC_ROOT"))
	if pitracRoot == "" {
		detected, err := detectRepoRoot()
		if err == nil {
			pitracRoot = detected
		}
	}
	if pitracRoot != "" {
		binary := filepath.Join(pitracRoot, "src", "build", "pitrac_lm")
		if fileExists(binary) {
			return binary, nil
		}
	}

	// 2. Try installed path /usr/lib/pitrac/pitrac_lm
	installed := "/usr/lib/pitrac/pitrac_lm"
	if fileExists(installed) {
		return installed, nil
	}

	hint := "run: pitrac-cli build --install"
	if pitracRoot != "" {
		return "", fmt.Errorf("pitrac_lm not found at %s/src/build/ or /usr/lib/pitrac/\n  %s", pitracRoot, hint)
	}
	return "", fmt.Errorf("pitrac_lm not found at /usr/lib/pitrac/\n  %s", hint)
}

func startCamera(camera, simPort int, trace bool) error {
	pidPath := cameraPIDPath(camera)
	existing := checkStaleAndClean(pidPath)
	if existing != 0 {
		printStatus(markWarning(), fmt.Sprintf("camera%d", camera),
			fmt.Sprintf("already running (PID %d)", existing))
		return nil
	}

	binary, err := resolvePitracBinary()
	if err != nil {
		return err
	}

	allArgs, err := buildCameraServiceArgs(camera, simPort, trace)
	if err != nil {
		return err
	}

	logPath := cameraLogPath(camera)
	if err := os.MkdirAll(filepath.Dir(logPath), 0o755); err != nil {
		return fmt.Errorf("failed to create log dir: %w", err)
	}

	// Truncate log so each run starts clean (old logs are easy to find via timestamps)
	logFile, err := os.OpenFile(logPath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0o644)
	if err != nil {
		return fmt.Errorf("failed to open log file %s: %w", logPath, err)
	}

	proc := exec.Command(binary, allArgs...)
	proc.Stdout = logFile
	proc.Stderr = logFile
	proc.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}

	// Print the full command for debugging
	printStatus(markInfo(), fmt.Sprintf("camera%d", camera),
		fmt.Sprintf("exec: %s %s", binary, strings.Join(allArgs, " ")))

	if err := proc.Start(); err != nil {
		logFile.Close()
		return fmt.Errorf("failed to start camera %d: %w", camera, err)
	}

	pid := proc.Process.Pid
	if err := os.MkdirAll(filepath.Dir(pidPath), 0o755); err != nil {
		logFile.Close()
		return fmt.Errorf("failed to create run dir: %w", err)
	}
	if err := writePID(pidPath, pid); err != nil {
		logFile.Close()
		return fmt.Errorf("failed to write PID file: %w", err)
	}

	// Release the process so it runs independently.
	_ = proc.Process.Release()
	logFile.Close()

	printStatus(markSuccess(), fmt.Sprintf("camera%d", camera),
		fmt.Sprintf("started (PID %d, log: %s)", pid, logPath))
	return nil
}

func stopCamera(camera int) error {
	pidPath := cameraPIDPath(camera)
	pid := checkStaleAndClean(pidPath)
	if pid == 0 {
		printStatus(markInfo(), fmt.Sprintf("camera%d", camera), "not running")
		return nil
	}

	printStatus(markInfo(), fmt.Sprintf("camera%d", camera),
		fmt.Sprintf("sending SIGTERM to PID %d...", pid))

	proc, err := os.FindProcess(pid)
	if err != nil {
		_ = os.Remove(pidPath)
		return nil
	}

	_ = proc.Signal(syscall.SIGTERM)

	if waitForProcessExit(pid, 5*time.Second) {
		_ = os.Remove(pidPath)
		printStatus(markSuccess(), fmt.Sprintf("camera%d", camera), "stopped")
		return nil
	}

	printStatus(markWarning(), fmt.Sprintf("camera%d", camera),
		"still running after SIGTERM, sending SIGKILL...")
	_ = proc.Signal(syscall.SIGKILL)

	if waitForProcessExit(pid, 3*time.Second) {
		_ = os.Remove(pidPath)
		printStatus(markSuccess(), fmt.Sprintf("camera%d", camera), "killed")
		return nil
	}

	printStatus(markFailure(), fmt.Sprintf("camera%d", camera),
		fmt.Sprintf("PID %d could not be stopped", pid))
	return fmt.Errorf("camera %d (PID %d) could not be stopped", camera, pid)
}

// killOrphanedLMProcesses finds and kills any pitrac_lm processes not tracked
// by PID files.  This handles cases where the service didn't shut down cleanly
// and the camera devices are still held.
func killOrphanedLMProcesses() {
	out, err := exec.Command("pgrep", "-x", "pitrac_lm").Output()
	if err != nil {
		return // no processes found
	}

	pids := strings.Fields(strings.TrimSpace(string(out)))
	if len(pids) == 0 {
		return
	}

	printStatus(markWarning(), "cleanup",
		fmt.Sprintf("found %d orphaned pitrac_lm process(es)", len(pids)))

	_ = exec.Command("killall", "-SIGTERM", "pitrac_lm").Run()
	time.Sleep(2 * time.Second)
	_ = exec.Command("killall", "-SIGKILL", "pitrac_lm").Run()
	time.Sleep(1 * time.Second)

	printStatus(markSuccess(), "cleanup", "orphaned processes killed")
}

func printCameraStatus(camera int) {
	pidPath := cameraPIDPath(camera)
	pid := checkStaleAndClean(pidPath)
	if pid == 0 {
		printStatus(markFailure(), fmt.Sprintf("camera%d", camera), "not running")
	} else {
		printStatus(markSuccess(), fmt.Sprintf("camera%d", camera),
			fmt.Sprintf("running (PID %d)", pid))
	}
}

func runLMStart(cmd *cobra.Command, args []string) error {
	camera, _ := cmd.Flags().GetInt("camera")
	simPort, _ := cmd.Flags().GetInt("sim-port")
	trace, _ := cmd.Flags().GetBool("trace")

	printHeader("Launch Monitor Start")

	// Clean up orphaned processes before starting any cameras
	killOrphanedLMProcesses()

	switch camera {
	case 1:
		return startCamera(1, simPort, trace)
	case 2:
		return startCamera(2, simPort, trace)
	case 0:
		// Start both: camera 1 first, then camera 2
		if err := startCamera(1, simPort, trace); err != nil {
			return err
		}
		printStatus(markInfo(), "startup", "waiting 2s before starting camera 2...")
		time.Sleep(2 * time.Second)
		return startCamera(2, simPort, trace)
	default:
		return fmt.Errorf("--camera must be 1 or 2, got %d", camera)
	}
}

func runLMStop(cmd *cobra.Command, args []string) error {
	printHeader("Launch Monitor Stop")

	// Stop camera 2 first (reverse dependency order)
	if err := stopCamera(2); err != nil {
		return err
	}
	if err := stopCamera(1); err != nil {
		return err
	}

	// Clean up any orphaned processes that weren't tracked by PID files
	killOrphanedLMProcesses()
	return nil
}

func runLMStatus(cmd *cobra.Command, args []string) error {
	printHeader("Launch Monitor Status")
	printCameraStatus(1)
	printCameraStatus(2)
	return nil
}

// ─── Convenience implementations ────────────────────────────────────

func runServiceStart(cmd *cobra.Command, args []string) error {
	printHeader("Service Start")

	// Clean up orphaned processes before starting any cameras
	killOrphanedLMProcesses()

	// 1. Start broker
	fmt.Println()
	printStatus(markInfo(), "broker", "starting...")
	if err := requireSystemctl(); err != nil {
		return err
	}
	if !systemctlIsActive("activemq") {
		c := exec.Command("sudo", "systemctl", "start", "activemq")
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr
		if err := c.Run(); err != nil {
			printStatus(markFailure(), "broker", "failed to start")
			return fmt.Errorf("systemctl start activemq failed: %w", err)
		}
	}

	printStatus(markInfo(), "broker", "waiting for port 61616...")
	if err := waitForPort("localhost:61616", 10*time.Second); err != nil {
		printStatus(markFailure(), "broker", "port 61616 not reachable")
		return fmt.Errorf("broker started but port not reachable: %w", err)
	}
	printStatus(markSuccess(), "broker", "running")

	// 2. Start camera 1
	fmt.Println()
	if err := startCamera(1, 0, false); err != nil {
		return err
	}

	// 3. Wait then start camera 2
	printStatus(markInfo(), "startup", "waiting 2s before starting camera 2...")
	time.Sleep(2 * time.Second)
	if err := startCamera(2, 0, false); err != nil {
		return err
	}

	fmt.Println()
	printStatus(markSuccess(), "service", "all services started")
	return nil
}

func runServiceStop(cmd *cobra.Command, args []string) error {
	printHeader("Service Stop")

	// 1. Stop camera 2 first, then camera 1
	fmt.Println()
	if err := stopCamera(2); err != nil {
		return err
	}
	if err := stopCamera(1); err != nil {
		return err
	}

	// 2. Stop broker
	fmt.Println()
	if err := requireSystemctl(); err != nil {
		return err
	}
	if systemctlIsActive("activemq") {
		printStatus(markInfo(), "broker", "stopping...")
		c := exec.Command("sudo", "systemctl", "stop", "activemq")
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr
		if err := c.Run(); err != nil {
			printStatus(markFailure(), "broker", "failed to stop")
			return fmt.Errorf("systemctl stop activemq failed: %w", err)
		}
		printStatus(markSuccess(), "broker", "stopped")
	} else {
		printStatus(markInfo(), "broker", "not running")
	}

	fmt.Println()
	printStatus(markSuccess(), "service", "all services stopped")
	return nil
}

func runServiceStatus(cmd *cobra.Command, args []string) error {
	printHeader("Service Status")

	fmt.Println()
	printBrokerStatus()

	fmt.Println()
	printCameraStatus(1)
	printCameraStatus(2)
	return nil
}
