package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"

	"github.com/spf13/cobra"
)

type commandStep struct {
	name string
	cmd  []string
}

type installTarget struct {
	id          string
	description string
	steps       []commandStep
}

var installProfiles = map[string][]string{
	"base":      {"source-deps", "boost", "java", "msgpack-cxx"},
	"messaging": {"mq-broker", "activemq-cpp"},
	"camera":    {"lgpio", "libcamera", "camera-timeout"},
	"compute":   {"opencv", "onnx"},
	"full": {
		"source-deps", "boost", "java", "msgpack-cxx",
		"mq-broker", "activemq-cpp", "lgpio", "libcamera", "camera-timeout",
		"opencv", "onnx",
	},
}

func init() {
	rootCmd.AddCommand(installCmd)
	installCmd.AddCommand(installListCmd)

	// Register profile commands
	for name := range installProfiles {
		name := name // capture
		c := &cobra.Command{
			Use:   name,
			Short: fmt.Sprintf("Install %s profile", name),
			RunE: func(cmd *cobra.Command, args []string) error {
				return runInstallProfile(cmd, name)
			},
		}
		c.Flags().Bool("dry-run", false, "print commands without executing")
		c.Flags().Bool("yes", false, "run non-interactively without confirmation")
		installCmd.AddCommand(c)
	}
}

var installCmd = &cobra.Command{
	Use:   "install",
	Short: "Install PiTrac dependencies and runtime components",
	Args:  cobra.MinimumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		targetID := args[0]
		target, found := findInstallTarget(targetID)
		if !found {
			return fmt.Errorf("unknown install target: %s (try: pitrac-cli install list)", targetID)
		}
		dryRun, _ := cmd.Flags().GetBool("dry-run")
		yes, _ := cmd.Flags().GetBool("yes")
		return executeInstallTarget(target, dryRun, yes)
	},
}

func init() {
	installCmd.Flags().Bool("dry-run", false, "print commands without executing")
	installCmd.Flags().Bool("yes", false, "run non-interactively without confirmation")
}

var installListCmd = &cobra.Command{
	Use:   "list",
	Short: "Show available install targets and profiles",
	Run: func(cmd *cobra.Command, args []string) {
		targets := cachedInstallTargets()

		printHeader("Install Targets")
		fmt.Println("Use: pitrac-cli install <target> [--dry-run] [--yes]")
		fmt.Println()

		for _, t := range targets {
			printStatus(markInfo(), t.id, t.description)
		}

		fmt.Printf("\n%s total: %d targets\n", markInfo(), len(targets))

		fmt.Println()
		printHeader("Install Profiles")
		profileOrder := []string{"base", "messaging", "camera", "compute", "full"}
		for _, name := range profileOrder {
			ids := installProfiles[name]
			printStatus(markInfo(), name, strings.Join(ids, ", "))
		}
		fmt.Println()
	},
}

func runInstallProfile(cmd *cobra.Command, profileName string) error {
	ids, ok := installProfiles[profileName]
	if !ok {
		return fmt.Errorf("unknown profile: %s", profileName)
	}

	dryRun, _ := cmd.Flags().GetBool("dry-run")
	yes, _ := cmd.Flags().GetBool("yes")

	printHeader(fmt.Sprintf("Install Profile: %s", profileName))
	fmt.Printf("targets: %s\n", strings.Join(ids, ", "))
	fmt.Printf("dry_run: %v\n\n", dryRun)

	if !dryRun && !yes {
		if !confirmProceed(fmt.Sprintf("Proceed with %s install profile? [y/N]: ", profileName)) {
			fmt.Println("aborted")
			return nil
		}
	}

	for _, id := range ids {
		target, found := findInstallTarget(id)
		if !found {
			printStatus(markFailure(), "install_target", "missing from registry: "+id)
			return fmt.Errorf("missing target: %s", id)
		}

		if err := executeInstallTarget(target, dryRun, true); err != nil {
			return err
		}
	}

	fmt.Println()
	printStatus(markSuccess(), "install_profile", profileName+" complete")
	return nil
}

