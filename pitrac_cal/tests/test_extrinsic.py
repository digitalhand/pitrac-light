"""Tests for extrinsic calibration: focal length formula parity, distance conversions, camera angles."""

import math

import cv2
import numpy as np
import pytest

from pitrac_cal import extrinsic, constants


class TestFocalLength:
    """Verify focal length formula matches gs_camera.cpp:909-913."""

    def test_known_values(self):
        """Cross-check with manually computed expected value.

        Given: ball at 0.54m, measured radius 85px.
        focal = 0.54 * 5.077365371 * (2*85/1456) / (2*0.021335)
              = 0.54 * 5.077365371 * 0.116758 / 0.04267
              = 7.5... mm (approximately)
        """
        focal = extrinsic.compute_focal_length(
            ball_radius_px=85.0,
            ball_distance_m=0.54,
            sensor_width_mm=constants.SENSOR_WIDTH_MM,
            resolution_x=constants.RESOLUTION_X,
            ball_radius_m=constants.BALL_RADIUS_M,
        )
        # Verify the formula structure produces a reasonable lens focal length
        assert 2.0 < focal < 50.0

        # Verify exact formula: f = d * sw * (2r/rx) / (2*R)
        expected = (
            0.54 * constants.SENSOR_WIDTH_MM
            * (2.0 * 85.0 / constants.RESOLUTION_X)
            / (2.0 * constants.BALL_RADIUS_M)
        )
        assert focal == pytest.approx(expected, rel=1e-12)

    def test_doubling_distance_doubles_focal(self):
        """Focal length is linear in distance (if pixel radius stays same)."""
        f1 = extrinsic.compute_focal_length(85.0, 0.5)
        f2 = extrinsic.compute_focal_length(85.0, 1.0)
        assert f2 == pytest.approx(2.0 * f1, rel=1e-12)

    def test_doubling_pixel_radius_doubles_focal(self):
        """Focal length is linear in pixel radius."""
        f1 = extrinsic.compute_focal_length(50.0, 0.5)
        f2 = extrinsic.compute_focal_length(100.0, 0.5)
        assert f2 == pytest.approx(2.0 * f1, rel=1e-12)

    def test_zero_radius_gives_zero(self):
        assert extrinsic.compute_focal_length(0.0, 0.5) == 0.0


class TestDistanceConversions:
    """Verify distance conversion formulas match gs_camera.cpp:916-928."""

    def test_x_at_center_is_zero(self):
        """Zero pixel offset → zero meter offset."""
        result = extrinsic.convert_x_distance_to_meters(
            z_distance_m=0.5, x_distance_px=0.0, focal_length_mm=5.0
        )
        assert result == pytest.approx(0.0)

    def test_y_at_center_is_zero(self):
        result = extrinsic.convert_y_distance_to_meters(
            z_distance_m=0.5, y_distance_px=0.0, focal_length_mm=5.0
        )
        assert result == pytest.approx(0.0)

    def test_x_conversion_symmetry(self):
        """Positive and negative pixel offsets give opposite meter offsets."""
        pos = extrinsic.convert_x_distance_to_meters(0.5, 100.0, 5.0)
        neg = extrinsic.convert_x_distance_to_meters(0.5, -100.0, 5.0)
        assert pos == pytest.approx(-neg, rel=1e-12)

    def test_x_conversion_formula(self):
        """Verify exact formula: halfW = (z/f)*(sw/2); result = halfW * (xpx/(rx/2))."""
        z, xpx, f = 0.5, 100.0, 5.0
        half_w = (z / f) * (constants.SENSOR_WIDTH_MM / 2.0)
        expected = half_w * (xpx / (constants.RESOLUTION_X / 2.0))
        actual = extrinsic.convert_x_distance_to_meters(z, xpx, f)
        assert actual == pytest.approx(expected, rel=1e-12)


class TestGetDistance:
    def test_3_4_5_triangle(self):
        assert extrinsic.get_distance((3.0, 4.0, 0.0)) == pytest.approx(5.0)

    def test_1_2_2(self):
        assert extrinsic.get_distance((1.0, 2.0, 2.0)) == pytest.approx(3.0)

    def test_zero(self):
        assert extrinsic.get_distance((0.0, 0.0, 0.0)) == 0.0


