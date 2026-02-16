"""Read/write golf_sim_config.json preserving Boost.property_tree string format.

The C++ side uses Boost.property_tree which stores every value as a JSON string.
This module preserves that convention so the C++ code can read values written here.
"""

import json
import math
import os
import shutil
from datetime import datetime
from pathlib import Path
from typing import Any

import numpy as np

from . import constants


def float_to_config_str(value: float) -> str:
    """Format a float matching Boost.property_tree's default precision (17 sig digits)."""
    return f"{value:.17g}"


def load_config(path: Path) -> dict:
    """Load golf_sim_config.json, preserving all string-typed values."""
    with open(path, "r") as f:
        return json.load(f)


def save_config(config: dict, path: Path) -> Path:
    """Write config to *path*, creating a timestamped backup first.

    Returns the backup path.
    """
    backup_path = _create_backup(path)
    with open(path, "w") as f:
        json.dump(config, f, indent=4)
        f.write("\n")
    return backup_path


def _create_backup(path: Path) -> Path:
    """Copy *path* to <path>_BACKUP_<timestamp>.json (matches C++ convention)."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = Path(f"{path}_BACKUP_{timestamp}.json")
    shutil.copy2(path, backup)
    return backup


def resolve_config_path(explicit: Path | None) -> Path:
    """Resolve the config file path.

    Priority: explicit argument > ~/.pitrac/config/golf_sim_config.json.
    """
    if explicit is not None:
        return explicit.resolve()

    candidate = Path.home() / ".pitrac" / "config" / "golf_sim_config.json"
    if candidate.exists():
        return candidate.resolve()

    raise FileNotFoundError(
        "Cannot find runtime golf_sim_config.json at "
        f"{candidate}. Run 'pitrac-cli config init' or pass --config."
    )


# ---------------------------------------------------------------------------
# Calibration rig → ball position resolution
# ---------------------------------------------------------------------------

def get_ball_position(config: dict, camera_num: int) -> tuple[float, float, float]:
    """Resolve the calibration ball position for a camera.

    Replicates RetrieveAutoCalibrationConstants() from gs_calibration.cpp:79-116.
    """
    cal = config["gs_config"]["calibration"]
    rig_type = int(cal["kCalibrationRigType"])

    cam_key = f"Camera{camera_num}"

    if rig_type == constants.RIG_STRAIGHT_FORWARD:
        key = f"kAutoCalibrationBaselineBallPositionFrom{cam_key}MetersForStraightOutCameras"
    elif rig_type == constants.RIG_SKEWED_CAMERA1:
        key = f"kAutoCalibrationBaselineBallPositionFrom{cam_key}MetersForSkewedCameras"
    elif rig_type == constants.RIG_CUSTOM:
        key = f"kCustomCalibrationRigPositionFrom{cam_key}"
    else:
        raise ValueError(f"Unknown calibration rig type: {rig_type}")

    vec = cal[key]
    return (float(vec[0]), float(vec[1]), float(vec[2]))


# ---------------------------------------------------------------------------
# Writing calibration results
# ---------------------------------------------------------------------------

def set_focal_length(config: dict, camera_num: int, focal_length_mm: float) -> None:
    """Write focal length into config dict."""
    key = f"kCamera{camera_num}FocalLength"
    config["gs_config"]["cameras"][key] = float_to_config_str(focal_length_mm)


def set_camera_angles(config: dict, camera_num: int, yaw_deg: float, pitch_deg: float) -> None:
    """Write camera angles [yaw, pitch] into config dict."""
    key = f"kCamera{camera_num}Angles"
    config["gs_config"]["cameras"][key] = [
        float_to_config_str(yaw_deg),
        float_to_config_str(pitch_deg),
    ]


def set_calibration_matrix(config: dict, camera_num: int, matrix: np.ndarray) -> None:
    """Write 3x3 camera calibration matrix into config dict.

    Stored as [[str, str, str], [str, str, str], [str, str, str]].
    """
    key = f"kCamera{camera_num}CalibrationMatrix"
    config["gs_config"]["cameras"][key] = [
        [float_to_config_str(matrix[r, c]) for c in range(3)]
        for r in range(3)
    ]


def set_distortion_vector(config: dict, camera_num: int, dist_coeffs: np.ndarray) -> None:
    """Write distortion vector (5 coefficients) into config dict."""
    key = f"kCamera{camera_num}DistortionVector"
    coeffs = dist_coeffs.flatten()[:5]
    config["gs_config"]["cameras"][key] = [
        float_to_config_str(float(coeffs[i])) for i in range(5)
    ]


def compute_expected_ball_radius_pixels_at_distance(
    focal_length_mm: float,
    distance_m: float,
) -> int:
    """Compute expected ball radius in pixels for a known camera/ball distance."""
    radius_px = (
        focal_length_mm
        * constants.BALL_RADIUS_M
        * constants.RESOLUTION_X
    ) / (distance_m * constants.SENSOR_WIDTH_MM)
    return int(round(radius_px))


def set_expected_ball_radius_pixels_at_40cm(
    config: dict,
    camera_num: int,
    focal_length_mm: float,
) -> int:
    """Write kExpectedBallRadiusPixelsAt40cmCamera{N} derived from focal length."""
    key = f"kExpectedBallRadiusPixelsAt40cmCamera{camera_num}"
    value = compute_expected_ball_radius_pixels_at_distance(focal_length_mm, 0.4)
    config["gs_config"]["cameras"][key] = str(value)
    return value


def get_camera_calibration_values(config: dict, camera_num: int) -> dict[str, Any]:
    """Return camera calibration values currently present in config."""
    cameras = config.get("gs_config", {}).get("cameras", {})
    return {
        f"kCamera{camera_num}CalibrationMatrix": cameras.get(f"kCamera{camera_num}CalibrationMatrix"),
        f"kCamera{camera_num}DistortionVector": cameras.get(f"kCamera{camera_num}DistortionVector"),
        f"kCamera{camera_num}FocalLength": cameras.get(f"kCamera{camera_num}FocalLength"),
        f"kCamera{camera_num}Angles": cameras.get(f"kCamera{camera_num}Angles"),
        f"kExpectedBallRadiusPixelsAt40cmCamera{camera_num}": cameras.get(
            f"kExpectedBallRadiusPixelsAt40cmCamera{camera_num}"
        ),
    }


def get_startup_calibration_coverage(config: dict, camera_num: int) -> list[dict[str, str]]:
    """Summarize runtime calibration key coverage for pitrac_lm startup.

    These keys are consumed during startup in C++ (camera_hardware.cpp).
    Missing keys trigger fallback/default behavior at runtime.
    """
    values = get_camera_calibration_values(config, camera_num)
    coverage: list[dict[str, str]] = []

    def _status(value: Any) -> str:
        return "present" if value is not None else "missing"

    coverage.append({
        "key": f"gs_config.cameras.kCamera{camera_num}CalibrationMatrix",
        "status": _status(values[f"kCamera{camera_num}CalibrationMatrix"]),
        "fallback": "identity matrix + undistortion disabled when missing/invalid",
    })
    coverage.append({
        "key": f"gs_config.cameras.kCamera{camera_num}DistortionVector",
        "status": _status(values[f"kCamera{camera_num}DistortionVector"]),
        "fallback": "identity distortion + undistortion disabled when missing/invalid",
    })
    coverage.append({
        "key": f"gs_config.cameras.kCamera{camera_num}FocalLength",
        "status": _status(values[f"kCamera{camera_num}FocalLength"]),
        "fallback": "camera-model lens default focal length",
    })
    coverage.append({
        "key": f"gs_config.cameras.kCamera{camera_num}Angles",
        "status": _status(values[f"kCamera{camera_num}Angles"]),
        "fallback": "existing in-memory default angles (typically zeros)",
    })
    coverage.append({
        "key": f"gs_config.cameras.kExpectedBallRadiusPixelsAt40cmCamera{camera_num}",
        "status": _status(values[f"kExpectedBallRadiusPixelsAt40cmCamera{camera_num}"]),
        "fallback": "camera-model default expected ball radius",
    })

    return coverage
