"""Camera source abstraction: rpicam-still capture or directory of images.

Provides a common interface so the calibration workflow works identically
whether running on a Pi with a live camera or on a dev machine with saved images.

On the Pi, frames are captured via rpicam-still (the same libcamera pipeline
that the C++ pitrac_lm uses).  picamera2 has sensor-mode selection issues
with the IMX296 global shutter camera, so we avoid it entirely.

Before capturing, the IMX296 sensor format must be explicitly configured via
media-ctl (the same step that the C++ pitrac_lm does in
libcamera_interface.cpp:ConfigCameraForFullScreenWatching).  Without this,
the sensor may default to an unusable 96x88 mode.
"""

from __future__ import annotations

import logging
import os
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Protocol

import cv2
import numpy as np

from . import constants

logger = logging.getLogger(__name__)


class CameraSource(Protocol):
    """Minimal interface for frame capture."""

    def capture(self) -> np.ndarray:
        """Return the next BGR frame."""
        ...

    def release(self) -> None:
        """Release hardware resources."""
        ...


def _discover_imx296_media(camera_index: int) -> tuple[int, int] | None:
    """Discover the /dev/media device and i2c address for an IMX296 sensor.

    Ports the DiscoverCameraLocation() logic from
    libcamera_interface.cpp:501-620.

    Returns (media_number, device_number) or None if not found.
    The *camera_index* is the 0-based rpicam index.
    """
    results: list[tuple[int, int]] = []

    for m in range(6):
        media_dev = f"/dev/media{m}"
        if not Path(media_dev).exists():
            continue
        try:
            proc = subprocess.run(
                ["media-ctl", "-d", media_dev, "--print-dot"],
                capture_output=True, text=True, timeout=5,
            )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if proc.returncode != 0:
            continue
        # Look for lines containing 'imx296' and extract the i2c device number
        for line in proc.stdout.splitlines():
            if "imx296" not in line.lower():
                continue
            # Pattern: "imx296 D-001a" where D is the device number
            match = re.search(r"imx296\s+(\d+)-001a", line, re.IGNORECASE)
            if match:
                device_num = int(match.group(1))
                results.append((m, device_num))
                break

    if not results:
        return None

    # If there are multiple IMX296 sensors, pick by index
    if camera_index < len(results):
        return results[camera_index]
    return results[0]


def _configure_imx296_sensor(
    media_num: int,
    device_num: int,
    width: int = constants.RESOLUTION_X,
    height: int = constants.RESOLUTION_Y,
    mono: bool = True,
) -> bool:
    """Configure the IMX296 sensor format via media-ctl.

    Ports ConfigCameraForFullScreenWatching() and
    GetCmdLineForMediaCtlCropping() from libcamera_interface.cpp:842-1003.

    This MUST be called before rpicam-still can capture at full resolution.
    Without it the sensor may default to a tiny 96x88 mode.
    """
    fmt = "Y10_1X10" if mono else "SBGGR10_1X10"
    entity = f"'imx296 {device_num}-001a'"
    v4l2_str = (
        f"{entity}:0 "
        f"[fmt:{fmt}/{width}x{height} crop:(0,0)/{width}x{height}]"
    )
    cmd = [
        "media-ctl",
        "-d", f"/dev/media{media_num}",
        "--set-v4l2", v4l2_str,
    ]
    logger.info("Configuring sensor: %s", " ".join(cmd))
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        logger.warning(
            "media-ctl failed (exit %d): %s", result.returncode, result.stderr.strip()
        )
        # Try color format as fallback
        if mono:
            logger.info("Retrying with color (SBGGR10_1X10) format")
            return _configure_imx296_sensor(
                media_num, device_num, width, height, mono=False
            )
        return False
    logger.info("Sensor configured to %dx%d (%s)", width, height, fmt)
    return True


