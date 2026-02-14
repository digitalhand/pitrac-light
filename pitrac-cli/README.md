# PiTrac CLI (`pitrac-cli`)

A Go-based command line tool for PiTrac installation, environment setup, and runtime configuration.

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Install the CLI](#install-the-cli)
- [Build from Source](#build-from-source)
- [Versioning](#versioning)
- [Two-Step Env Setup](#two-step-env-setup)
- [Command Reference](#command-reference)
  - [Top-level Commands](#top-level-commands)
  - [`doctor`](#doctor)
  - [`env`](#env)
  - [`config`](#config)
  - [`run`](#run)
  - [`build`](#build)
  - [`install`](#install)
  - [`validate`](#validate)
  - [`service`](#service)
- [Install Targets](#install-targets)
- [Install Override Variables](#install-override-variables)
- [Output and Styling](#output-and-styling)
- [Troubleshooting](#troubleshooting)

## Overview
`pitrac-cli` replaces legacy setup scripting with a single CLI for:
- dependency installation
- environment file management
- command argument generation/validation
- machine readiness checks

## Prerequisites
- Go `1.21+`
- Linux environment (Raspberry Pi OS arm64 supported)
- `sudo` access for install targets that write system packages/files

## Install the CLI
Preferred Go install location is your Go binary path (`$GOBIN` or `$GOPATH/bin`).

```bash
go install github.com/digitalhand/pitrac-light/pitrac-cli@latest
```

Ensure Go bin path is on `PATH`:

```bash
echo 'export PATH="$PATH:$(go env GOPATH)/bin"' >> ~/.bashrc
source ~/.bashrc
```

## Build from Source
From this folder (`pitrac-cli`):

```bash
go mod tidy
go build -o pitrac-cli .
./pitrac-cli --version
```

If you see stale behavior, force a clean rebuild:

```bash
go clean -cache
go build -a -o pitrac-cli .
```

## Versioning
Show version:

```bash
pitrac-cli version
pitrac-cli --version
```

Version output is resolved from:
1. build-time ldflags (`main.version`, `main.commit`, `main.date`)
2. Go build metadata (`vcs.revision`, `vcs.time`)

Example release build:

```bash
go build -ldflags "-X main.version=v0.1.0 -X main.commit=$(git rev-parse --short HEAD) -X main.date=$(date -u +%Y-%m-%dT%H:%M:%SZ)" -o pitrac-cli .
```

## Two-Step Env Setup
Use these two commands in most cases:

```bash
pitrac-cli env setup --force
pitrac-cli env validate
```

What they do:
- `env setup`: generates env file and updates shell profile (`~/.bashrc` by default)
- `env validate`: confirms required PiTrac vars are present and shell sourcing is configured

To reset/remove environment configuration (useful when switching repos or fixing incorrect paths):
```bash
pitrac-cli env reset
```

## Command Reference

### Top-level Commands
```bash
pitrac-cli doctor
pitrac-cli version
pitrac-cli env ...
pitrac-cli config ...
pitrac-cli run ...
pitrac-cli build ...
pitrac-cli install ...
pitrac-cli validate ...
pitrac-cli service ...
```

### `doctor`
Checks required tools, key files, and required/optional environment variables.

```bash
pitrac-cli doctor
```

Current required tool checks:
- `meson`, `ninja`, `cmake`, `pkg-config`, `g++`

Current required file checks (repo-root relative):
- `src/meson.build`
- `src/golf_sim_config.json`
- `assets/models/best.onnx`
- `assets/CameraTools/imx296_trigger`

### `env`
Manage env file generation, display, and shell integration.

```bash
pitrac-cli env setup [--force] [--pitrac-root <path>] [--env-file <path>] [--shell-file <path>]
pitrac-cli env validate [--env-file <path>] [--shell-file <path>]
pitrac-cli env reset [--yes] [--shell-file <path>] [--keep-dirs]
pitrac-cli env init [--force] [--pitrac-root <path>] [--env-file <path>]
pitrac-cli env show [--env-file <path>]
pitrac-cli env apply [--env-file <path>] [--shell-file <path>]
```

If subcommand is missing/invalid, env-scoped help is shown:

```bash
pitrac-cli env
pitrac-cli env help
```

Recommended:
- Use `setup` + `validate` as the default flow.
- Use `init`/`apply` only if you want to split those operations manually.

Default env file path:
- `~/.pitrac/config/pitrac.env`

`env init` default values:
- `PITRAC_ROOT`: auto-detected repo root (or `/path/to/pitrac-light` fallback)
- `PITRAC_MSG_BROKER_FULL_ADDRESS`: `tcp://127.0.0.1:61616`
- `PITRAC_WEBSERVER_SHARE_DIR`: `$HOME/LM_Shares/Images/`
- `PITRAC_BASE_IMAGE_LOGGING_DIR`: `$HOME/LM_Shares/PiTracLogs`
- `PITRAC_WEB_SERVER_URL`: `http://localhost:8080`

### `config`
Manage and resolve runtime configuration.

```bash
pitrac-cli config init [--force]
pitrac-cli config args [--env-file <path>]
pitrac-cli config validate [--env-file <path>]
```

**`config init`** copies `$PITRAC_ROOT/src/golf_sim_config.json` to `~/.pitrac/config/golf_sim_config.json`. Fails if the destination already exists unless `--force` is passed.

**`config args`** merges process environment + env file values, validates required vars, and outputs the resolved `pitrac_lm` arguments. The config file is resolved in order:
1. `~/.pitrac/config/golf_sim_config.json` (if it exists, preferred)
2. `$PITRAC_ROOT/src/golf_sim_config.json` (fallback)

### `run`
Run `pitrac_lm` in various modes without memorizing argument syntax.

```bash
pitrac-cli run pulse-test      [--camera <1|2>] [--dry-run]
pitrac-cli run still           [--camera <1|2>] [--dry-run]
pitrac-cli run ball-location   [--camera <1|2>] [--dry-run]
pitrac-cli run cam             [--camera <1|2>] [--dry-run]
pitrac-cli run calibrate       [--camera <1|2>] [--dry-run]
pitrac-cli run auto-calibrate  [--camera <1|2>] [--dry-run]
pitrac-cli run shutdown        [--dry-run]
pitrac-cli run calibrate-gui   [--camera <1|2>] [--mode <intrinsic|extrinsic|full>] [--dry-run]
```

Shared flags:
- `--camera <1|2>` (default `1`) — selects camera1 or camera2 mode variants
- `--dry-run` — prints the full `pitrac_lm` command without executing

Each subcommand resolves the binary at `$PITRAC_ROOT/src/build/pitrac_lm`, builds common args from env (same logic as `config args`), and appends mode-specific flags.

| Subcommand | pitrac_lm args |
| --- | --- |
| `pulse-test` | `--pulse_test --system_mode camera1` |
| `still` | `--system_mode camera1 --cam_still_mode` |
| `ball-location` | `--system_mode camera1_ball_location` |
| `cam` | `--system_mode camera1` |
| `calibrate` | `--system_mode camera1Calibrate` |
| `auto-calibrate` | `--system_mode camera1AutoCalibrate` |
| `shutdown` | `--shutdown` |
| `calibrate-gui` | Launches `python3 -m pitrac_cal` (see [`pitrac_cal/README.md`](../pitrac_cal/README.md)) |

Use `--dry-run` to inspect the resolved command before running:

```bash
pitrac-cli run pulse-test --dry-run
pitrac-cli run still --camera 2 --dry-run
```

**`calibrate-gui`** launches the Python-based calibration GUI (`pitrac_cal`) instead of `pitrac_lm`. It requires Python 3.9+, OpenCV with Python bindings, and picamera2 on the Pi.

```bash
pitrac-cli run calibrate-gui --camera 1 --mode full         # intrinsic then extrinsic
pitrac-cli run calibrate-gui --camera 1 --mode intrinsic    # lens calibration only
pitrac-cli run calibrate-gui --camera 2 --mode extrinsic    # focal length + angles only
```

| Flag | Default | Description |
|------|---------|-------------|
| `--camera` | `1` | Camera number (1 or 2) |
| `--mode` | `full` | `intrinsic`, `extrinsic`, or `full` |
| `--dry-run` | `false` | Print the `python3` command without executing |

The command sets `DISPLAY=:0.0` for VNC compatibility. See [`pitrac_cal/README.md`](../pitrac_cal/README.md) for the full calibration workflow, keyboard controls, and troubleshooting.

### `build`
Build PiTrac from source using Meson + Ninja.

```bash
pitrac-cli build [--clean] [--jobs N] [--type release|debug|debugoptimized] [--test] [--dry-run]
```

| Flag | Default | Description |
| --- | --- | --- |
| `--clean` | `false` | Wipe the build directory before building |
| `--jobs` | `4` | Number of parallel build jobs |
| `--type` | `release` | Meson build type (`release`, `debug`, `debugoptimized`) |
| `--test` | `false` | Run `meson test` after the build completes |
| `--dry-run` | `false` | Print commands without executing |

The command runs `meson setup` (with `--reconfigure` if the build directory already exists) then `ninja -C build`. The install prefix is `/opt/pitrac`.

```bash
pitrac-cli build                          # default release build
pitrac-cli build --type debug --test      # debug build + run tests
pitrac-cli build --clean --jobs 2         # clean rebuild with 2 jobs
```

### `install`
List and run install targets.

```bash
pitrac-cli install list
pitrac-cli install <target> [--dry-run] [--yes]
pitrac-cli install run <target> [--dry-run] [--yes]   # alias
pitrac-cli install base [--dry-run] [--yes]
```

Notes:
- Preferred syntax: `pitrac-cli install <target>`
- `--dry-run` prints commands only
- `--yes` skips confirmation prompt
- `install base` runs: `source-deps`, `boost`, `java`, `msgpack-cxx`

### `validate`
Validate that the PiTrac environment, configuration, and dependencies are correctly set up.

```bash
pitrac-cli validate env [--env-file <path>] [--shell-file <path>]
pitrac-cli validate config [--env-file <path>]
pitrac-cli validate install
```

**`validate env`** — confirms required environment variables (`PITRAC_ROOT`, `PITRAC_MSG_BROKER_FULL_ADDRESS`, `PITRAC_WEBSERVER_SHARE_DIR`, `PITRAC_BASE_IMAGE_LOGGING_DIR`) are set and that the env file is sourced in your shell profile.

| Flag | Default | Description |
| --- | --- | --- |
| `--env-file` | `~/.pitrac/.env` | Path to the env file |
| `--shell-file` | auto (`.bashrc` / `.zshrc`) | Shell RC file expected to source the env file |

**`validate config`** — checks that required env vars are present and that `golf_sim_config.json` exists at `$PITRAC_ROOT/src/golf_sim_config.json`.

| Flag | Default | Description |
| --- | --- | --- |
| `--env-file` | `~/.pitrac/.env` | Path to the env file |

**`validate install`** — checks whether each dependency (source-deps, boost, java, msgpack-cxx, mq-broker, activemq-cpp, lgpio, libcamera, opencv, onnx) is installed. Uses `dpkg`, `pkg-config`, `ldconfig`, and version checks as appropriate.

### `service`
Manage PiTrac background services (ActiveMQ broker and camera processes).

#### Convenience commands

```bash
pitrac-cli service start    # start broker + camera 1 + camera 2
pitrac-cli service stop     # stop cameras + broker
pitrac-cli service status   # show status of all services
```

#### Broker subgroup

```bash
pitrac-cli service broker start    # start ActiveMQ via systemctl
pitrac-cli service broker stop     # stop ActiveMQ via systemctl
pitrac-cli service broker status   # check broker systemd state + ports 61616/8161
pitrac-cli service broker setup    # create systemd unit + configure remote access
```

`broker setup` creates `/etc/systemd/system/activemq.service`, updates `/opt/apache-activemq/conf/jetty.xml` to bind to the Pi's IP, and enables the service at boot.

#### Launch-monitor (LM) subgroup

```bash
pitrac-cli service lm start [--camera <1|2>]   # start camera process(es)
pitrac-cli service lm stop                      # stop camera processes
pitrac-cli service lm status                    # check camera process status
```

| Flag | Default | Description |
| --- | --- | --- |
| `--camera` | both | Start only camera 1 or 2 (omit for both) |

#### File paths

| Path | Purpose |
| --- | --- |
| `~/.pitrac/run/camera1.pid` | PID file for camera 1 |
| `~/.pitrac/run/camera2.pid` | PID file for camera 2 |
| `~/.pitrac/log/camera1.log` | Log output for camera 1 |
| `~/.pitrac/log/camera2.log` | Log output for camera 2 |

## Install Targets
Current targets:
- `source-deps`
- `boost`
- `java`
- `msgpack-cxx`
- `mq-broker`
- `activemq-cpp`
- `lgpio`
- `libcamera`
- `opencv`
- `onnx`

Inspect available targets:

```bash
pitrac-cli install list
```

## Install Override Variables
Some install targets support shell env overrides.

### Shared build override
- `BUILD_JOBS` (default auto-detect via `nproc`, fallback `4`)
- Applies to source-build targets (for example `activemq-cpp`, `lgpio`, `libcamera`, `opencv`, `onnx`)
- Example:

```bash
BUILD_JOBS=4 pitrac-cli install onnx --yes
```

### `mq-broker`
- `ACTIVEMQ_VERSION` (default `6.1.7`)
- `ACTIVEMQ_URL` (default archive URL built from version)
- `INSTALL_DIR` (default `/opt/apache-activemq`)
- `FORCE` (`1` to reinstall/replace existing)

### `activemq-cpp`
- `FORCE` (`1` to rebuild even if installed)
- `RUN_DOCS` (`1` to run doxygen)
- `RUN_TESTS` (`1` to run `make check`)

### `lgpio`
- `FORCE` (`1` to rebuild)
- `LGPIO_URL` (default `http://abyz.me.uk/lg/lg.zip`)
- `CONFIG_FILE` (default `/boot/firmware/config.txt`)

### `libcamera`
- `LIBCAMERA_REF` (default `main`)
- `LIBCAMERA_WORK_DIR` (default `/tmp/pitrac-libcamera-build`)
- `REQUIRED_RPICAM_APPS_VERSION` (default `1.5.3`)
- `RPICAM_REF` (default `main`)
- `RPICAM_WORK_DIR` (default `/tmp/pitrac-rpicam-build`)

### `opencv`
- `REQUIRED_OPENCV_VERSION` (default `4.11.0`)
- `OPENCV_DIR` (default `$HOME/opencv-<version>`)
- `OPENCV_CONTRIB_DIR` (default `$HOME/opencv_contrib-<version>`)
- `OPENCV_ENABLE_PYTHON` (`1` enables Python bindings)

### `onnx`
- `ORT_VERSION` (default `1.17.3`)
- `ORT_REPO_URL` (default `https://github.com/microsoft/onnxruntime.git`)
- `ORT_SRC_DIR` (default `$HOME/src/onnxruntime-<version>`)
- `BUILD_CONFIG` (default `Release`)
- `BUILD_WHEEL` (`1` to build wheel)

## Output and Styling
The CLI uses symbols in status output:
- success: `✓`
- warning: `!`
- failure: `✗`
- info/list item: `•`

Disable ANSI colors by setting `NO_COLOR=1`.

## Troubleshooting

### `Exec format error` when running `./pitrac-cli`
Usually means stale/cross-compiled binary.

```bash
unset GOOS GOARCH GOARM CGO_ENABLED
rm -f ./pitrac-cli
go build -o ./pitrac-cli .
file ./pitrac-cli
```

Expected on Pi OS arm64: `ELF 64-bit ... ARM aarch64`.

### Old command output still appears
Rebuild and replace the local binary:

```bash
go clean -cache
go build -a -o ./pitrac-cli .
./pitrac-cli help
```

### ONNX build tries to download Eigen and fails
The CLI passes preinstalled Eigen defines, but if you have old source/binary, update and rebuild.

### `chdir: no such file or directory` or wrong PITRAC_ROOT path
The CLI auto-detects the repository root by searching for `src/meson.build` and `README.md`. If it finds the wrong directory (e.g., old `PiTracLight` vs new `pitrac-light`), you have options:

**Option 1: Set PITRAC_ROOT explicitly**
```bash
export PITRAC_ROOT=/home/pitracuser/pitrac-light
pitrac-cli build
```

**Option 2: Remove old configuration**
```bash
# Check for old config
cat ~/.pitrac/config/pitrac.env

# If it has wrong PITRAC_ROOT, regenerate
pitrac-cli env setup --force
source ~/.bashrc
```

**Option 3: Run from repo directory**
```bash
cd /home/pitracuser/pitrac-light
./pitrac-cli/pitrac-cli build
```

**Option 4: Rebuild CLI from latest code**
```bash
cd /home/pitracuser/pitrac-light/pitrac-cli
go clean -cache
go build -a -o pitrac-cli .
./pitrac-cli --version  # Verify new version
```

Manual prerequisites:

```bash
sudo apt install -y libeigen3-dev
```

Then rerun:

```bash
pitrac-cli install onnx --yes
```

### Builds are too slow
Use explicit job count:

```bash
BUILD_JOBS=4 pitrac-cli install onnx --yes
```

For lower-memory Pis, reducing jobs can sometimes finish faster by avoiding swap thrash:

```bash
BUILD_JOBS=2 pitrac-cli install onnx --yes
```
