"""Tests for config_manager: round-trip read/write, string precision, ball position."""

import json
import tempfile
from pathlib import Path

import numpy as np
import pytest

from pitrac_cal import config_manager, constants


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_config() -> dict:
    """Minimal config matching the real golf_sim_config.json structure."""
    return {
        "gs_config": {
            "cameras": {
                "kCamera1FocalLength": "5.8675451035986486",
                "kCamera1Angles": ["2.1406460383579446", "-26.426049413957287"],
                "kCamera1CalibrationMatrix": [
                    ["1833.5291988027575", "0.0", "697.2791579239232"],
                    ["0.0", "1832.2499845181273", "513.0904087097207"],
                    ["0.0", "0.0", "1.0"],
                ],
                "kCamera1DistortionVector": [
                    "-0.5088115166383071",
                    "0.34039760498152727",
                    "-0.0020686673595964942",
                    "0.0025571075134913457",
                    "-0.13507994577035",
                ],
                "kCamera2FocalLength": "5.5107136256866491",
                "kCamera2Angles": ["-3.9096762712853037", "10.292573196795921"],
                "kCamera2CalibrationMatrix": [
                    ["2340.2520648903665", "0.0", "698.4611375636877"],
                    ["0.0", "2318.2676880118993", "462.7245851119162"],
                    ["0.0", "0.0", "1.0"],
                ],
                "kCamera2DistortionVector": [
                    "-0.818067967818754",
                    "1.1642822122721734",
                    "0.03170748960585329",
                    "-0.000598495710701826",
                    "-1.9419896904560514",
                ],
            },
            "calibration": {
                "kCalibrationRigType": "2",
                "kCustomCalibrationRigPositionFromCamera1": ["0.0", "0.0", "0.0"],
                "kCustomCalibrationRigPositionFromCamera2": ["0.0", "0.0", "0.0"],
                "kAutoCalibrationBaselineBallPositionFromCamera1MetersForStraightOutCameras": [
                    "-0.120", "-0.28", "0.44"
                ],
                "kAutoCalibrationBaselineBallPositionFromCamera2MetersForStraightOutCameras": [
                    "0.00", "0.095", "0.435"
                ],
                "kAutoCalibrationBaselineBallPositionFromCamera1MetersForSkewedCameras": [
                    "-0.120", "-0.28", "0.44"
                ],
                "kAutoCalibrationBaselineBallPositionFromCamera2MetersForSkewedCameras": [
                    "0.00", "0.095", "0.435"
                ],
            },
        }
    }


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

class TestFloatToConfigStr:
    def test_high_precision(self):
        result = config_manager.float_to_config_str(5.8675451035986486)
        assert result == "5.8675451035986486"

    def test_zero(self):
        assert config_manager.float_to_config_str(0.0) == "0"

    def test_one(self):
        assert config_manager.float_to_config_str(1.0) == "1"

    def test_negative(self):
        original = -0.5088115166383071
        result = config_manager.float_to_config_str(original)
        # Round-trip: string must parse back to the same float
        assert float(result) == original


class TestRoundTrip:
    def test_save_and_reload_preserves_structure(self, tmp_path):
        config = _make_config()
        config_path = tmp_path / "golf_sim_config.json"

        # Write initial config
        with open(config_path, "w") as f:
            json.dump(config, f, indent=4)

        # Load, modify, save
        loaded = config_manager.load_config(config_path)
        config_manager.set_focal_length(loaded, 1, 6.123456789012345)
        backup = config_manager.save_config(loaded, config_path)

        # Backup was created
        assert backup.exists()

        # Re-load and check
        reloaded = config_manager.load_config(config_path)
        assert float(reloaded["gs_config"]["cameras"]["kCamera1FocalLength"]) == pytest.approx(6.123456789012345)

        # Other values untouched
        assert reloaded["gs_config"]["cameras"]["kCamera2FocalLength"] == "5.5107136256866491"

    def test_calibration_matrix_round_trip(self, tmp_path):
        config = _make_config()
        config_path = tmp_path / "golf_sim_config.json"
        with open(config_path, "w") as f:
            json.dump(config, f, indent=4)

        loaded = config_manager.load_config(config_path)

        matrix = np.array([
            [1833.5, 0.0, 697.3],
            [0.0, 1832.2, 513.1],
            [0.0, 0.0, 1.0],
        ])
        config_manager.set_calibration_matrix(loaded, 1, matrix)

        dist = np.array([-0.509, 0.340, -0.002, 0.003, -0.135])
        config_manager.set_distortion_vector(loaded, 1, dist)

        config_manager.save_config(loaded, config_path)
        reloaded = config_manager.load_config(config_path)

        cam_matrix = reloaded["gs_config"]["cameras"]["kCamera1CalibrationMatrix"]
        assert len(cam_matrix) == 3
        assert len(cam_matrix[0]) == 3
        # All values are strings
        for row in cam_matrix:
            for val in row:
                assert isinstance(val, str)
                float(val)  # Should not raise

        dist_vec = reloaded["gs_config"]["cameras"]["kCamera1DistortionVector"]
        assert len(dist_vec) == 5
        for val in dist_vec:
            assert isinstance(val, str)