def _is_camera_mono(camera_num: int) -> bool:
    """Check if a camera slot uses a mono sensor.

    Reads PITRAC_SLOT{N}_CAMERA_TYPE (same env var as the C++ code in
    gs_config.cpp:226-255).  Camera model enum from camera_hardware.h:

        1 = PiCam13, 2 = PiCam2, 3 = PiHQ,
        4 = PiGS (color), 5 = InnoMakerIMX296GS_Mono

    Only model 5 is mono.  Default is 4 (PiGS color).
    """
    env_var = f"PITRAC_SLOT{camera_num}_CAMERA_TYPE"
    val = os.environ.get(env_var, "4")
    is_mono = val.strip() == "5"
    logger.info("%s=%s → mono=%s", env_var, val, is_mono)
    return is_mono


def _setup_tuning_file(camera_num: int, is_mono: bool) -> None:
    """Set LIBCAMERA_RPI_TUNING_FILE env var for the IMX296 sensor.

    Ports SetLibcameraTuningFileEnvVariable() from
    libcamera_interface.cpp:1007-1086.

    Pi 5 uses /usr/share/libcamera/ipa/rpi/pisp/,
    Pi 4 uses /usr/share/libcamera/ipa/rpi/vc4/.
    """
    # Detect Pi model from /proc/device-tree/model
    pi5 = False
    model_path = Path("/proc/device-tree/model")
    if model_path.exists():
        try:
            model_str = model_path.read_text(errors="replace")
            pi5 = "Pi 5" in model_str
        except OSError:
            pass

    base = "/usr/share/libcamera/ipa/rpi"
    subdir = "pisp" if pi5 else "vc4"

    if is_mono:
        candidates = [
            f"{base}/{subdir}/imx296_mono.json",
            f"{base}/{subdir}/imx296.json",
        ]
    elif camera_num == 1:
        candidates = [
            f"{base}/{subdir}/imx296.json",
            f"{base}/{subdir}/imx296_noir.json",
        ]
    else:
        candidates = [
            f"{base}/{subdir}/imx296_noir.json",
            f"{base}/{subdir}/imx296.json",
            f"{base}/{subdir}/imx296_mono.json",
        ]

    tuning_file = ""
    for c in candidates:
        if Path(c).exists():
            tuning_file = c
            break

    if not tuning_file:
        tuning_file = candidates[0] if candidates else ""
        logger.warning("No preferred tuning file found, trying fallback: %s", tuning_file)
    else:
        logger.info("Using tuning file: %s", tuning_file)

    if tuning_file:
        os.environ["LIBCAMERA_RPI_TUNING_FILE"] = tuning_file


