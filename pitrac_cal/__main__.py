"""pitrac-cal entry point — keyboard-driven calibration state machine.

Usage:
    python3 -m pitrac_cal --camera 1 --mode intrinsic
    python3 -m pitrac_cal --camera 1 --mode extrinsic
    python3 -m pitrac_cal --camera 1 --mode full
    python3 -m pitrac_cal --generate-board board.png
"""

from __future__ import annotations

import logging
import os
import sys
import time
from datetime import datetime
from enum import Enum, auto
from pathlib import Path

import cv2
import numpy as np

from . import cli, camera, config_manager, constants, display, intrinsic, extrinsic

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger("pitrac-cal")

WINDOW = "pitrac-cal"

# Default log directory for saved calibration images
_DEFAULT_LOG_DIR = Path(
    os.environ.get(
        "PITRAC_BASE_IMAGE_LOGGING_DIR",
        os.path.expanduser("~/LM_Shares/PiTracLogs"),
    )
)


def _get_log_dir(camera_num: int, mode: str) -> Path:
    """Create and return a timestamped directory for saving calibration images."""
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_dir = _DEFAULT_LOG_DIR / f"calibrate_cam{camera_num}_{mode}_{timestamp}"
    log_dir.mkdir(parents=True, exist_ok=True)
    logger.info("Saving calibration images to %s", log_dir)
    return log_dir


def _save_frame(frame: np.ndarray, log_dir: Path, prefix: str, index: int) -> None:
    """Save a frame to the log directory."""
    path = log_dir / f"{prefix}_{index:03d}.png"
    cv2.imwrite(str(path), frame)
    logger.info("Saved %s", path)


def _log_saved_calibration_values(config: dict, camera_num: int, title: str) -> None:
    values = config_manager.get_camera_calibration_values(config, camera_num)
    logger.info("%s", title)
    logger.info(
        "gs_config.cameras.kCamera%dCalibrationMatrix=%s",
        camera_num,
        values[f"kCamera{camera_num}CalibrationMatrix"],
    )
    logger.info(
        "gs_config.cameras.kCamera%dDistortionVector=%s",
        camera_num,
        values[f"kCamera{camera_num}DistortionVector"],
    )
    logger.info(
        "gs_config.cameras.kCamera%dFocalLength=%s",
        camera_num,
        values[f"kCamera{camera_num}FocalLength"],
    )
    logger.info(
        "gs_config.cameras.kCamera%dAngles=%s",
        camera_num,
        values[f"kCamera{camera_num}Angles"],
    )
    logger.info(
        "gs_config.cameras.kExpectedBallRadiusPixelsAt40cmCamera%d=%s",
        camera_num,
        values[f"kExpectedBallRadiusPixelsAt40cmCamera{camera_num}"],
    )


def _log_startup_calibration_coverage(config: dict, camera_num: int) -> None:
    coverage = config_manager.get_startup_calibration_coverage(config, camera_num)
    logger.info(
        "Calibration startup coverage for camera %d (pitrac_lm fallback behavior):",
        camera_num,
    )
    for item in coverage:
        logger.info(
            "%s -> %s (fallback: %s)",
            item["key"],
            item["status"],
            item["fallback"],
        )


# Auto-capture settings
INTRINSIC_AUTO_CAPTURE_COUNT = 15
INTRINSIC_AUTO_CAPTURE_INTERVAL = 2.0  # seconds between captures
EXTRINSIC_AUTO_CAPTURE_COUNT = 6
EXTRINSIC_STROBED_AUTO_CAPTURE_INTERVAL = 5.0  # seconds between camera2 strobed captures


# ---------------------------------------------------------------------------
# State machine
# ---------------------------------------------------------------------------

class State(Enum):
    INTRINSIC_PREVIEW = auto()
    INTRINSIC_AUTO = auto()
    INTRINSIC_DONE = auto()
    EXTRINSIC_PREVIEW = auto()
    EXTRINSIC_AUTO = auto()
    EXTRINSIC_DONE = auto()
    FINISHED = auto()


def _build_intrinsic_hud(captures: int, corners_found: bool) -> list[str]:
    status = "CORNERS DETECTED" if corners_found else "no corners"
    return [
        f"INTRINSIC CALIBRATION  captures: {captures}",
        f"Status: {status}",
        "ENTER=start calibration  Q=quit",
    ]


def _build_intrinsic_auto_hud(
    captures: int, target: int, corners_found: bool,
) -> list[str]:
    status = "CORNERS DETECTED" if corners_found else "waiting for board..."
    return [
        f"AUTO-CAPTURING  {captures}/{target}",
        f"{status}  --  move board around slowly",
        "Q=cancel",
    ]


