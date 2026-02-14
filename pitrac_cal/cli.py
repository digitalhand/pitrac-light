"""Command-line argument parsing for pitrac-cal."""

import argparse
from pathlib import Path


def parse_args(argv=None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="pitrac-cal",
        description="PiTrac camera calibration GUI (intrinsic + extrinsic)",
    )

    parser.add_argument(
        "--camera",
        type=int,
        choices=[1, 2],
        default=1,
        help="Camera number (1 or 2)",
    )
    parser.add_argument(
        "--mode",
        choices=["intrinsic", "extrinsic", "full"],
        default="full",
        help="Calibration mode: intrinsic (CharucoBoard), extrinsic (ball), or full (both)",
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=None,
        help="Path to golf_sim_config.json (auto-detected from PITRAC_ROOT if omitted)",
    )
    parser.add_argument(
        "--image-dir",
        type=Path,
        default=None,
        help="Load images from directory instead of live camera (for off-Pi testing)",
    )
    parser.add_argument(
        "--generate-board",
        type=Path,
        default=None,
        metavar="OUTPUT.png",
        help="Generate a printable CharucoBoard image and exit",
    )

    return parser.parse_args(argv)
