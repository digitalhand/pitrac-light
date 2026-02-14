"""OpenCV highgui window and HUD overlay helpers."""

from __future__ import annotations

import cv2
import numpy as np


# Colours (BGR)
GREEN = (0, 255, 0)
RED = (0, 0, 255)
YELLOW = (0, 255, 255)
CYAN = (255, 255, 0)
WHITE = (255, 255, 255)
BG_DARK = (30, 30, 30)

FONT = cv2.FONT_HERSHEY_SIMPLEX
FONT_SCALE = 0.55
FONT_THICKNESS = 1
LINE_HEIGHT = 22


def show_frame(
    window_name: str,
    image: np.ndarray,
    hud_lines: list[str] | None = None,
) -> None:
    """Display *image* in a named window with an optional text overlay."""
    display = image.copy()
    if hud_lines:
        _draw_hud(display, hud_lines)
    cv2.imshow(window_name, display)


def draw_ball_detection(
    image: np.ndarray,
    center: tuple[int, int],
    radius: float,
) -> np.ndarray:
    """Draw circle + crosshair + radius info on *image* (returns a copy)."""
    out = image.copy()
    cx, cy = int(center[0]), int(center[1])
    r = int(round(radius))

    cv2.circle(out, (cx, cy), r, GREEN, 2)
    cv2.drawMarker(out, (cx, cy), CYAN, cv2.MARKER_CROSS, 20, 1)

    label = f"r={radius:.1f}px  ({cx},{cy})"
    cv2.putText(out, label, (cx + r + 8, cy), FONT, FONT_SCALE, GREEN, FONT_THICKNESS)
    return out


def draw_charuco_corners(
    image: np.ndarray,
    corners: np.ndarray,
    ids: np.ndarray,
) -> np.ndarray:
    """Draw detected CharucoBoard corners with ID labels."""
    out = image.copy()
    cv2.aruco.drawDetectedCornersCharuco(out, corners, ids, GREEN)
    return out


def show_calibration_results(
    image: np.ndarray,
    camera_matrix: np.ndarray,
    dist_coeffs: np.ndarray,
    rms_error: float,
) -> np.ndarray:
    """Overlay intrinsic calibration results on *image*."""
    out = image.copy()
    lines = [
        f"RMS reprojection error: {rms_error:.4f} px",
        f"fx={camera_matrix[0,0]:.2f}  fy={camera_matrix[1,1]:.2f}",
        f"cx={camera_matrix[0,2]:.2f}  cy={camera_matrix[1,2]:.2f}",
        f"dist: [{', '.join(f'{d:.4f}' for d in dist_coeffs.flatten()[:5])}]",
    ]
    _draw_hud(out, lines, y_start=out.shape[0] - len(lines) * LINE_HEIGHT - 20)
    return out


def draw_focal_length_info(
    image: np.ndarray,
    focal_mm: float,
    sample_num: int,
    avg_focal_mm: float,
) -> np.ndarray:
    """Overlay focal length measurement info."""
    out = image.copy()
    lines = [
        f"Sample #{sample_num}: focal={focal_mm:.4f} mm",
        f"Running average: {avg_focal_mm:.4f} mm",
    ]
    _draw_hud(out, lines, y_start=out.shape[0] - len(lines) * LINE_HEIGHT - 20)
    return out


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

def _draw_hud(
    image: np.ndarray,
    lines: list[str],
    y_start: int = 10,
) -> None:
    """Render text lines with a semi-transparent background strip."""
    if not lines:
        return

    max_width = max(
        cv2.getTextSize(line, FONT, FONT_SCALE, FONT_THICKNESS)[0][0]
        for line in lines
    )
    h = len(lines) * LINE_HEIGHT + 10
    w = max_width + 20

    overlay = image.copy()
    cv2.rectangle(overlay, (5, y_start), (5 + w, y_start + h), BG_DARK, -1)
    cv2.addWeighted(overlay, 0.65, image, 0.35, 0, image)

    for i, line in enumerate(lines):
        y = y_start + 20 + i * LINE_HEIGHT
        cv2.putText(image, line, (10, y), FONT, FONT_SCALE, WHITE, FONT_THICKNESS)
