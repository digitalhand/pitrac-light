"""Camera source abstraction: rpicam-still capture or directory of images.

Provides a common interface so the calibration workflow works identically
whether running on a Pi with a live camera or on a dev machine with saved images.

On the Pi, frames are captured via rpicam-still (the same libcamera pipeline
that the C++ pitrac_lm uses).  picamera2 has sensor-mode selection issues
with the IMX296 global shutter camera, so we avoid it entirely.
"""

from __future__ import annotations

import logging
import os
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


class RpicamSource:
    """Capture frames via rpicam-still (Pi only).

    Uses the same libcamera/rpicam-apps pipeline as the C++ pitrac_lm binary,
    which avoids the picamera2 sensor-mode bug on the IMX296.
    """

    def __init__(
        self,
        camera_num: int = 0,
        flip: bool = True,
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
    return RpicamSource(camera_num=rpicam_index, flip=flip)