class TestCameraAngles:
    def test_set_and_read_angles(self):
        config = _make_config()
        config_manager.set_camera_angles(config, 1, 2.5, -26.0)

        angles = config["gs_config"]["cameras"]["kCamera1Angles"]
        assert len(angles) == 2
        assert float(angles[0]) == pytest.approx(2.5)
        assert float(angles[1]) == pytest.approx(-26.0)


class TestExpectedBallRadius:
    def test_compute_expected_ball_radius_at_40cm(self):
        focal = 5.8675451035986486
        radius_px = config_manager.compute_expected_ball_radius_pixels_at_distance(
            focal_length_mm=focal,
            distance_m=0.4,
        )
        expected = int(
            round(
                (
                    focal
                    * constants.BALL_RADIUS_M
                    * constants.RESOLUTION_X
                ) / (0.4 * constants.SENSOR_WIDTH_MM)
            )
        )
        assert radius_px == expected

    def test_set_expected_ball_radius_at_40cm_writes_camera_key(self):
        config = _make_config()
        value = config_manager.set_expected_ball_radius_pixels_at_40cm(
            config,
            camera_num=2,
            focal_length_mm=5.5,
        )
        assert config["gs_config"]["cameras"]["kExpectedBallRadiusPixelsAt40cmCamera2"] == str(value)


class TestBallPosition:
    def test_straight_forward_rig(self):
        config = _make_config()
        config["gs_config"]["calibration"]["kCalibrationRigType"] = "1"

        pos = config_manager.get_ball_position(config, 1)
        assert pos == pytest.approx((-0.120, -0.28, 0.44))

    def test_skewed_rig(self):
        config = _make_config()
        # kCalibrationRigType=2 means skewed (matching C++ enum)
        pos = config_manager.get_ball_position(config, 2)
        assert pos == pytest.approx((0.0, 0.095, 0.435))

    def test_custom_rig(self):
        config = _make_config()
        config["gs_config"]["calibration"]["kCalibrationRigType"] = "3"
        config["gs_config"]["calibration"]["kCustomCalibrationRigPositionFromCamera1"] = [
            "0.1", "0.2", "0.3"
        ]

        pos = config_manager.get_ball_position(config, 1)
        assert pos == pytest.approx((0.1, 0.2, 0.3))

    def test_unknown_rig_raises(self):
        config = _make_config()
        config["gs_config"]["calibration"]["kCalibrationRigType"] = "99"

        with pytest.raises(ValueError, match="Unknown calibration rig type"):
            config_manager.get_ball_position(config, 1)


class TestBackup:
    def test_backup_created_on_save(self, tmp_path):
        config = _make_config()
        config_path = tmp_path / "golf_sim_config.json"
        with open(config_path, "w") as f:
            json.dump(config, f, indent=4)

        backup = config_manager.save_config(config, config_path)
        assert backup.exists()
        assert "_BACKUP_" in backup.name
        assert backup.name.endswith(".json")

        # Backup content matches original
        with open(backup) as f:
            backup_data = json.load(f)
        assert backup_data == config
