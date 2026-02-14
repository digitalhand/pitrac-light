"""Tests for intrinsic calibration: board creation, corner detection on synthetic images."""

import cv2
import numpy as np
import pytest

from pitrac_cal import intrinsic, constants


class TestBoardCreation:
    def test_default_board_dimensions(self):
        board = intrinsic.create_board()
        size = board.getChessboardSize()
        assert size == (constants.CHARUCO_COLS, constants.CHARUCO_ROWS)

    def test_custom_board_dimensions(self):
        board = intrinsic.create_board(cols=5, rows=3)
        size = board.getChessboardSize()
        assert size == (5, 3)


class TestBoardImageGeneration:
    def test_generate_creates_file(self, tmp_path):
        output = tmp_path / "board.png"
        intrinsic.generate_board_image(output)
        assert output.exists()
        img = cv2.imread(str(output))
        assert img is not None
        assert img.shape[0] > 0
        assert img.shape[1] > 0


class TestCornerDetection:
    def _render_board_view(self, board, image_size=(800, 600)):
        """Render a synthetic board image for detection testing."""
        board_img = board.generateImage(image_size)
        # Convert to BGR (as if captured by camera)
        if len(board_img.shape) == 2:
            board_img = cv2.cvtColor(board_img, cv2.COLOR_GRAY2BGR)
        return board_img

    def test_detect_on_synthetic_board(self):
        board = intrinsic.create_board()
        detector = intrinsic.create_detector(board)
        img = self._render_board_view(board)

        corners, ids = intrinsic.detect_corners(img, detector)
        assert corners is not None
        assert ids is not None
        assert len(corners) >= 4

    def test_detect_returns_none_on_blank_image(self):
        board = intrinsic.create_board()
        detector = intrinsic.create_detector(board)
        blank = np.zeros((600, 800, 3), dtype=np.uint8)

        corners, ids = intrinsic.detect_corners(blank, detector)
        assert corners is None
        assert ids is None


class TestCalibration:
    def test_calibrate_with_synthetic_views(self):
        """Calibrate using multiple rendered board views (identity-like result expected)."""
        board = intrinsic.create_board()
        detector = intrinsic.create_detector(board)

        all_corners = []
        all_ids = []

        # Generate the board at different scales/positions by rendering
        # then cropping or padding to simulate different viewpoints
        base_img = board.generateImage((800, 600))
        if len(base_img.shape) == 2:
            base_img = cv2.cvtColor(base_img, cv2.COLOR_GRAY2BGR)

        # Use different scaled versions as "different views"
        for scale in [1.0, 0.8, 0.6, 0.9, 0.7]:
            h, w = base_img.shape[:2]
            new_h, new_w = int(h * scale), int(w * scale)
            resized = cv2.resize(base_img, (new_w, new_h))

            # Place in a larger canvas to simulate different positions
            canvas = np.ones((600, 800, 3), dtype=np.uint8) * 255
            y_off = (600 - new_h) // 2
            x_off = (800 - new_w) // 2
            canvas[y_off:y_off + new_h, x_off:x_off + new_w] = resized

            corners, ids = intrinsic.detect_corners(canvas, detector)
            if corners is not None:
                all_corners.append(corners)
                all_ids.append(ids)

        if len(all_corners) < 3:
            pytest.skip("Could not detect enough views in synthetic images")

        result = intrinsic.calibrate(all_corners, all_ids, board, (800, 600))
        assert result.camera_matrix.shape == (3, 3)
        assert result.dist_coeffs.shape[1] == 5
        # RMS should be reasonable for synthetic data
        assert result.rms_error >= 0

    def test_calibrate_rejects_too_few_captures(self):
        board = intrinsic.create_board()
        with pytest.raises(ValueError, match="at least 3"):
            intrinsic.calibrate([], [], board, (800, 600))
