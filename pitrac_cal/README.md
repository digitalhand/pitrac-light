# PiTrac Calibration Tool (`pitrac-cal`)

A Python-based camera calibration GUI for PiTrac that performs both intrinsic (lens) and extrinsic (position/angle) calibration with live visual feedback, then writes results directly to `golf_sim_config.json`.

## Table of Contents
- [Overview](#overview)
- [Architecture](#architecture)
  - [Technology Choices](#technology-choices)
  - [Module Structure](#module-structure)
  - [State Machine](#state-machine)
  - [Camera Pipeline](#camera-pipeline)
  - [OpenCV 4.12 Details](#opencv-412-details)
  - [C++ Parity](#c-parity)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
  - [Launch via pitrac-cli](#launch-via-pitrac-cli)
  - [Launch directly](#launch-directly)
  - [Generate a CharucoBoard](#generate-a-charucoboard)
- [Calibration Workflow](#calibration-workflow)
  - [Intrinsic Calibration (CharucoBoard)](#intrinsic-calibration-charucoboard)
  - [Extrinsic Calibration (Golf Ball)](#extrinsic-calibration-golf-ball)
  - [Full Calibration](#full-calibration)
- [Keyboard Controls](#keyboard-controls)
- [Command-Line Reference](#command-line-reference)
- [How It Works](#how-it-works)
  - [Intrinsic Calibration](#intrinsic-calibration)
  - [Extrinsic Calibration](#extrinsic-calibration)
  - [Config File Format](#config-file-format)
- [Environment Variables](#environment-variables)
- [Off-Pi Development and Testing](#off-pi-development-and-testing)
- [Troubleshooting](#troubleshooting)

## Overview

PiTrac's camera calibration has two parts:

1. **Intrinsic calibration** determines the camera matrix and distortion coefficients for the lens. These correct for barrel/pincushion distortion and are specific to each physical camera+lens combination.

2. **Extrinsic calibration** determines the focal length and camera mounting angles (yaw, pitch). These change whenever a camera is moved or repositioned in the enclosure.

Previously, intrinsic parameters were computed manually and hardcoded, while extrinsic calibration ran headless through `pitrac_lm` with no visual feedback. `pitrac-cal` replaces both workflows with a single interactive tool that shows exactly what the camera sees and what the algorithms are detecting.

The primary workflow is one-button: press **ENTER** to auto-capture and calibrate.

## Architecture

### Technology Choices

| Component | Choice | Rationale |
|-----------|--------|-----------|
| Language | Python 3.9+ | OpenCV's calibration API, CharucoBoard detection, and HoughCircles are fully exposed in the Python bindings. No C++ compilation needed for the calibration workflow. |
| GUI | OpenCV `highgui` (`cv2.imshow` + keyboard) | Zero extra dependencies. Works over VNC. No Tkinter/Qt required. The window is created with `cv2.namedWindow(name, cv2.WINDOW_NORMAL)` which allows resizing. |
| Camera capture | `rpicam-still` subprocess | Avoids picamera2, which has sensor-mode selection bugs with the IMX296 global shutter camera (selects 96x88 instead of 1456x1088). rpicam-still uses the same libcamera pipeline as the C++ `pitrac_lm`. Before first capture, the IMX296 sensor format is configured via `media-ctl`, matching the C++ `ConfigCameraForFullScreenWatching()` flow. |
| Config format | JSON with string-typed values | PiTrac's C++ code uses Boost.property_tree which stores all values as JSON strings. Python preserves this with `f"{value:.17g}"` formatting. |
| Off-Pi testing | `--image-dir` flag | Loads images from a directory instead of live camera. No Pi hardware needed for algorithm development. |

### Module Structure

```
pitrac_cal/
├── __init__.py            # Package marker
├── __main__.py            # Entry point — state machine driving the calibration loop
├── cli.py                 # argparse argument definitions
├── camera.py              # CameraSource protocol + RpicamSource / DirectorySource
├── intrinsic.py           # CharucoBoard creation, corner detection, cv2.calibrateCamera
├── extrinsic.py           # HoughCircles ball detection, focal length, camera angles
├── config_manager.py      # Read/write golf_sim_config.json with timestamped backups
├── display.py             # OpenCV HUD overlay rendering (text, circles, crosshairs)
├── constants.py           # Hardware constants from camera_hardware.cpp
├── requirements.txt       # Off-Pi only (dev machine pip install)
└── tests/
    ├── __init__.py
    ├── test_config.py     # Config round-trip, string precision, ball position, backups
    ├── test_extrinsic.py  # Focal length formula parity, distance conversions, angles
    └── test_intrinsic.py  # Board creation, corner detection, calibration with synthetic views
```

Each module has a single responsibility and depends only on `constants.py` and the standard library/OpenCV/numpy. The `__main__.py` state machine orchestrates the modules but contains no calibration logic itself.

### State Machine

The UI is a keyboard-driven state machine in `__main__.py`. Each calibration mode has three states:

```
PREVIEW  ──ENTER──▶  AUTO  ──(enough captures)──▶  DONE
   ▲                  │                              │
   └──────Q(cancel)───┘                              └──S=save, Q=quit
```

**Preview** shows the live camera feed with detection overlays (charuco corners or ball circle). **Auto** captures frames automatically and shows progress (`3/15`, `4/6`). **Done** shows results and waits for save or quit.

The main loop runs at ~30fps (`cv2.waitKey(30)`). Each iteration:
1. Captures a frame from the camera source
2. Runs detection (corners or ball)
3. Renders the visualization + HUD overlay
4. Checks for keyboard input and transitions state

### Camera Pipeline

`camera.py` defines a `CameraSource` protocol with two implementations:

**`RpicamSource`** (Pi hardware):
1. Discovers the IMX296 sensor's media device by scanning `/dev/media0`–`/dev/media5` with `media-ctl --print-dot` (ports `DiscoverCameraLocation()` from `libcamera_interface.cpp:501-620`)
2. Configures the sensor format via `media-ctl --set-v4l2` to set 1456x1088 resolution (ports `ConfigCameraForFullScreenWatching()` from `libcamera_interface.cpp:974-1003`)
3. Sets `LIBCAMERA_RPI_TUNING_FILE` for the correct camera type — mono (`imx296_mono.json`) or color (`imx296.json`) — by reading `PITRAC_SLOT{N}_CAMERA_TYPE` (ports `SetLibcameraTuningFileEnvVariable()` from `libcamera_interface.cpp:1007-1086`)
4. Each `capture()` call runs `rpicam-still --camera N --width 1456 --height 1088 -o /tmp/frame.png --immediate --nopreview -n` and reads the result with `cv2.imread()`
5. Applies 180° flip if `PITRAC_SLOT{N}_CAMERA_ORIENTATION` is set to `2` (upside-down)

**`DirectorySource`** (off-Pi):
- Cycles through image files in a directory on each `capture()` call
- Supports `.png`, `.jpg`, `.jpeg`, `.bmp`, `.tif`, `.tiff`

### OpenCV 4.12 Details

The codebase targets **OpenCV 4.8+** and is tested against **OpenCV 4.12** (built from source via `pitrac-cli install opencv`).

**CharucoBoard API (intrinsic.py):**

OpenCV 4.12 removed `cv2.aruco.calibrateCameraCharuco()`. The replacement is a two-step process using `CharucoBoard.matchImagePoints()` + `cv2.calibrateCamera()`:

```python
# Old API (removed in 4.12):
# rms, mtx, dist, rvecs, tvecs = cv2.aruco.calibrateCameraCharuco(
#     all_corners, all_ids, board, image_size, None, None)

# New API (4.8+):
board = cv2.aruco.CharucoBoard((7, 5), square_len, marker_len, dictionary)
detector = cv2.aruco.CharucoDetector(board)

# Detection per frame:
corners, ids, marker_corners, marker_ids = detector.detectBoard(gray_image)

# Calibration from collected frames:
for corners, ids in zip(all_corners, all_ids):
    obj_pts, img_pts = board.matchImagePoints(corners, ids)
    obj_points.append(obj_pts)
    img_points.append(img_pts)

rms, camera_matrix, dist_coeffs, rvecs, tvecs = cv2.calibrateCamera(
    obj_points, img_points, image_size, None, None)
```

**HoughCircles (extrinsic.py):**

Ball detection uses `cv2.HoughCircles()` with `HOUGH_GRADIENT`. Parameters are tuned for a white golf ball at calibration distance:

```python
circles = cv2.HoughCircles(
    blurred, cv2.HOUGH_GRADIENT,
    dp=1.2, minDist=height//4,
    param1=100, param2=30,
    minRadius=20, maxRadius=200)
```

**HUD rendering (display.py):**

Text overlays use `cv2.putText()` with a semi-transparent dark background strip rendered via `cv2.addWeighted()`:

```python
overlay = image.copy()
cv2.rectangle(overlay, (x, y), (x+w, y+h), (30, 30, 30), -1)
cv2.addWeighted(overlay, 0.65, image, 0.35, 0, image)
cv2.putText(image, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255,255,255), 2)
```

Font scale is `1.0` with thickness `2` and line height `35px`, sized for readability at 1456x1088.

### C++ Parity

All calibration formulas are direct ports from the C++ codebase, with unit tests verifying numerical parity:

| Python function | C++ source | Purpose |
|----------------|-----------|---------|
| `compute_focal_length()` | `gs_camera.cpp:909-913` | Focal length from ball pixel radius |
| `convert_x_distance_to_meters()` | `gs_camera.cpp:916-921` | Pixel X offset → meters |
| `convert_y_distance_to_meters()` | `gs_camera.cpp:923-928` | Pixel Y offset → meters |
| `compute_camera_angles()` | `gs_calibration.cpp:163-278` | Yaw/pitch from ball position |
| `get_ball_position()` | `gs_calibration.cpp:79-116` | Rig type → ball 3D position |
| `float_to_config_str()` | Boost.property_tree default | 17-digit precision formatting |
| `_discover_imx296_media()` | `libcamera_interface.cpp:501-620` | Find IMX296 media device |
| `_configure_imx296_sensor()` | `libcamera_interface.cpp:842-1003` | Set sensor format via media-ctl |
| `_setup_tuning_file()` | `libcamera_interface.cpp:1007-1086` | Select IMX296 tuning JSON |
| `_is_upside_down()` | `gs_config.cpp:286-322` | Camera orientation from env |
| `_is_camera_mono()` | `camera_hardware.cpp:134` | Camera model from env |

## Prerequisites

**Hardware:**
- Raspberry Pi 4 or 5 running Raspberry Pi OS (64-bit)
- PiTrac camera rig with IMX296 global shutter camera(s) connected
- A printed CharucoBoard (for intrinsic calibration — the tool can generate one)
- A golf ball on the calibration rig (for extrinsic calibration)

**Software:**
- Python 3.9+ (included in Raspberry Pi OS)
- OpenCV 4.8+ with Python bindings
- rpicam-apps (`rpicam-still`) — installed by `pitrac-cli install camera`
- numpy

**Display:**
- A monitor connected to the Pi, or VNC access (the tool uses OpenCV `highgui` windows)

## Installation

All dependencies are installed through `apt`. No pip or virtual environments needed.

```bash
sudo apt install -y python3-numpy python3-opencv
```

If you already ran `pitrac-cli install opencv` (or `full`), the source-built OpenCV includes Python bindings and `python3-opencv` can be skipped — but installing it anyway is harmless.

Verify:

```bash
python3 -c "import numpy, cv2; print('all OK')"
rpicam-still --version
```

## Usage

### Launch via pitrac-cli

The recommended way to run calibration on the Pi:

```bash
# Full calibration (intrinsic then extrinsic) for camera 1
pitrac-cli run calibrate-gui --camera 1 --mode full

# Intrinsic only
pitrac-cli run calibrate-gui --camera 1 --mode intrinsic

# Extrinsic only
pitrac-cli run calibrate-gui --camera 2 --mode extrinsic

# Preview the command without running
pitrac-cli run calibrate-gui --camera 1 --mode full --dry-run
```

### Launch directly

From the repository root:

```bash
# Full calibration for camera 1
python3 -m pitrac_cal --camera 1 --mode full

# With explicit config path
python3 -m pitrac_cal --camera 1 --mode intrinsic --config /path/to/golf_sim_config.json
```

### Generate a CharucoBoard

Before intrinsic calibration, you need a printed CharucoBoard. Generate the image with:

```bash
python3 -m pitrac_cal --generate-board charuco_board.png
```

The default board is 7x5 squares (30mm squares, 22.5mm markers).

**Printing options:**

- **Home printer** — Print `charuco_board.png` at actual size (100% scale, no "fit to page") on A4 or Letter paper. Measure a square with a ruler to verify it is 30mm. Tape or glue the printout to a rigid flat surface (cardboard, foam board, clipboard).
- **Print shop** — Upload the PNG to a local print shop or online service and request actual-size printing on rigid stock (foam board or cardboard mounting). This gives the best results since the board stays perfectly flat.
- **Online services** — Upload to FedEx Office (fedex.com/printing), Staples, or similar. Request a single-sided print on foam board at 100% scale.
- **Pre-made boards** — Charuco boards with the `DICT_4X4_50` dictionary can be purchased from camera calibration suppliers, but the square/marker dimensions must match (30mm/22.5mm) or you'll need to regenerate with matching dimensions.

**Important:** The board must be **flat**. A bent or curled printout will degrade calibration accuracy. Mount on rigid material if using plain paper.

## Calibration Workflow

### Intrinsic Calibration (CharucoBoard)

Determines the camera matrix and lens distortion coefficients using a printed CharucoBoard pattern.

**What you need:** A printed CharucoBoard (generate one with `--generate-board`).

**Procedure:**

1. Launch in intrinsic mode:
   ```bash
   pitrac-cli run calibrate-gui --camera 1 --mode intrinsic
   ```
2. Hold the printed CharucoBoard in front of the camera. When corners are detected, they appear as green overlays on the live preview.
3. Press **ENTER** to start auto-capture. The tool captures a frame every 2 seconds whenever corners are detected — just slowly move the board around.
4. The HUD shows progress: `AUTO-CAPTURING 7/15 — move board around slowly`. Cover:
   - Different distances (close, medium, far)
   - Different angles (tilted left, right, up, down)
   - Different positions in the frame (center, edges, corners)
5. After 15 captures, calibration runs automatically. The RMS reprojection error is displayed — values below 1.0 pixel indicate a good calibration.
6. Press **U** to toggle undistorted preview and compare before/after.
7. Press **S** to save the camera matrix and distortion vector to `golf_sim_config.json`.
8. Press **Q** to exit (or continue to extrinsic if using `--mode full`).

**Tips for good intrinsic calibration:**
- Cover the entire field of view, including edges and corners
- Keep the board flat — don't bend the paper
- Ensure even lighting with no strong reflections on the board
- Target RMS error below 0.5 for best results

### Extrinsic Calibration (Golf Ball)

Determines the focal length and camera mounting angles using a golf ball placed at a known position.

**What you need:** A golf ball placed at the calibration position on your rig.

For live camera use, camera 2 extrinsic mode defaults to strobed still capture via
`pitrac_lm --cam_still_mode` so calibration matches runtime trigger behavior.
Captured strobed frames are written to:
`~/LM_Shares/PiTracLogs/log_cam2_cal_YYYYMMDD_HHMMSS_xxx.png` by default.
Each capture gets a unique timestamped filename (and includes `cal`).
Use `--no-strobe` to disable this behavior for camera 2.

**Procedure:**

1. Place a golf ball at the calibration position defined in your config (the tool reads the position from `golf_sim_config.json` based on the rig type and camera number).
2. Launch in extrinsic mode:
   ```bash
   pitrac-cli run calibrate-gui --camera 1 --mode extrinsic
   ```
3. The live preview shows ball detection results: a green circle around the detected ball, a crosshair at the center, and the measured pixel radius.
4. Press **ENTER** to start calibration. The tool auto-captures 6 focal length samples and computes camera angles automatically.
5. Results are displayed: focal length, yaw, and pitch.
6. Press **S** to save focal length and camera angles to `golf_sim_config.json`.
   The save also updates `kExpectedBallRadiusPixelsAt40cmCamera{N}` derived from focal length.
   On save, the tool logs the exact persisted values for:
   `kCamera{N}CalibrationMatrix`, `kCamera{N}DistortionVector`,
   `kCamera{N}FocalLength`, `kCamera{N}Angles`, and `kExpectedBallRadiusPixelsAt40cmCamera{N}`.
7. Press **Q** to exit.

**Tips for good extrinsic calibration:**
- Ensure the ball is exactly at the configured calibration position
- Use consistent, even lighting — shadows on the ball affect the detected radius
- Verify the detected circle matches the ball outline before pressing ENTER

### Full Calibration

Runs intrinsic first, then extrinsic in sequence:

```bash
pitrac-cli run calibrate-gui --camera 1 --mode full
```

Press **Q** at the end of intrinsic mode to advance to extrinsic mode.

## Keyboard Controls

### Intrinsic Mode — Preview

| Key | Action |
|-----|--------|
| **ENTER** | Start auto-capture (captures 15 frames, one every 2s when board detected) |
| **SPACE** | Manual single capture (when corners detected) |
| **G** | Generate CharucoBoard image file |
| **Q** | Quit |

### Intrinsic Mode — Auto-Capture

| Key | Action |
|-----|--------|
| **SPACE** | Manual capture (adds to auto-capture count) |
| **R** | Reset captures and return to preview |
| **Q** | Cancel auto-capture, return to preview |

### Intrinsic Mode — Done

| Key | Action |
|-----|--------|
| **U** | Toggle undistorted preview |
| **S** | Save camera matrix + distortion to config |
| **Q** | Quit / advance to next mode |

### Extrinsic Mode — Preview

| Key | Action |
|-----|--------|
| **ENTER** | Start auto-capture (captures 6 samples, auto-finalizes) |
| **SPACE** | Manual single capture |
| **Q** | Quit |

### Extrinsic Mode — Done

| Key | Action |
|-----|--------|
| **S** | Save focal length + angles to config |
| **Q** | Quit |

## Command-Line Reference

```
usage: pitrac-cal [-h] [--camera {1,2}] [--mode {intrinsic,extrinsic,full}]
                  [--config CONFIG] [--image-dir IMAGE_DIR]
                  [--generate-board OUTPUT.png] [--no-strobe]
                  [--strobe-output STROBE_OUTPUT]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--camera` | `1` | Camera number (1 or 2) |
| `--mode` | `full` | Calibration mode: `intrinsic`, `extrinsic`, or `full` |
| `--config` | auto | Path to `golf_sim_config.json` (auto-detected from `PITRAC_ROOT`) |
| `--image-dir` | none | Load images from a directory instead of live camera |
| `--generate-board` | none | Generate a printable CharucoBoard PNG and exit |
| `--no-strobe` | `false` | Disable default strobed still capture for camera 2 live extrinsic mode |
| `--strobe-output` | `~/LM_Shares/PiTracLogs/log_cam2_cal_YYYYMMDD_HHMMSS_xxx.png` | Output file template for strobed extrinsic captures (timestamp/index appended per capture) |

## How It Works

### Intrinsic Calibration

Uses OpenCV's CharucoBoard detection and `cv2.calibrateCamera()`:

1. A CharucoBoard combines a chessboard pattern with ArUco markers, allowing partial detection when the board is partially occluded or at extreme angles.
2. For each captured frame, the tool extracts Charuco corner positions (sub-pixel accurate) using `CharucoDetector.detectBoard()`.
3. Corner/ID pairs are converted to object/image point correspondences via `CharucoBoard.matchImagePoints()`.
4. After collecting enough frames, `cv2.calibrateCamera()` solves for the 3x3 camera matrix (focal lengths `fx`, `fy` and principal point `cx`, `cy`) and the 5-coefficient distortion vector.
5. The RMS reprojection error measures how well the solved parameters explain the observed corner positions.

### Extrinsic Calibration

Ports the focal length and angle formulas directly from the C++ codebase:

**Focal length** (from `gs_camera.cpp:909-913`):
```
focal_length_mm = ball_distance_m * sensor_width_mm
                  * (2 * ball_radius_px / resolution_x)
                  / (2 * ball_radius_m)
```

**Camera angles** (from `gs_calibration.cpp:163-278`):
1. Compute the pixel offset of the detected ball from the image center.
2. Convert pixel offset to meters using the focal length and sensor dimensions.
3. Compute the angle of the ball from the camera bore axis.
4. Compute the angle of the ball from the launch monitor perspective (based on known 3D position).
5. The camera angles are the difference between these two angles.

### Config File Format

PiTrac's C++ code uses Boost.property_tree, which stores all JSON values as strings. `pitrac-cal` preserves this convention so the C++ side can read values written by Python.

Keys written to `gs_config.cameras`:

| Key | Format | Source |
|-----|--------|--------|
| `kCamera{N}CalibrationMatrix` | `[[str, str, str], ...]` (3x3) | Intrinsic |
| `kCamera{N}DistortionVector` | `[str, str, str, str, str]` | Intrinsic |
| `kCamera{N}FocalLength` | `str` | Extrinsic |
| `kCamera{N}Angles` | `[str, str]` (yaw, pitch in degrees) | Extrinsic |

A timestamped backup is created before every write: `golf_sim_config.json_BACKUP_<timestamp>.json`.

## Environment Variables

The tool reads the same environment variables as the C++ `pitrac_lm`:

| Variable | Default | Purpose |
|----------|---------|---------|
| `PITRAC_ROOT` | (auto-detect) | Repository root, used to find `golf_sim_config.json` |
| `PITRAC_SLOT{N}_CAMERA_TYPE` | `4` | Camera model: `4` = PiGS (color), `5` = InnoMaker (mono) |
| `PITRAC_SLOT{N}_CAMERA_ORIENTATION` | `1` | Orientation: `1` = upright, `2` = upside-down (180° flip) |

Camera type determines the sensor format (`SBGGR10_1X10` for color, `Y10_1X10` for mono) and tuning file (`imx296.json` vs `imx296_mono.json`). If your camera 2 is an InnoMaker mono sensor, set:

```bash
export PITRAC_SLOT2_CAMERA_TYPE=5
```

## Off-Pi Development and Testing

For development on a desktop machine (Ubuntu, macOS, etc.) without Pi hardware, use `--image-dir` to load images from a directory instead of the live camera:

```bash
python3 -m pitrac_cal --camera 1 --mode extrinsic --image-dir ~/test_images/
```

The tool cycles through image files in the directory, simulating a camera feed. This is useful for testing the calibration algorithms without Pi hardware.

### Running Tests

From the repository root:

```bash
# On Pi OS — install pytest via apt
sudo apt install -y python3-pytest

# On a dev machine — install via pip (or pip inside a venv)
pip install pytest numpy opencv-python-headless

# Run the tests
python3 -m pytest pitrac_cal/tests/ -v
```

Tests cover:
- **`test_config.py`** — Config round-trip read/write, string precision, ball position resolution, backup creation
- **`test_extrinsic.py`** — Focal length formula parity with C++, distance conversions, camera angle computation, ball detection on synthetic images
- **`test_intrinsic.py`** — CharucoBoard creation, corner detection on synthetic images, calibration with synthetic views

## Troubleshooting

### `error: externally-managed-environment` or `pip install` fails

Do not use `pip`. Raspberry Pi OS manages Python system-wide. Install everything through `apt`:

```bash
sudo apt install -y python3-numpy python3-opencv
```

### `ModuleNotFoundError: No module named 'cv2'`

```bash
sudo apt install -y python3-opencv
```

If you want the source-built version instead, rebuild OpenCV (new builds include Python bindings by default):

```bash
FORCE=1 pitrac-cli install opencv --yes
```

### `ModuleNotFoundError: No module named 'pitrac_cal'`

Run from the repository root so Python can find the package:

```bash
cd /path/to/pitrac-light
python3 -m pitrac_cal --camera 1 --mode full
```

Or use `pitrac-cli run calibrate-gui` which sets the working directory automatically.

### `RuntimeError: rpicam-still not found`

You are either on a non-Pi machine or rpicam-apps is not installed. On a dev machine, use `--image-dir` to load images from a directory. On the Pi:

```bash
pitrac-cli install camera --yes
```

### Camera starts but image has green tint

The camera is being configured as mono (`Y10_1X10`) but is actually a color sensor. Set the camera type environment variable:

```bash
# Camera 1 is the standard RPi Global Shutter Camera (color, model 4)
export PITRAC_SLOT1_CAMERA_TYPE=4

# Camera 2 is InnoMaker mono (model 5)
export PITRAC_SLOT2_CAMERA_TYPE=5
```

Camera model values: `1`=PiCam1.3, `2`=PiCam2, `3`=PiHQ, `4`=PiGS (color), `5`=InnoMaker mono.

### Camera fails with "Selected sensor format: 96x88"

The IMX296 sensor needs explicit `media-ctl` configuration before capture. The tool does this automatically by discovering the sensor's media device and running `media-ctl --set-v4l2`. If this fails:

1. Verify `media-ctl` is installed: `which media-ctl`
2. Check the sensor is detected: `media-ctl -d /dev/media0 --print-dot | grep imx296`
3. Try manual configuration:
   ```bash
   # Find which /dev/mediaX has the IMX296
   for m in 0 1 2 3 4 5; do
     media-ctl -d /dev/media$m --print-dot 2>/dev/null | grep -q imx296 && echo "media$m"
   done

   # Configure it (adjust media number and device number)
   media-ctl -d /dev/media0 --set-v4l2 "'imx296 4-001a':0 [fmt:SBGGR10_1X10/1456x1088 crop:(0,0)/1456x1088]"
   ```

### No window appears / `cv2.error: ... cannot open display`

The tool requires a display. If connected via SSH, use VNC instead, or set the display:

```bash
export DISPLAY=:0.0
python3 -m pitrac_cal --camera 1 --mode full
```

When launched via `pitrac-cli run calibrate-gui`, `DISPLAY=:0.0` is set automatically.

### Ball not detected in extrinsic mode

- Ensure the ball is well-lit with even lighting (no harsh shadows)
- Check that the ball is at the configured calibration position
- Verify the ball is in focus and fully visible in the frame
- The detection uses HoughCircles — a white ball on a contrasting background works best

### Focal length result seems wrong

Valid focal lengths for PiTrac cameras are typically 2-50mm. If results fall outside this range:

- Verify the ball is at the exact calibration position specified in the config
- Check `kCalibrationRigType` in `golf_sim_config.json` matches your rig
- Ensure `kAutoCalibrationBaselineBallPositionFrom*` values are correct for your setup

### Saved values not read by C++ code

Ensure you are editing the same `golf_sim_config.json` that `pitrac_lm` reads. By default:
- `pitrac-cal` reads from `~/.pitrac/config/golf_sim_config.json` (or explicit `--config`)
- `pitrac_lm` reads the config specified by `--config_file` (CLI uses `~/.pitrac/config/golf_sim_config.json`)

Initialize runtime config once on a fresh install:

```bash
pitrac-cli config init
```
