"""Extrinsic calibration: ball detection, focal length, and camera angle computation.

All formulas are direct ports from the C++ codebase to ensure numerical parity:
- Focal length: gs_camera.cpp:909-913 (computeFocalDistanceFromBallData)
- Distance conversions: gs_camera.cpp:916-928 (convertX/YDistanceToMeters)
- Camera angles: gs_calibration.cpp:163-278 (DetermineCameraAngles)
"""

from __future__ import annotations

import math
import logging

import cv2
import numpy as np

from . import constants

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Ball detection
# ---------------------------------------------------------------------------

def detect_ball(
    image: np.ndarray,
    min_radius: int = 20,
    max_radius: int = 200,
    param1: float = 100,
    param2: float = 30,
    min_dist: int | None = None,
) -> tuple[tuple[float, float], float] | None:
    """Detect a golf ball using HoughCircles.

    Returns ((center_x, center_y), radius) or None if no ball found.
    """
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY) if len(image.shape) == 3 else image
    blurred = cv2.GaussianBlur(gray, (9, 9), 2)

    if min_dist is None:
        min_dist = gray.shape[0] // 4

    circles = cv2.HoughCircles(
        blurred,
        cv2.HOUGH_GRADIENT,
        dp=1.2,
        minDist=min_dist,
        param1=param1,
        param2=param2,
        minRadius=min_radius,
        maxRadius=max_radius,
    )

    if circles is None:
        return None

    # Take the best (first) circle
    c = circles[0][0]
    center = (float(c[0]), float(c[1]))
    radius = float(c[2])
    return center, radius


# ---------------------------------------------------------------------------
# Focal length computation — port of gs_camera.cpp:909-913
# ---------------------------------------------------------------------------

def compute_focal_length(
    ball_radius_px: float,
    ball_distance_m: float,
    sensor_width_mm: float = constants.SENSOR_WIDTH_MM,
    resolution_x: int = constants.RESOLUTION_X,
    ball_radius_m: float = constants.BALL_RADIUS_M,
) -> float:
    """Compute focal length from known ball size and measured pixel radius.

    Direct port of GolfSimCamera::computeFocalDistanceFromBallData
    (gs_camera.cpp:909-913).

    Returns focal length in mm.
    """
    focal_length_mm = (
        ball_distance_m
        * sensor_width_mm
        * (2.0 * ball_radius_px / resolution_x)
        / (2.0 * ball_radius_m)
    )
    return focal_length_mm


# ---------------------------------------------------------------------------
# Distance conversions — port of gs_camera.cpp:916-928
# ---------------------------------------------------------------------------

def convert_x_distance_to_meters(
    z_distance_m: float,
    x_distance_px: float,
    focal_length_mm: float,
    sensor_width_mm: float = constants.SENSOR_WIDTH_MM,
    resolution_x: int = constants.RESOLUTION_X,
) -> float:
    """Convert pixel X offset to meters at a given Z distance.

    Port of GolfSimCamera::convertXDistanceToMeters (gs_camera.cpp:916-921).
    """
    half_width_m = (z_distance_m / focal_length_mm) * (sensor_width_mm / 2.0)
    return half_width_m * (x_distance_px / (resolution_x / 2.0))


def convert_y_distance_to_meters(
    z_distance_m: float,
    y_distance_px: float,
    focal_length_mm: float,
    sensor_height_mm: float = constants.SENSOR_HEIGHT_MM,
    resolution_y: int = constants.RESOLUTION_Y,
) -> float:
    """Convert pixel Y offset to meters at a given Z distance.

    Port of GolfSimCamera::convertYDistanceToMeters (gs_camera.cpp:923-928).
    """
    half_height_m = (z_distance_m / focal_length_mm) * (sensor_height_mm / 2.0)
    return half_height_m * (y_distance_px / (resolution_y / 2.0))


# ---------------------------------------------------------------------------
# Camera angle computation — port of gs_calibration.cpp:163-278
# ---------------------------------------------------------------------------

def get_distance(position: tuple[float, float, float]) -> float:
    """Euclidean distance — port of CvUtils::GetDistance (cv_utils.cpp:324-326)."""
    return math.sqrt(position[0] ** 2 + position[1] ** 2 + position[2] ** 2)


def compute_camera_angles(
    ball_center_px: tuple[float, float],
    image_size: tuple[int, int],
    focal_length_mm: float,
    ball_position_3d: tuple[float, float, float],
    sensor_width_mm: float = constants.SENSOR_WIDTH_MM,
    sensor_height_mm: float = constants.SENSOR_HEIGHT_MM,
    resolution_x: int = constants.RESOLUTION_X,
    resolution_y: int = constants.RESOLUTION_Y,
) -> tuple[float, float]:
    """Compute camera yaw and pitch angles from ball position.

    Direct port of GolfSimCalibration::DetermineCameraAngles
    (gs_calibration.cpp:163-278).

    Parameters
    ----------
    ball_center_px : (x, y) pixel coordinates of detected ball center
    image_size : (width, height) of the image
    focal_length_mm : calibrated focal length
    ball_position_3d : (x, y, z) known ball position in meters from camera

    Returns
    -------
    (yaw_degrees, pitch_degrees) — camera angles
    """
    width, height = image_size

    # Offset from image center (gs_calibration.cpp:199-200)
    x_from_center = ball_center_px[0] - round(resolution_x / 2.0)
    y_from_center = ball_center_px[1] - round(resolution_y / 2.0)

    # Direct distance to ball (gs_calibration.cpp:204)
    distance_direct = get_distance(ball_position_3d)

    if distance_direct <= 0.0001:
        raise ValueError("Ball position too close to camera (distance ~0)")

    if ball_position_3d[2] < 0.0:
        raise ValueError("Ball Z position must be positive (in front of camera)")

    # Convert pixel offset to meters (gs_calibration.cpp:221-228)
    x_dist_from_center_m = convert_x_distance_to_meters(
        distance_direct, x_from_center, focal_length_mm,
        sensor_width_mm, resolution_x,
    )
    y_dist_from_center_m = convert_y_distance_to_meters(
        distance_direct, y_from_center, focal_length_mm,
        sensor_height_mm, resolution_y,
    )

    # Angles from camera bore to ball (gs_calibration.cpp:239-240)
    x_angle_camera = -math.degrees(math.atan(x_dist_from_center_m / distance_direct))
    y_angle_camera = math.degrees(math.atan(-y_dist_from_center_m / distance_direct))

    # Angles from LM perspective (if camera was pointing straight out)
    # (gs_calibration.cpp:250-254)
    x_angle_lm = -math.degrees(
        math.atan(ball_position_3d[0] / ball_position_3d[2])
    )
    horiz_dist = math.sqrt(ball_position_3d[0] ** 2 + ball_position_3d[2] ** 2)
    y_angle_lm = math.degrees(
        math.atan(ball_position_3d[1] / horiz_dist)
    )

    # Camera angles = difference (gs_calibration.cpp:261-262)
    yaw = x_angle_lm - x_angle_camera
    pitch = y_angle_lm - y_angle_camera

    if abs(yaw) > constants.MAX_REASONABLE_ANGLE_DEG or abs(pitch) > constants.MAX_REASONABLE_ANGLE_DEG:
        raise ValueError(
            f"Computed angles ({yaw:.2f}, {pitch:.2f}) exceed "
            f"+/- {constants.MAX_REASONABLE_ANGLE_DEG} degrees"
        )

    return (yaw, pitch)