func executeInstallTarget(target installTarget, dryRun, yes bool) error {
	fmt.Printf("Target: %s\n", target.id)

	fmt.Printf("steps: %d\n", len(target.steps))
	fmt.Printf("dry_run: %v\n\n", dryRun)

	if !dryRun && !yes {
		if !confirmProceed(fmt.Sprintf("Proceed with install target %s? [y/N]: ", target.id)) {
			fmt.Println("aborted")
			return nil
		}
	}

	for i, step := range target.steps {
		printStep(i+1, len(target.steps), step.name)
		printCommand(step.cmd)

		if dryRun {
			fmt.Println("      [DRY] skipped")
			continue
		}

		if len(step.cmd) == 0 {
			fmt.Fprintf(os.Stderr, "      %s empty command\n", markFailure())
			return fmt.Errorf("empty command in step %d", i+1)
		}

		if _, err := exec.LookPath(step.cmd[0]); err != nil {
			fmt.Fprintf(os.Stderr, "      %s command not found: %s\n", markFailure(), step.cmd[0])
			return fmt.Errorf("command not found: %s", step.cmd[0])
		}

		start := time.Now()
		c := exec.Command(step.cmd[0], step.cmd[1:]...)
		c.Stdin = os.Stdin
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr

		if err := c.Run(); err != nil {
			fmt.Fprintf(os.Stderr, "      %s step failed: %v\n", markFailure(), err)
			return fmt.Errorf("step %q failed: %w", step.name, err)
		}

		printDuration(time.Since(start))
	}

	fmt.Println()
	printStatus(markSuccess(), "install_target", "complete: "+target.id)
	fmt.Println()
	return nil
}

func formatCommand(parts []string) string {
	if len(parts) == 0 {
		return ""
	}

	quoted := make([]string, 0, len(parts))
	for _, p := range parts {
		if strings.ContainsAny(p, " \t\"'") {
			quoted = append(quoted, fmt.Sprintf("%q", p))
		} else {
			quoted = append(quoted, p)
		}
	}
	return strings.Join(quoted, " ")
}

func shellStep(name, script string) commandStep {
	return commandStep{
		name: name,
		cmd: []string{
			"bash",
			"-lc",
			"set -euo pipefail\n" + strings.TrimSpace(script),
		},
	}
}

func findInstallTarget(id string) (installTarget, bool) {
	for _, t := range cachedInstallTargets() {
		if t.id == id {
			return t, true
		}
	}
	return installTarget{}, false
}

var (
	cachedTargets     []installTarget
	cachedTargetsOnce sync.Once
)

func cachedInstallTargets() []installTarget {
	cachedTargetsOnce.Do(func() {
		cachedTargets = buildInstallTargets()
	})
	return cachedTargets
}