def _build_intrinsic_done_hud(rms: float, undistort_on: bool) -> list[str]:
    return [
        f"CALIBRATION COMPLETE  RMS={rms:.4f} px",
        f"Undistort preview: {'ON' if undistort_on else 'OFF'}",
        "U=undistort  S=save  Q=quit/next",
    ]


def _build_extrinsic_hud(samples: int, avg_focal: float, detected: bool) -> list[str]:
    det = "BALL DETECTED" if detected else "no ball"
    avg_str = f"{avg_focal:.4f}" if samples > 0 else "---"
    return [
        f"EXTRINSIC CALIBRATION  samples: {samples}  avg: {avg_str} mm",
        f"Status: {det}",
        "ENTER=start calibration  Q=quit",
    ]


def _build_extrinsic_auto_hud(
    samples: int,
    target: int,
    detected: bool,
    captures: int | None = None,
) -> list[str]:
    det = "BALL DETECTED" if detected else "waiting for ball..."
    if captures is not None:
        lines = [f"AUTO-CAPTURING  frames: {captures}/{target}  samples: {samples}/{target}"]
    else:
        lines = [f"AUTO-CAPTURING  {samples}/{target}"]
    lines.append(det)
    lines.append("Q=cancel")
    return lines


def _build_extrinsic_done_hud(focal: float, yaw: float, pitch: float) -> list[str]:
    return [
        "EXTRINSIC COMPLETE",
        f"Focal length: {focal:.4f} mm",
        f"Angles: yaw={yaw:.4f}  pitch={pitch:.4f} deg",
        "S=save  Q=quit",
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
    last_auto_capture: float = 0.0
    log_dir = _get_log_dir(camera_num, "intrinsic")

    while True:
        frame = source.capture()
        corners, ids = intrinsic.detect_corners(frame, detector)

        if state == State.INTRINSIC_PREVIEW:
            vis = frame.copy()
            if corners is not None:
                vis = display.draw_charuco_corners(vis, corners, ids)
            hud = _build_intrinsic_hud(len(all_corners), corners is not None)
            display.show_frame(WINDOW, vis, hud)

        elif state == State.INTRINSIC_AUTO:
            vis = frame.copy()
            if corners is not None:
                vis = display.draw_charuco_corners(vis, corners, ids)

            # Auto-capture every INTRINSIC_AUTO_CAPTURE_INTERVAL seconds
            # when corners are detected
            now = time.monotonic()
            if (corners is not None
                    and now - last_auto_capture >= INTRINSIC_AUTO_CAPTURE_INTERVAL):
                all_corners.append(corners)
                all_ids.append(ids)
                last_auto_capture = now
                _save_frame(frame, log_dir, "intrinsic", len(all_corners))
                logger.info(
                    "Auto-captured %d/%d (%d corners)",
                    len(all_corners), INTRINSIC_AUTO_CAPTURE_COUNT, len(corners),
                )

            hud = _build_intrinsic_auto_hud(
                len(all_corners), INTRINSIC_AUTO_CAPTURE_COUNT,
                corners is not None,
            )
            display.show_frame(WINDOW, vis, hud)

            # Auto-calibrate once we have enough
            if len(all_corners) >= INTRINSIC_AUTO_CAPTURE_COUNT:
                h, w = frame.shape[:2]
                try:
                    result = intrinsic.calibrate(
                        all_corners, all_ids, board, (w, h),
                    )
                    state = State.INTRINSIC_DONE
                    logger.info("Calibration done: RMS=%.4f", result.rms_error)
                except Exception as e:
                    logger.error("Calibration failed: %s", e)
                    state = State.INTRINSIC_PREVIEW

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
            if state == State.INTRINSIC_AUTO:
                # Cancel auto-capture, go back to preview
                state = State.INTRINSIC_PREVIEW
                all_corners.clear()
                all_ids.clear()
                logger.info("Auto-capture cancelled")
            else:
                break

        elif key == 13 and state == State.INTRINSIC_PREVIEW:  # ENTER
            # Start auto-capture mode
            all_corners.clear()
            all_ids.clear()
            last_auto_capture = 0.0
            state = State.INTRINSIC_AUTO
            logger.info(
                "Auto-capture started — move board around slowly "
                "(capturing %d frames, one every %.0fs)",
                INTRINSIC_AUTO_CAPTURE_COUNT,
                INTRINSIC_AUTO_CAPTURE_INTERVAL,
            )

        elif key == ord(" ") and state in (
            State.INTRINSIC_PREVIEW, State.INTRINSIC_AUTO,
        ):
            # Manual single capture (still works as before)
            if corners is not None:
                all_corners.append(corners)
                all_ids.append(ids)
                last_auto_capture = time.monotonic()
                logger.info(
                    "Captured frame %d (%d corners)",
                    len(all_corners), len(corners),
                )
            else:
                logger.warning("No corners detected — move the board into view")

        elif key == ord("u") and state == State.INTRINSIC_DONE:
            undistort_on = not undistort_on

        elif key == ord("s") and state == State.INTRINSIC_DONE and result is not None:
            config_manager.set_calibration_matrix(config, camera_num, result.camera_matrix)
            config_manager.set_distortion_vector(config, camera_num, result.dist_coeffs)
            backup = config_manager.save_config(config, config_path)
            logger.info("Saved intrinsic calibration to %s (backup: %s)", config_path, backup)
            _log_saved_calibration_values(
                config,
                camera_num,
                "Saved intrinsic calibration outcome:",
            )

        elif key == ord("g"):
            out = f"charuco_board_{constants.CHARUCO_COLS}x{constants.CHARUCO_ROWS}.png"
            intrinsic.generate_board_image(out)
            logger.info("Board image saved to %s", out)

        elif key == ord("r") and state in (
            State.INTRINSIC_PREVIEW, State.INTRINSIC_AUTO,
        ):
            all_corners.clear()
            all_ids.clear()
            state = State.INTRINSIC_PREVIEW
            logger.info("Reset all captures")

    return result


# ---------------------------------------------------------------------------
# Extrinsic calibration loop
# ---------------------------------------------------------------------------

def _detect_extrinsic_ball(frame: np.ndarray, is_strobed_cam2: bool) -> tuple[tuple[float, float], float] | None:
    if not is_strobed_cam2:
        return extrinsic.detect_ball(frame)

    # Camera 2 strobed/IR frames are often darker; use relaxed settings first.
    detection = extrinsic.detect_ball(
        frame,
        min_radius=15,
        max_radius=140,
        param1=70,
        param2=22,
        min_brightness=35,
        allow_dark_fallback=True,
    )
    if detection is not None:
        return detection

    # Retry with even more permissive Hough thresholds as a fallback.
    return extrinsic.detect_ball(
        frame,
        min_radius=12,
        max_radius=170,
        param1=50,
        param2=16,
        min_brightness=15,
        allow_dark_fallback=True,
    )


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

    if distance < 0.01:
        logger.error(
            "Ball distance is ~0 (position: %.3f, %.3f, %.3f). "
            "Check golf_sim_config.json — the calibration ball position "
            "values for camera %d and rig type are not set. "
            "Look for kAutoCalibrationBaselineBallPositionFrom* or "
            "kCustomCalibrationRigPositionFrom* keys under gs_config.calibration.",
            *ball_pos, camera_num,
        )
        return None

    focal_samples: list[float] = []
    final_yaw: float = 0.0
    final_pitch: float = 0.0
    final_focal: float = 0.0
    state = State.EXTRINSIC_PREVIEW
    last_detection: tuple[tuple[float, float], float] | None = None
    log_dir = _get_log_dir(camera_num, "extrinsic")
    is_strobed_cam2 = (
        camera_num == 2 and isinstance(source, camera.StrobedStillSource)
    )
    if is_strobed_cam2:
        source.set_output_template(log_dir / "log_cam2_cal.png")
    last_auto_strobed_capture_time: float | None = None
    auto_capture_frames = 0
    strobed_min_valid_samples = 3

    frame = np.zeros((constants.RESOLUTION_Y, constants.RESOLUTION_X, 3), dtype=np.uint8)
    detection: tuple[tuple[float, float], float] | None = None
    while True:
        # Camera 2 strobed mode:
        # 1) Never fire in preview (wait for ENTER/SPACE user action).
        # 2) During auto-capture, enforce a fixed delay between captures.
        should_capture = True
        if is_strobed_cam2:
            if state in (State.EXTRINSIC_PREVIEW, State.EXTRINSIC_DONE):
                should_capture = False
            elif state == State.EXTRINSIC_AUTO and last_auto_strobed_capture_time is not None:
                elapsed = time.monotonic() - last_auto_strobed_capture_time
                if elapsed < EXTRINSIC_STROBED_AUTO_CAPTURE_INTERVAL:
                    should_capture = False

        capture_performed = False
        if should_capture:
            try:
                frame = source.capture()
                capture_performed = True
                detection = _detect_extrinsic_ball(frame, is_strobed_cam2)
                last_detection = detection
                if is_strobed_cam2 and state == State.EXTRINSIC_AUTO:
                    auto_capture_frames += 1
                    last_auto_strobed_capture_time = time.monotonic()
            except Exception as e:
                logger.warning("Capture failed: %s", e)
                detection = None
        else:
            detection = None

        avg_focal = sum(focal_samples) / len(focal_samples) if focal_samples else 0.0

        if state == State.EXTRINSIC_PREVIEW:
            vis = frame.copy()
            if detection is not None:
                vis = display.draw_ball_detection(vis, detection[0], detection[1])
            if focal_samples:
                vis = display.draw_focal_length_info(
                    vis, focal_samples[-1], len(focal_samples), avg_focal,
                )
            hud_detected = detection is not None or is_strobed_cam2
            hud = _build_extrinsic_hud(len(focal_samples), avg_focal, hud_detected)
            display.show_frame(WINDOW, vis, hud)

        elif state == State.EXTRINSIC_AUTO:
            vis = frame.copy()
            if detection is not None:
                vis = display.draw_ball_detection(vis, detection[0], detection[1])

                # Auto-capture: ball is stationary, grab a sample each frame
                center, radius = detection
                focal = extrinsic.compute_focal_length(radius, distance)
                if constants.MIN_FOCAL_LENGTH_MM <= focal <= constants.MAX_FOCAL_LENGTH_MM:
                    focal_samples.append(focal)
                    avg_focal = sum(focal_samples) / len(focal_samples)
                    if not is_strobed_cam2:
                        _save_frame(frame, log_dir, "extrinsic", len(focal_samples))
                    logger.info(
                        "Auto-sample %d/%d: radius=%.1f px, focal=%.4f mm (avg=%.4f)",
                        len(focal_samples), EXTRINSIC_AUTO_CAPTURE_COUNT,
                        radius, focal, avg_focal,
                    )
                else:
                    logger.warning("Focal %.2f mm out of range, skipping", focal)
            elif is_strobed_cam2 and capture_performed:
                logger.warning(
                    "Auto-capture frame %d/%d has no detectable ball",
                    auto_capture_frames,
                    EXTRINSIC_AUTO_CAPTURE_COUNT,
                )

            if focal_samples:
                vis = display.draw_focal_length_info(
                    vis, focal_samples[-1], len(focal_samples),
                    sum(focal_samples) / len(focal_samples),
                )

            hud = _build_extrinsic_auto_hud(
                len(focal_samples), EXTRINSIC_AUTO_CAPTURE_COUNT,
                (detection is not None) or is_strobed_cam2,
                captures=auto_capture_frames if is_strobed_cam2 else None,
            )
            display.show_frame(WINDOW, vis, hud)

            # Non-strobed mode finalizes based on sample count.
            should_finalize = False
            if not is_strobed_cam2:
                should_finalize = len(focal_samples) >= EXTRINSIC_AUTO_CAPTURE_COUNT
            else:
                # Strobed camera2 mode: always stop after exactly 6 captures.
                if auto_capture_frames >= EXTRINSIC_AUTO_CAPTURE_COUNT:
                    if len(focal_samples) >= strobed_min_valid_samples:
                        should_finalize = True
                        if len(focal_samples) < EXTRINSIC_AUTO_CAPTURE_COUNT:
                            logger.warning(
                                "Using %d valid samples from %d captured frames for finalization",
                                len(focal_samples),
                                auto_capture_frames,
                            )
                    else:
                        logger.error(
                            "Captured %d/%d frames but only %d valid ball detections (need >= %d).",
                            auto_capture_frames,
                            EXTRINSIC_AUTO_CAPTURE_COUNT,
                            len(focal_samples),
                            strobed_min_valid_samples,
                        )
                        state = State.EXTRINSIC_PREVIEW
                        focal_samples.clear()
                        auto_capture_frames = 0
                        last_auto_strobed_capture_time = None

            if should_finalize:
                final_focal = sum(focal_samples) / len(focal_samples)

                if not (constants.MIN_FOCAL_LENGTH_MM <= final_focal <= constants.MAX_FOCAL_LENGTH_MM):
                    logger.error("Average focal %.2f mm out of valid range", final_focal)
                    state = State.EXTRINSIC_PREVIEW
                    focal_samples.clear()
                    auto_capture_frames = 0
                    last_auto_strobed_capture_time = None
                    continue

                if last_detection is None:
                    logger.warning("No ball in last frame for angle computation")
                    state = State.EXTRINSIC_PREVIEW
                    auto_capture_frames = 0
                    last_auto_strobed_capture_time = None
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
                    state = State.EXTRINSIC_PREVIEW
                    focal_samples.clear()
                    auto_capture_frames = 0
                    last_auto_strobed_capture_time = None

        elif state == State.EXTRINSIC_DONE:
            vis = frame.copy()
            if detection is not None:
                vis = display.draw_ball_detection(vis, detection[0], detection[1])
            hud = _build_extrinsic_done_hud(final_focal, final_yaw, final_pitch)
            display.show_frame(WINDOW, vis, hud)

        key = cv2.waitKey(30) & 0xFF

        if key == ord("q"):
            if state == State.EXTRINSIC_AUTO:
                # Cancel auto-capture, go back to preview
                state = State.EXTRINSIC_PREVIEW
                focal_samples.clear()
                auto_capture_frames = 0
                last_auto_strobed_capture_time = None
                logger.info("Auto-capture cancelled")
            else:
                break

        elif key == 13 and state == State.EXTRINSIC_PREVIEW:  # ENTER
            # Start auto-capture mode
            focal_samples.clear()
            auto_capture_frames = 0
            last_auto_strobed_capture_time = None
            state = State.EXTRINSIC_AUTO
            if is_strobed_cam2:
                logger.info(
                    "Auto-capture started — collecting %d frames (5s apart), requiring >=%d valid samples",
                    EXTRINSIC_AUTO_CAPTURE_COUNT,
                    strobed_min_valid_samples,
                )
            else:
                logger.info(
                    "Auto-capture started — collecting %d samples",
                    EXTRINSIC_AUTO_CAPTURE_COUNT,
                )

        elif key == ord(" ") and state in (
            State.EXTRINSIC_PREVIEW, State.EXTRINSIC_AUTO,
        ):
            # Manual single capture
            if is_strobed_cam2:
                try:
                    frame = source.capture()
                    detection = _detect_extrinsic_ball(frame, is_strobed_cam2)
                    last_detection = detection
                except Exception as e:
                    logger.warning("Capture failed: %s", e)
                    detection = None

            if detection is None:
                if camera_num != 2:
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

        elif key == ord("s") and state == State.EXTRINSIC_DONE:
            config_manager.set_focal_length(config, camera_num, final_focal)
            config_manager.set_camera_angles(config, camera_num, final_yaw, final_pitch)
            config_manager.set_expected_ball_radius_pixels_at_40cm(
                config,
                camera_num,
                final_focal,
            )
            backup = config_manager.save_config(config, config_path)
            logger.info("Saved extrinsic calibration to %s (backup: %s)", config_path, backup)
            _log_saved_calibration_values(
                config,
                camera_num,
                "Saved extrinsic calibration outcome:",
            )

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
    _log_startup_calibration_coverage(config, args.camera)

    cv2.namedWindow(WINDOW, cv2.WINDOW_NORMAL)

    try:
        if args.mode in ("intrinsic", "full"):
            try:
                intrinsic_source = camera.create_source(
                    camera_num=args.camera,
                    image_dir=args.image_dir,
                )
            except Exception as e:
                logger.error("Failed to create intrinsic camera source: %s", e)
                return 1

            logger.info("Starting intrinsic calibration (camera %d)", args.camera)
            try:
                run_intrinsic(intrinsic_source, args.camera, config, config_path)
            finally:
                intrinsic_source.release()

        if args.mode in ("extrinsic", "full"):
            use_strobe = args.image_dir is None and args.camera == 2
            try:
                extrinsic_source = camera.create_source(
                    camera_num=args.camera,
                    image_dir=args.image_dir,
                    use_strobe=use_strobe,
                    config_path=config_path,
                )
            except Exception as e:
                logger.error("Failed to create extrinsic camera source: %s", e)
                return 1

            if use_strobe:
                logger.info("Extrinsic mode using strobed still capture (camera 2).")
            else:
                logger.info("Extrinsic mode using live camera capture.")

            logger.info("Starting extrinsic calibration (camera %d)", args.camera)
            try:
                run_extrinsic(extrinsic_source, args.camera, config, config_path)
            finally:
                extrinsic_source.release()

    finally:
        cv2.destroyAllWindows()

    return 0


if __name__ == "__main__":
    sys.exit(main())
