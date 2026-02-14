"""Camera source abstraction: rpicam-vid streaming or directory of images.

Provides a common interface so the calibration workflow works identically
whether running on a Pi with a live camera or on a dev machine with saved images.

On the Pi, frames are captured via rpicam-vid MJPEG streaming (the same
libcamera pipeline that the C++ pitrac_lm uses).  This is much faster than
per-frame rpicam-still calls since the camera stays open and streams
continuously.

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

# Size of chunks read from the rpicam-vid MJPEG pipe
_PIPE_READ_SIZE = 65536


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

    This MUST be called before rpicam-vid can capture at full resolution.
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


def _setup_imx296(camera_num: int, pitrac_camera_num: int) -> None:
    """Discover and configure IMX296 sensor (media-ctl + tuning file)."""
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


class RpicamSource:
    """Capture frames via rpicam-vid MJPEG streaming (Pi only).

    Uses the same libcamera/rpicam-apps pipeline as the C++ pitrac_lm binary.
    Streams MJPEG to a pipe for fast continuous capture instead of launching
    rpicam-still per frame.

    Before streaming, configures the IMX296 sensor format via media-ctl
    (matching the C++ ConfigCameraForFullScreenWatching flow).
    """

    def __init__(
        self,
        camera_num: int = 0,
        flip: bool = True,
        pitrac_camera_num: int = 1,
    ):
        self._camera_num = camera_num
        self._flip = flip
        self._proc: subprocess.Popen | None = None
        self._buffer = b""

        # Verify rpicam-vid is available
        try:
            subprocess.run(
                ["rpicam-vid", "--version"],
                capture_output=True, check=True,
            )
        except FileNotFoundError:
            raise RuntimeError(
                "rpicam-vid not found. Install rpicam-apps or use --image-dir."
            )

        # Configure the IMX296 sensor via media-ctl before streaming
        _setup_imx296(camera_num, pitrac_camera_num)

        # Start rpicam-vid streaming MJPEG to stdout
        self._start_stream()

        # Read a test frame to confirm streaming works
        test = self.capture()
        logger.info(
            "RpicamSource ready (camera %d, %dx%d, streaming)",
            camera_num, test.shape[1], test.shape[0],
        )

    def _start_stream(self) -> None:
        """Start rpicam-vid as a background process streaming MJPEG to stdout."""
        cmd = [
            "rpicam-vid",
            "--camera", str(self._camera_num),
            "--width", str(constants.RESOLUTION_X),
            "--height", str(constants.RESOLUTION_Y),
            "--codec", "mjpeg",
            "--framerate", "10",
            "--nopreview",
            "-n",
            "-t", "0",
            "-o", "-",
        ]
        logger.info("Starting stream: %s", " ".join(cmd))
        self._proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._buffer = b""

    def capture(self) -> np.ndarray:
        """Read the next MJPEG frame from the rpicam-vid pipe."""
        if self._proc is None or self._proc.poll() is not None:
            raise RuntimeError("rpicam-vid stream not running")

        while True:
            chunk = self._proc.stdout.read(_PIPE_READ_SIZE)
            if not chunk:
                raise RuntimeError(
                    "rpicam-vid stream ended unexpectedly"
                )
            self._buffer += chunk

            # Find a complete JPEG: starts with FF D8, ends with FF D9
            start = self._buffer.find(b"\xff\xd8")
            if start == -1:
                # No JPEG start yet, discard buffer up to last byte
                self._buffer = self._buffer[-1:]
                continue

            end = self._buffer.find(b"\xff\xd9", start + 2)
            if end == -1:
                # Have start but no end yet — keep reading
                continue

            # Extract complete JPEG and advance buffer
            jpeg_data = self._buffer[start : end + 2]
            self._buffer = self._buffer[end + 2 :]

            frame = cv2.imdecode(
                np.frombuffer(jpeg_data, dtype=np.uint8),
                cv2.IMREAD_COLOR,
            )
            if frame is None:
                # Corrupted JPEG, skip and try next
                continue

            if self._flip:
                frame = cv2.flip(frame, -1)
            return frame

    def release(self) -> None:
        if self._proc is not None:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait()
            self._proc = None
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

    rpicam-vid uses 0-based camera indices.  Camera 1 in PiTrac maps to
    index 0 on a two-Pi system (each Pi has one camera) and also on a
    single-Pi system (camera 1 is at slot 0, camera 2 at slot 1).
    See libcamera_interface.cpp:888 / 1178-1180.
    """
    if image_dir is not None:
        return DirectorySource(image_dir)
    rpicam_index = camera_num - 1
    flip = _is_upside_down(camera_num)
    logger.info("Mapping PiTrac camera %d → rpicam-vid --camera %d", camera_num, rpicam_index)
    return RpicamSource(
        camera_num=rpicam_index,
        flip=flip,
        pitrac_camera_num=camera_num,
    )
