"""pitrac-cal entry point — keyboard-driven calibration state machine.

Usage:
    python3 -m pitrac_cal --camera 1 --mode intrinsic
    python3 -m pitrac_cal --camera 1 --mode extrinsic
    python3 -m pitrac_cal --camera 1 --mode full
    python3 -m pitrac_cal --generate-board board.png
"""

from __future__ import annotations

import logging
import sys
from enum import Enum, auto

import cv2
import numpy as np

from . import cli, camera, config_manager, constants, display, intrinsic, extrinsic

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("pitrac-cal")

WINDOW = "pitrac-cal"


# ---------------------------------------------------------------------------
# State machine
# ---------------------------------------------------------------------------

class State(Enum):
    INTRINSIC_PREVIEW = auto()
    INTRINSIC_DONE = auto()
    EXTRINSIC_PREVIEW = auto()
    EXTRINSIC_DONE = auto()
    FINISHED = auto()


def _build_intrinsic_hud(captures: int, corners_found: bool) -> list[str]:
    status = "CORNERS DETECTED" if corners_found else "no corners"
    return [
        f"== INTRINSIC CALIBRATION (captures: {captures}) ==",
        f"Status: {status}",
        "SPACE=capture  ENTER=calibrate  G=gen board  R=reset  Q=quit",
    ]


def _build_intrinsic_done_hud(rms: float, undistort_on: bool) -> list[str]:
    return [
        f"== CALIBRATION COMPLETE  RMS={rms:.4f} px ==",
        f"Undistort preview: {'ON' if undistort_on else 'OFF'}",
        "U=toggle undistort  S=save to config  Q=quit/next",
    ]


def _build_extrinsic_hud(samples: int, avg_focal: float, detected: bool) -> list[str]:
    det = "BALL DETECTED" if detected else "no ball"
    avg_str = f"{avg_focal:.4f}" if samples > 0 else "—"
    return [
        f"== EXTRINSIC CALIBRATION (samples: {samples}, avg focal: {avg_str} mm) ==",
        f"Status: {det}",
        "SPACE=capture  ENTER=finalize  Q=quit",
    ]


def _build_extrinsic_done_hud(focal: float, yaw: float, pitch: float) -> list[str]:
    return [
        f"== EXTRINSIC COMPLETE ==",
        f"Focal length: {focal:.4f} mm",
        f"Camera angles: yaw={yaw:.4f}  pitch={pitch:.4f} deg",
        "S=save to config  Q=quit",
    ]


# ---------------------------------------------------------------------------
# Intrinsic calibration loop
# ---------------------------------------------------------------------------

def run_intrinsic(
    source: camera.CameraSource,
    camera_num: int,
    config: dict,
    config_path,
) -> intrinsic.IntrinsicResult | None:
    """Run the intrinsic calibration interactive loop. Returns result or None."""
    board = intrinsic.create_board()
    detector = intrinsic.create_detector(board)

    all_corners: list[np.ndarray] = []
    all_ids: list[np.ndarray] = []
    result: intrinsic.IntrinsicResult | None = None
    undistort_on = False
    state = State.INTRINSIC_PREVIEW

    while True:
        frame = source.capture()
        corners, ids = intrinsic.detect_corners(frame, detector)

        if state == State.INTRINSIC_PREVIEW:
            vis = frame.copy()
            if corners is not None:
                vis = display.draw_charuco_corners(vis, corners, ids)
            hud = _build_intrinsic_hud(len(all_corners), corners is not None)
            display.show_frame(WINDOW, vis, hud)

        elif state == State.INTRINSIC_DONE and result is not None:
            vis = frame.copy()
            if undistort_on:
                vis = intrinsic.undistort(vis, result.camera_matrix, result.dist_coeffs)
            vis = display.show_calibration_results(
                vis, result.camera_matrix, result.dist_coeffs, result.rms_error,
            )
            hud = _build_intrinsic_done_hud(result.rms_error, undistort_on)
            display.show_frame(WINDOW, vis, hud)

        key = cv2.waitKey(30) & 0xFF

        if key == ord("q"):
            break

        elif key == ord(" ") and state == State.INTRINSIC_PREVIEW:
            if corners is not None:
                all_corners.append(corners)
                all_ids.append(ids)
                logger.info("Captured frame %d (%d corners)", len(all_corners), len(corners))
            else:
                logger.warning("No corners detected — move the board into view")

        elif key == 13 and state == State.INTRINSIC_PREVIEW:  # ENTER
            if len(all_corners) < 3:
                logger.warning("Need at least 3 captures (have %d)", len(all_corners))
                continue
            h, w = frame.shape[:2]
            try:
                result = intrinsic.calibrate(all_corners, all_ids, board, (w, h))
                state = State.INTRINSIC_DONE
                logger.info("Calibration done: RMS=%.4f", result.rms_error)
            except Exception as e:
                logger.error("Calibration failed: %s", e)

        elif key == ord("u") and state == State.INTRINSIC_DONE:
            undistort_on = not undistort_on

        elif key == ord("s") and state == State.INTRINSIC_DONE and result is not None:
            config_manager.set_calibration_matrix(config, camera_num, result.camera_matrix)
            config_manager.set_distortion_vector(config, camera_num, result.dist_coeffs)
            backup = config_manager.save_config(config, config_path)
            logger.info("Saved intrinsic calibration to %s (backup: %s)", config_path, backup)

        elif key == ord("g"):
            out = f"charuco_board_{constants.CHARUCO_COLS}x{constants.CHARUCO_ROWS}.png"
            intrinsic.generate_board_image(out)
            logger.info("Board image saved to %s", out)

        elif key == ord("r") and state == State.INTRINSIC_PREVIEW:
            all_corners.clear()
            all_ids.clear()
            logger.info("Reset all captures")

    return result