func buildInstallTargets() []installTarget {
	return []installTarget{
		{
			id:          "source-deps",
			description: "Install base apt build dependencies",
			steps: []commandStep{
				{
					name: "apt update",
					cmd:  []string{"sudo", "apt", "update"},
				},
				{
					name: "apt install base packages",
					cmd: []string{
						"sudo", "apt", "install", "-y",
						"build-essential", "cmake", "git", "pkg-config",
						"libjpeg-dev", "libpng-dev", "libtiff-dev",
						"libavcodec-dev", "libavformat-dev", "libswscale-dev",
						"libv4l-dev", "v4l-utils",
						"libxvidcore-dev", "libx264-dev",
						"libgtk-3-dev",
						"python3-pip", "python3-venv", "python3-dev", "python3-numpy",
						"libexif-dev",
						"libprotobuf-dev", "protobuf-compiler",
						"libssl-dev",
						"libfmt-dev",
						"libyaml-cpp-dev",
					},
				},
			},
		},
		{
			id:          "boost",
			description: "Install Boost development headers/libs",
			steps: []commandStep{
				shellStep("install boost", `
FORCE="${FORCE:-0}"
if dpkg -s libboost-all-dev >/dev/null 2>&1 && [ "${FORCE}" != "1" ]; then
  echo "libboost-all-dev already installed. Set FORCE=1 to reinstall."
  exit 0
fi
sudo apt install -y libboost-all-dev
`),
			},
		},
		{
			id:          "java",
			description: "Install Java runtime/toolchain (21+ from apt)",
			steps: []commandStep{
				shellStep("install openjdk 21", `
FORCE="${FORCE:-0}"
if dpkg -s openjdk-21-jdk >/dev/null 2>&1 && [ "${FORCE}" != "1" ]; then
  echo "openjdk-21-jdk already installed. Set FORCE=1 to reinstall."
  exit 0
fi
sudo apt update
if apt-cache show "openjdk-21-jdk" >/dev/null 2>&1; then
  echo "Installing openjdk-21-jdk"
  sudo apt install -y "openjdk-21-jdk"
else
  echo "openjdk-21-jdk package not found" >&2
  exit 1
fi
`),
			},
		},
		{
			id:          "msgpack-cxx",
			description: "Install MessagePack C++ headers from distro",
			steps: []commandStep{
				shellStep("install msgpack package", `
FORCE="${FORCE:-0}"
if dpkg -s libmsgpack-cxx-dev >/dev/null 2>&1 && [ "${FORCE}" != "1" ]; then
  echo "libmsgpack-cxx-dev already installed. Set FORCE=1 to reinstall."
  exit 0
fi
# libmsgpack-cxx-dev provides the C++ header-only library (msgpack.hpp)
# and the msgpack-cxx.pc pkg-config file that meson requires.
# NOTE: libmsgpack-dev is a transitional package that only installs the
# C bindings (libmsgpack-c-dev), which does NOT include msgpack.hpp.
sudo apt install -y libmsgpack-cxx-dev
`),
			},
		},
		{
			id:          "mq-broker",
			description: "Install and configure ActiveMQ broker",
			steps: []commandStep{
				{
					name: "install broker prerequisites",
					cmd:  []string{"sudo", "apt", "install", "-y", "wget", "ca-certificates", "tar", "file"},
				},
				shellStep("download and install ActiveMQ broker", `
ACTIVEMQ_VERSION="${ACTIVEMQ_VERSION:-6.1.7}"
INSTALL_DIR="${INSTALL_DIR:-/opt/apache-activemq}"
FORCE="${FORCE:-0}"
FILENAME="apache-activemq-${ACTIVEMQ_VERSION}-bin.tar.gz"
URL="${ACTIVEMQ_URL:-https://archive.apache.org/dist/activemq/${ACTIVEMQ_VERSION}/${FILENAME}}"

if [ -x "${INSTALL_DIR}/bin/activemq" ] && [ "${FORCE}" != "1" ]; then
  echo "ActiveMQ already present at ${INSTALL_DIR}. Set FORCE=1 to reinstall/upgrade."
  exit 0
fi

WORK="$(mktemp -d /tmp/activemq.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

echo "Downloading ${URL}..."
wget -O "${FILENAME}" "${URL}"
file "${FILENAME}" | grep -qi 'gzip compressed data'
tar -xzf "${FILENAME}"

SRC_DIR="apache-activemq-${ACTIVEMQ_VERSION}"
[ -d "${SRC_DIR}" ]

sudo mkdir -p "$(dirname "${INSTALL_DIR}")"
if [ -d "${INSTALL_DIR}" ]; then
  ts="$(date +%Y%m%d-%H%M%S)"
  sudo mv "${INSTALL_DIR}" "${INSTALL_DIR}.bak.${ts}"
fi
sudo mv "${SRC_DIR}" "${INSTALL_DIR}"
"${INSTALL_DIR}/bin/activemq" --version || true
`),
				shellStep("configure remote access and systemd service", `
INSTALL_DIR="${INSTALL_DIR:-/opt/apache-activemq}"
JETTY_CONFIG="${INSTALL_DIR}/conf/jetty.xml"
SERVICE_FILE="/etc/systemd/system/activemq.service"

if [ -f "${JETTY_CONFIG}" ]; then
  sudo cp -n "${JETTY_CONFIG}" "${JETTY_CONFIG}.ORIGINAL" || true
  PI_IP="$(hostname -I | awk '{print $1}')"
  if [ -z "${PI_IP}" ]; then
    PI_IP="0.0.0.0"
  fi
  sudo sed -i "s/127\\.0\\.0\\.1/${PI_IP}/g" "${JETTY_CONFIG}" || true
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
sudo systemctl restart activemq || true
sudo systemctl --no-pager --full status activemq || true
`),
			},
		},
		{
			id:          "activemq-cpp",
			description: "Install ActiveMQ C++ CMS client libraries",
			steps: []commandStep{
				{
					name: "install activemq-cpp prerequisites",
					cmd: []string{
						"sudo", "apt", "install", "-y",
						"autoconf", "automake", "libtool", "pkg-config",
						"build-essential", "libssl-dev", "libapr1-dev", "libaprutil1-dev",
						"libcppunit-dev", "uuid-dev", "doxygen", "git",
					},
				},
				shellStep("build and install activemq-cpp", `
FORCE="${FORCE:-0}"
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

if ldconfig -p 2>/dev/null | grep -q "libactivemq-cpp" && [ "${FORCE}" != "1" ]; then
  echo "ActiveMQ-CPP already installed. Set FORCE=1 to rebuild."
  exit 0
fi

WORK_DIR="$(mktemp -d /tmp/activemq-cpp.XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR"

git clone https://gitbox.apache.org/repos/asf/activemq-cpp.git
cd activemq-cpp/activemq-cpp

./autogen.sh
./configure
make -j"${BUILD_JOBS}"
sudo make install
sudo ldconfig || true

if [ "${RUN_DOCS:-0}" = "1" ]; then
  make doxygen-run || true
fi

if [ "${RUN_TESTS:-0}" = "1" ]; then
  make check || true
fi
`),
			},
		},
		{
			id:          "lgpio",
			description: "Install lgpio and SPI configuration",
			steps: []commandStep{
				{
					name: "install lgpio prerequisites",
					cmd:  []string{"sudo", "apt", "install", "-y", "wget", "unzip", "build-essential", "ca-certificates"},
				},
				shellStep("build and install lgpio", `
FORCE="${FORCE:-0}"
LGPIO_URL="${LGPIO_URL:-http://abyz.me.uk/lg/lg.zip}"
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

if ldconfig -p 2>/dev/null | grep -q 'liblgpio\.so' && [ "${FORCE}" != "1" ]; then
  echo "lgpio already installed. Set FORCE=1 to rebuild."
  exit 0
fi

WORK_DIR="$(mktemp -d /tmp/lgpio.XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
cd "$WORK_DIR"

wget -O lg.zip "${LGPIO_URL}"
unzip -q lg.zip
cd lg
make -j"${BUILD_JOBS}"
sudo make install
sudo ldconfig || true
`),
				shellStep("install pkg-config metadata and enable SPI", `
ARCH_PKGCONFIG="/usr/lib/aarch64-linux-gnu/pkgconfig"
GENERIC_PKGCONFIG="/usr/lib/pkgconfig"
CONFIG_FILE="${CONFIG_FILE:-/boot/firmware/config.txt}"

PC_CONTENT='prefix=/usr/local
exec_prefix=${prefix}
libdir=${exec_prefix}/lib
includedir=${prefix}/include

Name: lgpio
Description: LGPIO library for Raspberry Pi GPIO
Version: 0.2
Libs: -L${libdir} -llgpio
Cflags: -I${includedir}'

sudo mkdir -p "${ARCH_PKGCONFIG}" "${GENERIC_PKGCONFIG}"
echo "${PC_CONTENT}" | sudo tee "${ARCH_PKGCONFIG}/lgpio.pc" >/dev/null
echo "${PC_CONTENT}" | sudo tee "${GENERIC_PKGCONFIG}/lgpio.pc" >/dev/null

if [ -f "${CONFIG_FILE}" ]; then
  if grep -q '^dtparam=spi=on' "${CONFIG_FILE}"; then
    echo "SPI already enabled in ${CONFIG_FILE}"
  elif grep -q '^dtparam=spi=off' "${CONFIG_FILE}"; then
    sudo sed -i 's/^dtparam=spi=off/dtparam=spi=on/' "${CONFIG_FILE}"
  else
    echo 'dtparam=spi=on' | sudo tee -a "${CONFIG_FILE}" >/dev/null
  fi
else
  echo "warning: ${CONFIG_FILE} not found; skipped SPI config."
fi

if [ -e /dev/spidev0.0 ] && [ -e /dev/spidev0.1 ]; then
  echo "SPI devices available."
else
  echo "SPI device files not present yet; reboot may be required on Pi hardware."
fi
`),
			},
		},
		{
			id:          "libcamera",
			description: "Install libcamera and rpicam-apps",
			steps: []commandStep{
				{
					name: "install libcamera and rpicam prerequisites",
					cmd: []string{
						"sudo", "apt", "install", "-y",
						"git", "python3", "python3-pip", "python3-graphviz", "python3-sphinx",
						"python3-yaml", "python3-ply", "python3-jinja2",
						"doxygen", "libevent-dev", "pybind11-dev", "libavdevice-dev",
						"qtbase5-dev",
						"meson", "cmake", "ninja-build", "pkg-config", "build-essential",
						"libglib2.0-dev", "libgstreamer-plugins-base1.0-dev",
						"unzip", "wget", "ca-certificates",
						"libboost-program-options-dev", "libdrm-dev", "libexif-dev",
					},
				},
				shellStep("build and install libcamera", `
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

if pkg-config --exists libcamera 2>/dev/null || pkg-config --exists libcamera-base 2>/dev/null; then
  echo "libcamera already present via pkg-config. Skipping build."
  exit 0
fi

LIBCAMERA_REF="${LIBCAMERA_REF:-main}"
WORK_DIR="${LIBCAMERA_WORK_DIR:-/tmp/pitrac-libcamera-build}"

mkdir -p "${WORK_DIR}"
if [ ! -d "${WORK_DIR}/libcamera/.git" ]; then
  git clone https://github.com/raspberrypi/libcamera.git "${WORK_DIR}/libcamera"
fi

cd "${WORK_DIR}/libcamera"
git fetch --tags origin
git checkout "${LIBCAMERA_REF}"

if [ -d build ]; then
  meson setup build \
    --buildtype=release \
    -Dpipelines=rpi/vc4,rpi/pisp \
    -Dipas=rpi/vc4,rpi/pisp \
    -Dv4l2=enabled \
    -Dgstreamer=enabled \
    -Dtest=false \
    -Dlc-compliance=disabled \
    -Dcam=disabled \
    -Dqcam=disabled \
    -Ddocumentation=disabled \
    -Dpycamera=enabled \
    --reconfigure
else
  meson setup build \
    --buildtype=release \
    -Dpipelines=rpi/vc4,rpi/pisp \
    -Dipas=rpi/vc4,rpi/pisp \
    -Dv4l2=enabled \
    -Dgstreamer=enabled \
    -Dtest=false \
    -Dlc-compliance=disabled \
    -Dcam=disabled \
    -Dqcam=disabled \
    -Ddocumentation=disabled \
    -Dpycamera=enabled
fi

meson compile -C build -j "${BUILD_JOBS}"
sudo ninja -C build install
sudo ldconfig || true
`),
				shellStep("build and install rpicam-apps", `
REQUIRED_RPICAM_APPS_VERSION="${REQUIRED_RPICAM_APPS_VERSION:-1.5.3}"
RPICAM_REF="${RPICAM_REF:-main}"
WORK_DIR="${RPICAM_WORK_DIR:-/tmp/pitrac-rpicam-build}"
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

version_ge() {
  [ "$1" = "$2" ] && return 0
  [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n1)" = "$1" ]
}

if command -v rpicam-still >/dev/null 2>&1; then
  installed="$(rpicam-still --version 2>/dev/null | awk '/rpicam-apps build/ {print $3}' | sed 's/^v//')"
  if [ -n "${installed}" ] && version_ge "${installed}" "${REQUIRED_RPICAM_APPS_VERSION}"; then
    echo "rpicam-apps ${installed} already satisfies >= ${REQUIRED_RPICAM_APPS_VERSION}. Skipping."
    exit 0
  fi
fi

mkdir -p "${WORK_DIR}"
if [ ! -d "${WORK_DIR}/rpicam-apps/.git" ]; then
  git clone https://github.com/raspberrypi/rpicam-apps.git "${WORK_DIR}/rpicam-apps"
fi

cd "${WORK_DIR}/rpicam-apps"
git fetch --tags origin
git checkout "${RPICAM_REF}"

export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:/usr/local/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"

if [ -d build ]; then
  meson setup build \
    -Denable_libav=enabled \
    -Denable_drm=enabled \
    -Denable_egl=enabled \
    -Denable_qt=enabled \
    -Denable_opencv=enabled \
    -Denable_tflite=disabled \
    -Denable_hailo=disabled \
    --reconfigure
else
  meson setup build \
    -Denable_libav=enabled \
    -Denable_drm=enabled \
    -Denable_egl=enabled \
    -Denable_qt=enabled \
    -Denable_opencv=enabled \
    -Denable_tflite=disabled \
    -Denable_hailo=disabled
fi

meson compile -C build -j "${BUILD_JOBS}"
sudo meson install -C build
sudo ldconfig || true
rpicam-still --version || true
`),
			},
		},
		{
			id:          "opencv",
			description: "Build and install OpenCV from source",
			steps: []commandStep{
				{
					name: "install OpenCV build prerequisites",
					cmd: []string{
						"sudo", "apt", "install", "-y",
						"build-essential", "cmake", "git", "pkg-config",
						"libjpeg-dev", "libpng-dev", "libtiff-dev",
						"libavcodec-dev", "libavformat-dev", "libswscale-dev",
						"libv4l-dev", "v4l-utils", "libxvidcore-dev", "libx264-dev",
						"libgtk-3-dev", "libtbbmalloc2", "libtbb-dev",
						"python3-dev", "python3-numpy",
					},
				},
				shellStep("clone and build OpenCV", `
OPENCV_VERSION="${REQUIRED_OPENCV_VERSION:-4.12.0}"
OPENCV_DIR="${OPENCV_DIR:-$HOME/opencv-${OPENCV_VERSION}}"
OPENCV_CONTRIB_DIR="${OPENCV_CONTRIB_DIR:-$HOME/opencv_contrib-${OPENCV_VERSION}}"
ENABLE_PYTHON="${OPENCV_ENABLE_PYTHON:-1}"
FORCE="${FORCE:-0}"
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

installed_ver="$(pkg-config --modversion opencv4 2>/dev/null || true)"
if [ -n "${installed_ver}" ] && [ "${installed_ver}" = "${OPENCV_VERSION}" ] && [ "${FORCE}" != "1" ]; then
  echo "OpenCV ${installed_ver} already installed (matches ${OPENCV_VERSION}). Set FORCE=1 to rebuild."
  exit 0
fi

if [ -n "${installed_ver}" ] && [ "${installed_ver}" != "${OPENCV_VERSION}" ]; then
  echo "OpenCV ${installed_ver} found but ${OPENCV_VERSION} requested. Proceeding with build."
fi

if [ ! -d "${OPENCV_DIR}" ]; then
  git clone https://github.com/opencv/opencv.git "${OPENCV_DIR}"
fi
if [ ! -d "${OPENCV_CONTRIB_DIR}" ]; then
  git clone https://github.com/opencv/opencv_contrib.git "${OPENCV_CONTRIB_DIR}"
fi

git -C "${OPENCV_DIR}" fetch --all --tags
git -C "${OPENCV_DIR}" checkout "${OPENCV_VERSION}"
git -C "${OPENCV_CONTRIB_DIR}" fetch --all --tags
git -C "${OPENCV_CONTRIB_DIR}" checkout "${OPENCV_VERSION}"

mkdir -p "${OPENCV_DIR}/build"
cd "${OPENCV_DIR}/build"

PY_ON=OFF
if [ "${ENABLE_PYTHON}" = "1" ]; then
  PY_ON=ON
fi

cmake -D CMAKE_BUILD_TYPE=Release \
      -D CMAKE_INSTALL_PREFIX=/usr/local \
      -D OPENCV_EXTRA_MODULES_PATH="${OPENCV_CONTRIB_DIR}/modules" \
      -D OPENCV_GENERATE_PKGCONFIG=ON \
      -D BUILD_TESTS=OFF \
      -D BUILD_PERF_TESTS=OFF \
      -D BUILD_EXAMPLES=OFF \
      -D INSTALL_C_EXAMPLES=OFF \
      -D INSTALL_PYTHON_EXAMPLES=OFF \
      -D BUILD_opencv_python3="${PY_ON}" \
      -D BUILD_opencv_python2=OFF \
      -D WITH_TBB=ON \
      -D WITH_OPENGL=ON \
      -D WITH_V4L=ON \
      -D WITH_GSTREAMER=ON ..

make -j"${BUILD_JOBS}"
sudo make install
sudo ldconfig
pkg-config --modversion opencv4 || true
`),
			},
		},
		{
			id:          "onnx",
			description: "Build and install ONNX Runtime",
			steps: []commandStep{
				{
					name: "install ONNX Runtime build prerequisites",
					cmd: []string{
						"sudo", "apt", "install", "-y",
						"build-essential", "git", "cmake", "libeigen3-dev",
						"python3", "python3-dev", "python3-pip", "python3-venv",
						"libprotobuf-dev", "protobuf-compiler", "libssl-dev", "zlib1g-dev",
					},
				},
				shellStep("clone/build/install ONNX Runtime", `
ORT_VERSION="${ORT_VERSION:-1.17.3}"
ORT_REPO_URL="${ORT_REPO_URL:-https://github.com/microsoft/onnxruntime.git}"
ORT_SRC_DIR="${ORT_SRC_DIR:-$HOME/src/onnxruntime-${ORT_VERSION}}"
BUILD_CONFIG="${BUILD_CONFIG:-Release}"
BUILD_WHEEL="${BUILD_WHEEL:-0}"
FORCE="${FORCE:-0}"
DEFAULT_JOBS=4
if command -v nproc >/dev/null 2>&1; then
  DEFAULT_JOBS="$(nproc)"
fi
BUILD_JOBS="${BUILD_JOBS:-${DEFAULT_JOBS}}"

if ldconfig -p 2>/dev/null | grep -q "libonnxruntime" && [ "${FORCE}" != "1" ]; then
  installed_ver="$(ldconfig -p 2>/dev/null | grep 'libonnxruntime\.so\.' | head -1 | sed 's/.*\.so\.//')"
  if [ -n "${installed_ver}" ] && [ "${installed_ver}" = "${ORT_VERSION}" ]; then
    echo "ONNX Runtime ${ORT_VERSION} already installed. Set FORCE=1 to rebuild."
    exit 0
  fi
  echo "ONNX Runtime found (${installed_ver:-unknown version}) but ${ORT_VERSION} requested."
  if [ -z "${installed_ver}" ]; then
    echo "Could not determine installed version. Set FORCE=1 to rebuild, or continuing with build."
  fi
fi

export CMAKE_BUILD_PARALLEL_LEVEL="${BUILD_JOBS}"
export MAKEFLAGS="-j${BUILD_JOBS}"

if [ -d "${ORT_SRC_DIR}/.git" ]; then
  cd "${ORT_SRC_DIR}"
  git fetch --tags origin
else
  mkdir -p "$(dirname "${ORT_SRC_DIR}")"
  git clone --recursive "${ORT_REPO_URL}" "${ORT_SRC_DIR}"
  cd "${ORT_SRC_DIR}"
fi

git checkout "v${ORT_VERSION}"
git submodule update --init --recursive

BUILD_ARGS=(
  --config "${BUILD_CONFIG}"
  --build_shared_lib
  --parallel
  --compile_no_warning_as_error
  --skip_tests
  --skip_submodule_sync
  --cmake_extra_defines "eigen_SOURCE_PATH=/usr/include/eigen3"
  --cmake_extra_defines "onnxruntime_USE_PREINSTALLED_EIGEN=ON"
)

if [ "${BUILD_WHEEL}" = "1" ]; then
  BUILD_ARGS+=( --build_wheel )
fi

./build.sh "${BUILD_ARGS[@]}"

BUILD_DIR="build/Linux/${BUILD_CONFIG}"
[ -d "${BUILD_DIR}" ]

sudo cmake --install "${BUILD_DIR}" --prefix /usr/local
sudo ldconfig
ldconfig -p | grep onnxruntime || true

BASE="/usr/local/include/onnxruntime"
TARGET="${BASE}/core/session"
sudo mkdir -p "${TARGET}"

for h in \
  onnxruntime_cxx_api.h \
  onnxruntime_c_api.h \
  onnxruntime_cxx_inline.h \
  onnxruntime_float16.h \
  onnxruntime_run_options_config_keys.h \
  onnxruntime_session_options_config_keys.h \
  cpu_provider_factory.h
do
  if [ -e "${BASE}/${h}" ] && [ ! -e "${TARGET}/${h}" ]; then
    sudo ln -s "../../${h}" "${TARGET}/${h}"
  fi
done
`),
			},
		},
		{
			id:          "camera-timeout",
			description: "Configure libcamera pipeline timeout for Pi 5 (pisp)",
			steps: []commandStep{
				shellStep("configure camera timeout", `
# Set camera_timeout_value_ms in the libcamera pisp pipeline config.
# Without this, the IMX296 global shutter sensor can trigger spurious
# "Camera frontend has timed out!" errors during startup.
# Value of 10000000 (10s) matches the recommendation in libcamera_jpeg.cpp:441-443.

TIMEOUT_VALUE=10000000

YAML_CANDIDATES=(
  "/usr/local/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
  "/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml"
)

YAML_FILE=""
for f in "${YAML_CANDIDATES[@]}"; do
  if [ -f "$f" ]; then
    YAML_FILE="$f"
    break
  fi
done

if [ -z "$YAML_FILE" ]; then
  echo "No rpi_apps.yaml found at expected Pi 5 (pisp) paths:"
  printf "  %s\n" "${YAML_CANDIDATES[@]}"
  echo "Skipping camera timeout configuration."
  exit 0
fi

echo "Found pipeline config: $YAML_FILE"

# Check if already configured with our value
if grep -q "\"camera_timeout_value_ms\": *${TIMEOUT_VALUE}" "$YAML_FILE" 2>/dev/null; then
  echo "camera_timeout_value_ms already set to ${TIMEOUT_VALUE}. Nothing to do."
  exit 0
fi

# Back up the original
BACKUP="${YAML_FILE}.bak.$(date +%Y%m%d_%H%M%S)"
sudo cp "$YAML_FILE" "$BACKUP"
echo "Backed up original to: $BACKUP"

# Write the correct content (the file is small JSON-in-YAML)
sudo tee "$YAML_FILE" > /dev/null <<EOFYAML
{
    "pipeline_handler":
    {
        "camera_timeout_value_ms": ${TIMEOUT_VALUE}
    }
}
EOFYAML

echo "Set camera_timeout_value_ms to ${TIMEOUT_VALUE} in $YAML_FILE"
`),
			},
		},
	}
}
