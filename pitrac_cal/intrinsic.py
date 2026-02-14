"""Intrinsic calibration using CharucoBoard detection + cv2.calibrateCamera.

Provides CharucoBoard creation, corner detection across multiple captures,
and intrinsic calibration (camera matrix + distortion coefficients).
"""

from __future__ import annotations

import logging
from dataclasses import dataclass, field
from pathlib import Path

import cv2
import numpy as np

from . import constants

logger = logging.getLogger(__name__)


@dataclass
class IntrinsicResult:
    """Outcome of intrinsic calibration."""

    camera_matrix: np.ndarray
    dist_coeffs: np.ndarray
    rms_error: float
    rvecs: list[np.ndarray] = field(default_factory=list)
    tvecs: list[np.ndarray] = field(default_factory=list)


def create_board(
    cols: int = constants.CHARUCO_COLS,
    rows: int = constants.CHARUCO_ROWS,
    square_len: float = constants.CHARUCO_SQUARE_MM,
    marker_len: float = constants.CHARUCO_MARKER_MM,
) -> cv2.aruco.CharucoBoard:
    """Create a CharucoBoard with the configured parameters."""
    dictionary = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
    board = cv2.aruco.CharucoBoard((cols, rows), square_len, marker_len, dictionary)
    return board


def create_detector(board: cv2.aruco.CharucoBoard) -> cv2.aruco.CharucoDetector:
    """Create a CharucoDetector for the given board."""
    return cv2.aruco.CharucoDetector(board)


def generate_board_image(
    output_path: Path,
    cols: int = constants.CHARUCO_COLS,
    rows: int = constants.CHARUCO_ROWS,
    square_len: float = constants.CHARUCO_SQUARE_MM,
    marker_len: float = constants.CHARUCO_MARKER_MM,
    pixels_per_mm: int = 10,
) -> None:
    """Generate a printable CharucoBoard PNG image."""
    board = create_board(cols, rows, square_len, marker_len)
    img_width = int(cols * square_len * pixels_per_mm)
    img_height = int(rows * square_len * pixels_per_mm)
    board_img = board.generateImage((img_width, img_height), marginSize=int(square_len * pixels_per_mm // 2))
    cv2.imwrite(str(output_path), board_img)
    logger.info("CharucoBoard image saved to %s (%dx%d)", output_path, img_width, img_height)


def detect_corners(
    image: np.ndarray,
    detector: cv2.aruco.CharucoDetector,
    min_corners: int = 4,
) -> tuple[np.ndarray | None, np.ndarray | None]:
    """Detect CharucoBoard corners in a single image.

    Returns (corners, ids) or (None, None) if fewer than *min_corners* found.
    """
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if len(image.shape) == 3 else image
    charuco_corners, charuco_ids, marker_corners, marker_ids = detector.detectBoard(gray)

    if charuco_corners is None or charuco_ids is None:
        return None, None

    if len(charuco_corners) < min_corners:
        return None, None

    return charuco_corners, charuco_ids


def calibrate(
    all_corners: list[np.ndarray],
    all_ids: list[np.ndarray],
    board: cv2.aruco.CharucoBoard,
    image_size: tuple[int, int],
) -> IntrinsicResult:
    """Run intrinsic calibration from collected corner sets.

    *image_size* is (width, height).
    """
    if len(all_corners) < 3:
        raise ValueError(f"Need at least 3 captures, got {len(all_corners)}")

    # Build object/image point lists using CharucoBoard.matchImagePoints
    # (OpenCV 4.12+ removed cv2.aruco.calibrateCameraCharuco)
    obj_points = []
    img_points = []
    for corners, ids in zip(all_corners, all_ids):
        obj_pts, img_pts = board.matchImagePoints(corners, ids)
        if obj_pts is not None and len(obj_pts) >= 4:
            obj_points.append(obj_pts)
            img_points.append(img_pts)

    if len(obj_points) < 3:
        raise ValueError(
            f"Need at least 3 usable captures after point matching, got {len(obj_points)}"
        )

    rms, camera_matrix, dist_coeffs, rvecs, tvecs = cv2.calibrateCamera(
        obj_points,
        img_points,
        image_size,
        None,
        None,
    )

    logger.info("Intrinsic calibration complete: RMS=%.4f", rms)
    return IntrinsicResult(
        camera_matrix=camera_matrix,
        dist_coeffs=dist_coeffs,
        rms_error=rms,
        rvecs=list(rvecs),
        tvecs=list(tvecs),
    )


def undistort(
    image: np.ndarray,
    camera_matrix: np.ndarray,
    dist_coeffs: np.ndarray,
) -> np.ndarray:
    """Return undistorted version of *image*."""
    h, w = image.shape[:2]
    new_mtx, roi = cv2.getOptimalNewCameraMatrix(camera_matrix, dist_coeffs, (w, h), 1, (w, h))
    return cv2.undistort(image, camera_matrix, dist_coeffs, None, new_mtx)