# ---------------------------------------------------------------------------
# Extrinsic calibration loop
# ---------------------------------------------------------------------------

def run_extrinsic(
    source: camera.CameraSource,
    camera_num: int,
    config: dict,
    config_path,
    focal_seed: float | None = None,
) -> tuple[float, float, float] | None:
    """Run the extrinsic calibration interactive loop.

    Returns (focal_length, yaw, pitch) or None.
    """
    ball_pos = config_manager.get_ball_position(config, camera_num)
    distance = extrinsic.get_distance(ball_pos)
    logger.info(
        "Ball position (x,y,z): (%.3f, %.3f, %.3f) → distance %.3f m",
        *ball_pos, distance,
    )

    focal_samples: list[float] = []
    final_yaw: float = 0.0
    final_pitch: float = 0.0
    final_focal: float = 0.0
    state = State.EXTRINSIC_PREVIEW
    last_detection: tuple[tuple[float, float], float] | None = None

    while True:
        frame = source.capture()
        detection = extrinsic.detect_ball(frame)
        last_detection = detection

        avg_focal = sum(focal_samples) / len(focal_samples) if focal_samples else 0.0

        if state == State.EXTRINSIC_PREVIEW:
            vis = frame.copy()
            if detection is not None:
                vis = display.draw_ball_detection(vis, detection[0], detection[1])
            if focal_samples:
                vis = display.draw_focal_length_info(
                    vis, focal_samples[-1], len(focal_samples), avg_focal,
                )
            hud = _build_extrinsic_hud(len(focal_samples), avg_focal, detection is not None)
            display.show_frame(WINDOW, vis, hud)

        elif state == State.EXTRINSIC_DONE:
            vis = frame.copy()
            if detection is not None:
                vis = display.draw_ball_detection(vis, detection[0], detection[1])
            hud = _build_extrinsic_done_hud(final_focal, final_yaw, final_pitch)
            display.show_frame(WINDOW, vis, hud)

        key = cv2.waitKey(30) & 0xFF

        if key == ord("q"):
            break

        elif key == ord(" ") and state == State.EXTRINSIC_PREVIEW:
            if detection is None:
                logger.warning("No ball detected — place ball at calibration position")
                continue

            center, radius = detection
            focal = extrinsic.compute_focal_length(radius, distance)

            if focal < constants.MIN_FOCAL_LENGTH_MM or focal > constants.MAX_FOCAL_LENGTH_MM:
                logger.warning("Computed focal %.2f mm out of range, skipping", focal)
                continue

            focal_samples.append(focal)
            logger.info(
                "Sample %d: radius=%.1f px, focal=%.4f mm (avg=%.4f)",
                len(focal_samples), radius, focal,
                sum(focal_samples) / len(focal_samples),
            )

        elif key == 13 and state == State.EXTRINSIC_PREVIEW:  # ENTER
            if not focal_samples:
                logger.warning("No focal length samples — press SPACE to capture first")
                continue

            final_focal = sum(focal_samples) / len(focal_samples)

            if final_focal < constants.MIN_FOCAL_LENGTH_MM or final_focal > constants.MAX_FOCAL_LENGTH_MM:
                logger.error("Average focal %.2f mm is out of valid range", final_focal)
                continue

            # Use the last detection for angle computation
            if last_detection is None:
                logger.warning("No ball detected in last frame for angle computation")
                continue

            center, radius = last_detection
            h, w = frame.shape[:2]

            try:
                final_yaw, final_pitch = extrinsic.compute_camera_angles(
                    ball_center_px=center,
                    image_size=(w, h),
                    focal_length_mm=final_focal,
                    ball_position_3d=ball_pos,
                )
                state = State.EXTRINSIC_DONE
                logger.info(
                    "Extrinsic done: focal=%.4f mm, yaw=%.4f, pitch=%.4f",
                    final_focal, final_yaw, final_pitch,
                )
            except ValueError as e:
                logger.error("Angle computation failed: %s", e)

        elif key == ord("s") and state == State.EXTRINSIC_DONE:
            config_manager.set_focal_length(config, camera_num, final_focal)
            config_manager.set_camera_angles(config, camera_num, final_yaw, final_pitch)
            backup = config_manager.save_config(config, config_path)
            logger.info("Saved extrinsic calibration to %s (backup: %s)", config_path, backup)

    if state == State.EXTRINSIC_DONE:
        return (final_focal, final_yaw, final_pitch)
    return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    args = cli.parse_args()

    # Handle board generation shortcut
    if args.generate_board is not None:
        intrinsic.generate_board_image(args.generate_board)
        logger.info("Board image written to %s", args.generate_board)
        return 0

    # Resolve config
    try:
        config_path = config_manager.resolve_config_path(args.config)
    except FileNotFoundError as e:
        logger.error("%s", e)
        return 1

    config = config_manager.load_config(config_path)
    logger.info("Config loaded from %s", config_path)

    # Create camera source
    try:
        source = camera.create_source(
            camera_num=args.camera,
            image_dir=args.image_dir,
        )
    except Exception as e:
        logger.error("Failed to create camera source: %s", e)
        return 1

    cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)

    try:
        if args.mode in ("intrinsic", "full"):
            logger.info("Starting intrinsic calibration (camera %d)", args.camera)
            intrinsic_result = run_intrinsic(source, args.camera, config, config_path)

        if args.mode in ("extrinsic", "full"):
            logger.info("Starting extrinsic calibration (camera %d)", args.camera)
            run_extrinsic(source, args.camera, config, config_path)

    finally:
        source.release()
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    sys.exit(main())