class RpicamSource:
    """Capture frames via rpicam-still (Pi only).

    Uses the same libcamera/rpicam-apps pipeline as the C++ pitrac_lm binary,
    which avoids the picamera2 sensor-mode bug on the IMX296.

    Before the first capture, configures the IMX296 sensor format via
    media-ctl (matching the C++ ConfigCameraForFullScreenWatching flow).
    """

    def __init__(
        self,
        camera_num: int = 0,
        flip: bool = True,
        pitrac_camera_num: int = 1,
    ):
        self._camera_num = camera_num
        self._flip = flip
        self._tmp = Path(tempfile.mkdtemp()) / "pitrac_cal_frame.png"

        # Verify rpicam-still is available
        try:
            subprocess.run(
                ["rpicam-still", "--version"],
                capture_output=True, check=True,
            )
        except FileNotFoundError:
            raise RuntimeError(
                "rpicam-still not found. Install rpicam-apps or use --image-dir."
            )

        # Configure the IMX296 sensor via media-ctl before first capture
        # (matches C++ ConfigCameraForFullScreenWatching)
        media_info = _discover_imx296_media(camera_num)
        if media_info is not None:
            media_num, device_num = media_info
            logger.info(
                "Found IMX296 at /dev/media%d, device %d-001a",
                media_num, device_num,
            )
            is_mono = _is_camera_mono(pitrac_camera_num)
            _setup_tuning_file(pitrac_camera_num, is_mono)
            _configure_imx296_sensor(media_num, device_num, mono=is_mono)
        else:
            logger.info(
                "No IMX296 sensor found via media-ctl for camera %d "
                "(may be a non-IMX296 camera, or media-ctl not available)",
                camera_num,
            )

        # Take a test capture to confirm the camera works
        self._run_capture()
        test = cv2.imread(str(self._tmp))
        if test is None:
            raise RuntimeError("rpicam-still produced no output — check camera connection")
        logger.info(
            "RpicamSource ready (camera %d, %dx%d)",
            camera_num, test.shape[1], test.shape[0],
        )

    def _run_capture(self) -> None:
        """Run rpicam-still to capture a single frame."""
        cmd = [
            "rpicam-still",
            "--camera", str(self._camera_num),
            "--width", str(constants.RESOLUTION_X),
            "--height", str(constants.RESOLUTION_Y),
            "-o", str(self._tmp),
            "--immediate",
            "--nopreview",
            "-n",
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(
                f"rpicam-still failed (exit {result.returncode}): {result.stderr.strip()}"
            )

    def capture(self) -> np.ndarray:
        self._run_capture()
        frame = cv2.imread(str(self._tmp))
        if frame is None:
            raise RuntimeError("Failed to read captured frame")
        if self._flip:
            frame = cv2.flip(frame, -1)
        return frame

    def release(self) -> None:
        if self._tmp.exists():
            self._tmp.unlink()
        logger.info("RpicamSource released")


class DirectorySource:
    """Load images from a directory, cycling through them on each capture().

    Useful for off-Pi development and testing.
    """

    def __init__(self, directory: Path):
        self._dir = directory
        extensions = {".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff"}
        self._files = sorted(
            p for p in directory.iterdir()
            if p.suffix.lower() in extensions
        )
        if not self._files:
            raise FileNotFoundError(f"No image files found in {directory}")
        self._index = 0
        logger.info("DirectorySource: %d images from %s", len(self._files), directory)

    def capture(self) -> np.ndarray:
        path = self._files[self._index]
        self._index = (self._index + 1) % len(self._files)
        frame = cv2.imread(str(path))
        if frame is None:
            raise RuntimeError(f"Failed to read image: {path}")
        return frame

    def release(self) -> None:
        pass

    @property
    def current_file(self) -> Path:
        """Path of the most-recently-returned image."""
        idx = (self._index - 1) % len(self._files)
        return self._files[idx]


def _is_upside_down(camera_num: int) -> bool:
    """Check if a camera is mounted upside-down.

    Reads the same PITRAC_SLOT{N}_CAMERA_ORIENTATION env vars as the C++
    code (gs_config.cpp:286-322).  Values: 1 = upside-up, 2 = upside-down.
    Default is upside-up (no flip).
    """
    env_var = f"PITRAC_SLOT{camera_num}_CAMERA_ORIENTATION"
    val = os.environ.get(env_var, "1")
    is_flipped = val == "2"
    logger.info("%s=%s → flip=%s", env_var, val, is_flipped)
    return is_flipped


def create_source(
    camera_num: int = 1,
    image_dir: Path | None = None,
) -> CameraSource:
    """Factory: return a DirectorySource if *image_dir* given, else RpicamSource.

    rpicam-still uses 0-based camera indices.  Camera 1 in PiTrac maps to
    index 0 on a two-Pi system (each Pi has one camera) and also on a
    single-Pi system (camera 1 is at slot 0, camera 2 at slot 1).
    See libcamera_interface.cpp:888 / 1178-1180.
    """
    if image_dir is not None:
        return DirectorySource(image_dir)
    rpicam_index = camera_num - 1
    flip = _is_upside_down(camera_num)
    logger.info("Mapping PiTrac camera %d → rpicam-still --camera %d", camera_num, rpicam_index)
    return RpicamSource(
        camera_num=rpicam_index,
        flip=flip,
        pitrac_camera_num=camera_num,
    )