class TestCameraAngles:
    """Verify camera angle computation matches gs_calibration.cpp:163-278."""

    def test_ball_at_center_straight_camera(self):
        """Ball exactly on bore axis → angles equal LM perspective angles only."""
        # Ball at (0, 0, 0.5) means straight ahead. If detected at image center,
        # camera bore = LM perspective, so angles should be ~0.
        yaw, pitch = extrinsic.compute_camera_angles(
            ball_center_px=(constants.RESOLUTION_X / 2.0, constants.RESOLUTION_Y / 2.0),
            image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
            focal_length_mm=5.0,
            ball_position_3d=(0.0, 0.0, 0.5),
        )
        assert yaw == pytest.approx(0.0, abs=0.01)
        assert pitch == pytest.approx(0.0, abs=0.01)

    def test_ball_offset_x_produces_yaw(self):
        """Ball offset to the left (negative X) with detection at center → positive yaw."""
        yaw, pitch = extrinsic.compute_camera_angles(
            ball_center_px=(constants.RESOLUTION_X / 2.0, constants.RESOLUTION_Y / 2.0),
            image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
            focal_length_mm=5.0,
            ball_position_3d=(-0.12, 0.0, 0.5),
        )
        # Negative X → positive yaw (camera points left)
        assert yaw > 0
        assert abs(pitch) < 1.0  # Pitch should be near zero

    def test_ball_offset_y_produces_pitch(self):
        """Ball below camera (negative Y) with detection at center → negative pitch."""
        yaw, pitch = extrinsic.compute_camera_angles(
            ball_center_px=(constants.RESOLUTION_X / 2.0, constants.RESOLUTION_Y / 2.0),
            image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
            focal_length_mm=5.0,
            ball_position_3d=(0.0, -0.2, 0.5),
        )
        assert abs(yaw) < 1.0
        assert pitch < 0  # Looking down

    def test_realistic_camera1_scenario(self):
        """Simulate a realistic Camera 1 calibration and verify reasonable angles."""
        # Camera 1 ball position from config: (-0.120, -0.28, 0.44)
        ball_pos = (-0.120, -0.28, 0.44)
        # Suppose ball is detected slightly off center
        yaw, pitch = extrinsic.compute_camera_angles(
            ball_center_px=(650.0, 600.0),
            image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
            focal_length_mm=5.87,
            ball_position_3d=ball_pos,
        )
        # Should produce reasonable angles (within bounds)
        assert abs(yaw) < constants.MAX_REASONABLE_ANGLE_DEG
        assert abs(pitch) < constants.MAX_REASONABLE_ANGLE_DEG

    def test_rejects_zero_distance(self):
        with pytest.raises(ValueError, match="too close"):
            extrinsic.compute_camera_angles(
                ball_center_px=(728.0, 544.0),
                image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
                focal_length_mm=5.0,
                ball_position_3d=(0.0, 0.0, 0.0),
            )

    def test_rejects_negative_z(self):
        with pytest.raises(ValueError, match="Z position must be positive"):
            extrinsic.compute_camera_angles(
                ball_center_px=(728.0, 544.0),
                image_size=(constants.RESOLUTION_X, constants.RESOLUTION_Y),
                focal_length_mm=5.0,
                ball_position_3d=(0.0, 0.0, -0.5),
            )


class TestBallDetection:
    def test_detect_synthetic_circle(self):
        """Create a synthetic white circle on dark background and detect it."""
        img = np.zeros((600, 800, 3), dtype=np.uint8)
        cv2.circle(img, (400, 300), 50, (255, 255, 255), -1)

        result = extrinsic.detect_ball(
            img, min_radius=30, max_radius=80, param1=50, param2=20
        )
        assert result is not None
        center, radius = result
        assert abs(center[0] - 400) < 10
        assert abs(center[1] - 300) < 10
        assert abs(radius - 50) < 10

    def test_detect_returns_none_on_blank(self):
        img = np.zeros((600, 800, 3), dtype=np.uint8)
        result = extrinsic.detect_ball(img)
        assert result is None
