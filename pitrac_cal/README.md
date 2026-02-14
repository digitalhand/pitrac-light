# PiTrac Calibration Tool (`pitrac-cal`)

A Python-based camera calibration GUI for PiTrac that performs both intrinsic (lens) and extrinsic (position/angle) calibration with live visual feedback, then writes results directly to `golf_sim_config.json`.

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Usage](#usage)
  - [Launch via pitrac-cli](#launch-via-pitrac-cli)
  - [Launch directly](#launch-directly)
  - [Generate a CharucoBoard](#generate-a-charucoboard)
- [Calibration Modes](#calibration-modes)
  - [Intrinsic Calibration (CharucoBoard)](#intrinsic-calibration-charucoboard)
  - [Extrinsic Calibration (Golf Ball)](#extrinsic-calibration-golf-ball)
  - [Full Calibration](#full-calibration)
- [Keyboard Controls](#keyboard-controls)
- [Command-Line Reference](#command-line-reference)
- [How It Works](#how-it-works)
  - [Intrinsic Calibration](#intrinsic-calibration)
  - [Extrinsic Calibration](#extrinsic-calibration)
  - [Config File Format](#config-file-format)
- [Off-Pi Development and Testing](#off-pi-development-and-testing)
- [Module Reference](#module-reference)
- [Troubleshooting](#troubleshooting)

## Overview

PiTrac's camera calibration has two parts:

1. **Intrinsic calibration** determines the camera matrix and distortion coefficients for the lens. These correct for barrel/pincushion distortion and are specific to each physical camera+lens combination.

2. **Extrinsic calibration** determines the focal length and camera mounting angles (yaw, pitch). These change whenever a camera is moved or repositioned in the enclosure.

Previously, intrinsic parameters were computed manually and hardcoded, while extrinsic calibration ran headless through `pitrac_lm` with no visual feedback. `pitrac-cal` replaces both workflows with a single interactive tool that shows exactly what the camera sees and what the algorithms are detecting.

## Prerequisites

**Hardware:**
- Raspberry Pi 4 or 5 running Raspberry Pi OS (64-bit)
- PiTrac camera rig with IMX296 global shutter camera(s) connected
- A printed CharucoBoard (for intrinsic calibration — the tool can generate one)
- A golf ball on the calibration rig (for extrinsic calibration)

**Software:**
- Python 3.9+ (included in Raspberry Pi OS)
- OpenCV 4.8+ with Python bindings
- picamera2 (included in Raspberry Pi OS)
- numpy

**Display:**
- A monitor connected to the Pi, or VNC access (the tool uses OpenCV `highgui` windows)

## Installation

From the PiTrac repository root on the Raspberry Pi:

```bash
pip install -r pitrac_cal/requirements.txt
```

If you built OpenCV from source during PiTrac installation (via `pitrac-cli install opencv`), the Python bindings are already available system-wide. Verify with:

```bash
python3 -c "import cv2; print(cv2.__version__)"
```

If OpenCV was built without Python bindings, rebuild with `OPENCV_ENABLE_PYTHON=1`:

```bash
OPENCV_ENABLE_PYTHON=1 pitrac-cli install opencv --yes
```

Verify picamera2 is available (it ships with Raspberry Pi OS):

```bash
python3 -c "from picamera2 import Picamera2; print('picamera2 OK')"
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

Before intrinsic calibration, generate a board image to print:

```bash
python3 -m pitrac_cal --generate-board charuco_board.png
```

Print the resulting image at actual size on A4/Letter paper. The default board is 7x5 squares (30mm squares, 22.5mm markers). Ensure your printer does not scale the image — measure a square with a ruler to verify it is 30mm.

## Calibration Modes

### Intrinsic Calibration (CharucoBoard)

Determines the camera matrix and lens distortion coefficients using a printed CharucoBoard pattern.

**What you need:** A printed CharucoBoard (generate one with `--generate-board`).

**Procedure:**

1. Launch in intrinsic mode:
   ```bash
   pitrac-cli run calibrate-gui --camera 1 --mode intrinsic
   ```
2. Hold the printed CharucoBoard in front of the camera. When corners are detected, they appear as green overlays on the live preview.
3. Press **SPACE** to capture a frame (only works when corners are detected).
4. Move the board to a different position and angle. Capture again. Repeat 15-20 times, covering:
   - Different distances (close, medium, far)
   - Different angles (tilted left, right, up, down)
   - Different positions in the frame (center, edges, corners)
5. Press **ENTER** to run the calibration. The RMS reprojection error is displayed — values below 1.0 pixel indicate a good calibration.
6. Press **U** to toggle undistorted preview and compare before/after.
7. Press **S** to save the camera matrix and distortion vector to `golf_sim_config.json`.
8. Press **Q** to exit (or continue to extrinsic if using `--mode full`).

**Tips for good intrinsic calibration:**
- Use at least 15 captures from varied positions and angles
- Cover the entire field of view, including edges and corners
- Keep the board flat — don't bend the paper
- Ensure even lighting with no strong reflections on the board
- Target RMS error below 0.5 for best results

### Extrinsic Calibration (Golf Ball)

Determines the focal length and camera mounting angles using a golf ball placed at a known position.

**What you need:** A golf ball placed at the calibration position on your rig.

**Procedure:**

1. Place a golf ball at the calibration position defined in your config (the tool reads the position from `golf_sim_config.json` based on the rig type and camera number).
2. Launch in extrinsic mode:
   ```bash
   pitrac-cli run calibrate-gui --camera 1 --mode extrinsic
   ```
3. The live preview shows ball detection results: a green circle around the detected ball, a crosshair at the center, and the measured pixel radius.
4. Press **SPACE** to capture a focal length sample. The computed focal length and running average are displayed.
5. Repeat 5-6 times. Small variations between samples are normal — the average smooths them out.
6. Press **ENTER** to finalize. The tool computes camera angles from the last captured frame.
7. Press **S** to save focal length and camera angles to `golf_sim_config.json`.
8. Press **Q** to exit.

**Tips for good extrinsic calibration:**
- Ensure the ball is exactly at the configured calibration position
- Use consistent, even lighting — shadows on the ball affect the detected radius
- Verify the detected circle matches the ball outline before capturing
- Discard outlier samples by pressing **R** to reset and start over

### Full Calibration

Runs intrinsic first, then extrinsic in sequence:

```bash
pitrac-cli run calibrate-gui --camera 1 --mode full
```

Press **Q** at the end of intrinsic mode to advance to extrinsic mode.

## Keyboard Controls

### Intrinsic Mode

| Key | Action |
|-----|--------|
| **SPACE** | Capture frame (when corners detected) |
| **ENTER** | Run calibration from collected captures |
| **U** | Toggle undistorted preview (after calibration) |
| **S** | Save camera matrix + distortion to config |
| **G** | Generate CharucoBoard image file |
| **R** | Reset all captures, start over |
| **Q** | Quit / advance to next mode |

### Extrinsic Mode

| Key | Action |
|-----|--------|
| **SPACE** | Capture focal length sample (when ball detected) |
| **ENTER** | Finalize calibration (average focal length + compute angles) |
| **S** | Save focal length + angles to config |
| **Q** | Quit |

## Command-Line Reference

```
usage: pitrac-cal [-h] [--camera {1,2}] [--mode {intrinsic,extrinsic,full}]
                  [--config CONFIG] [--image-dir IMAGE_DIR]
                  [--generate-board OUTPUT.png]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--camera` | `1` | Camera number (1 or 2) |
| `--mode` | `full` | Calibration mode: `intrinsic`, `extrinsic`, or `full` |
| `--config` | auto | Path to `golf_sim_config.json` (auto-detected from `PITRAC_ROOT`) |
| `--image-dir` | none | Load images from a directory instead of live camera |
| `--generate-board` | none | Generate a printable CharucoBoard PNG and exit |

## How It Works

### Intrinsic Calibration

Uses OpenCV's CharucoBoard detection and `cv2.calibrateCamera()`:

1. A CharucoBoard combines a chessboard pattern with ArUco markers, allowing partial detection when the board is partially occluded or at extreme angles.
2. For each captured frame, the tool extracts Charuco corner positions (sub-pixel accurate).
3. After collecting enough frames, `cv2.calibrateCamera()` solves for the 3x3 camera matrix (focal lengths `fx`, `fy` and principal point `cx`, `cy`) and the 5-coefficient distortion vector.
4. The RMS reprojection error measures how well the solved parameters explain the observed corner positions.

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

## Off-Pi Development and Testing

For development on a desktop machine (Ubuntu, macOS, etc.) without Pi hardware, use `--image-dir` to load images from a directory instead of the live camera:

```bash
python3 -m pitrac_cal --camera 1 --mode extrinsic --image-dir ~/test_images/
```

The tool cycles through image files in the directory, simulating a camera feed. This is useful for testing the calibration algorithms without Pi hardware.

### Running Tests

From the repository root:

```bash
pip install pytest numpy opencv-python-headless
python3 -m pytest pitrac_cal/tests/ -v
```

Tests cover:
- **`test_config.py`** — Config round-trip read/write, string precision, ball position resolution, backup creation
- **`test_extrinsic.py`** — Focal length formula parity with C++, distance conversions, camera angle computation, ball detection on synthetic images
- **`test_intrinsic.py`** — CharucoBoard creation, corner detection on synthetic images, calibration with synthetic views

## Module Reference

| File | Purpose |
|------|---------|
| `__main__.py` | Entry point, keyboard-driven state machine |
| `cli.py` | Argument parsing |
| `camera.py` | picamera2 live capture + directory fallback |
| `intrinsic.py` | CharucoBoard detection + `cv2.calibrateCamera` |
| `extrinsic.py` | Ball detection + focal length + angle computation |
| `config_manager.py` | Read/write `golf_sim_config.json` with backup |
| `display.py` | OpenCV window + HUD overlay helpers |
| `constants.py` | Hardware constants (sensor dimensions, ball radius) |

## Troubleshooting

### `ModuleNotFoundError: No module named 'pitrac_cal'`

Run from the repository root so Python can find the package:

```bash
cd /path/to/pitrac-light
python3 -m pitrac_cal --camera 1 --mode full
```

Or use `pitrac-cli run calibrate-gui` which sets the working directory automatically.

### `RuntimeError: picamera2 is not available`

You are running on a non-Pi machine. Use `--image-dir` to load images from a directory, or install picamera2 on the Pi:

```bash
sudo apt install -y python3-picamera2
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
- Try more samples (6+) to reduce the effect of individual detection errors

### Saved values not read by C++ code

Ensure you are editing the same `golf_sim_config.json` that `pitrac_lm` reads. By default:
- `pitrac-cal` reads from `$PITRAC_ROOT/src/golf_sim_config.json`
- `pitrac_lm` reads the config specified by `--config_file` or `~/.pitrac/config/golf_sim_config.json`

After calibrating, copy the config if needed:

```bash
pitrac-cli config init --force
```
