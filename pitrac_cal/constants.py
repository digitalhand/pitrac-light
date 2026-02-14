"""Hardware constants for PiTrac cameras.

Values sourced from src/camera_hardware.cpp:245-255 (PiGS / InnoMaker IMX296GS).
"""

# Sensor physical dimensions (mm) — camera_hardware.cpp:245-246
SENSOR_WIDTH_MM = 5.077365371
SENSOR_HEIGHT_MM = 3.789078635

# Default sensor resolution (pixels) — camera_hardware.cpp:254-255
RESOLUTION_X = 1456
RESOLUTION_Y = 1088

# Golf ball radius (meters) — standard USGA ball diameter 42.67mm
BALL_RADIUS_M = 0.021335

# Focal length sanity bounds (mm) — gs_calibration.cpp:428-429
MIN_FOCAL_LENGTH_MM = 2.0
MAX_FOCAL_LENGTH_MM = 50.0

# Camera angle sanity bound (degrees) — gs_calibration.cpp:264
MAX_REASONABLE_ANGLE_DEG = 45.0

# Calibration rig types — gs_calibration.h:36-41
# enum CalibrationRigType { kStraightForwardCameras=1, kSkewedCamera1=2, kSCustomRig=3 }
RIG_STRAIGHT_FORWARD = 1
RIG_SKEWED_CAMERA1 = 2
RIG_CUSTOM = 3

# CharucoBoard defaults
CHARUCO_COLS = 7
CHARUCO_ROWS = 5
CHARUCO_SQUARE_MM = 30.0
CHARUCO_MARKER_MM = 22.5
CHARUCO_DICT = "DICT_4X4_50"
