"""Camera source abstraction: picamera2 live feed or directory of images.

Provides a common interface so the calibration workflow works identically
whether running on a Pi with a live camera or on a dev machine with saved images.
"""

from __future__ import annotations

import logging
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


class PiCamera2Source:
    """Live capture via picamera2 (Pi only)."""

    def __init__(
        self,
        camera_num: int = 0,
        resolution: tuple[int, int] = (constants.RESOLUTION_X, constants.RESOLUTION_Y),
        flip: bool = True,
    ):
        try:
            from picamera2 import Picamera2  # type: ignore[import-not-found]
        except ImportError:
            raise RuntimeError(
                "picamera2 is not available. Use --image-dir for off-Pi testing."
            )

        self._flip = flip
        self._cam = Picamera2(camera_num)
        config = self._cam.create_still_configuration(
            main={"size": resolution, "format": "BGR888"},
        )
        self._cam.configure(config)
        self._cam.start()
        logger.info("PiCamera2 started (camera %d, %dx%d)", camera_num, *resolution)

    def capture(self) -> np.ndarray:
        frame = self._cam.capture_array()
        if self._flip:
            frame = cv2.flip(frame, -1)
        return frame

    def release(self) -> None:
        self._cam.stop()
        self._cam.close()
        logger.info("PiCamera2 released")


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


def create_source(
    camera_num: int = 1,
    image_dir: Path | None = None,
) -> CameraSource:
    """Factory: return a DirectorySource if *image_dir* given, else PiCamera2Source."""
    if image_dir is not None:
        return DirectorySource(image_dir)
    # picamera2 uses 0-based camera index
    return PiCamera2Source(camera_num=camera_num - 1)
