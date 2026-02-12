# Configuration Migration Audit

## Executive Summary

**Current State:**
- **343 SetConstant() calls** across **14 files** (excluding gs_config.cpp/h)
- **17 configuration categories** identified
- **Anti-pattern:** Tight coupling between JSON config and static variables

**Target State:**
- Migrate to `ConfigurationManager` pattern (already used in bounded contexts)
- YAML-based configuration with runtime validation
- Dependency injection instead of static globals
- Testable configuration management

---

## Configuration Categories by Usage

| Category | Count | Primary Files | Priority |
|----------|-------|---------------|----------|
| **ball_identification** | 79 | ball_image_proc.cpp (116 total) | 🔴 High |
| **testing** | 57 | gs_automated_testing.cpp, lm_main.cpp | 🟡 Medium |
| **ball_exposure_selection** | 31 | gs_camera.cpp (73 total) | 🔴 High |
| **strobing** | 28 | pulse_strobe.cpp (26), gs_camera.cpp | 🟢 Low |
| **cameras** | 22 | gs_camera.cpp, libcamera_interface.cpp | 🔴 High |
| **spin_analysis** | 12 | ball_image_proc.cpp | 🟡 Medium |
| **calibration** | 12 | gs_calibration.cpp (9) | 🔴 High |
| **user_interface** | 11 | gs_camera.cpp, lm_main.cpp | 🟢 Low |
| **motion_detect_stage** | 11 | (post-processing) | 🟢 Low |
| **ball_position** | 11 | ball_image_proc.cpp | 🟡 Medium |
| **logging** | 10 | gs_camera.cpp | 🟢 Low |
| **golf_simulator_interfaces** | 9 | gs_gspro_interface.cpp | 🟢 Low |
| **club_data** | 8 | gs_club_data.cpp | 🟢 Low |
| **modes** | 2 | gs_camera.cpp | 🟢 Low |
| **ipc_interface** | 2 | gs_ipc_system.cpp | 🟢 Low |
| **image_capture** | 2 | libcamera_jpeg.cpp | 🟢 Low |
| **physical_constants** | 1 | gs_fsm.cpp | 🟢 Low |
| **TOTAL** | **343** | | |

---

## Files Requiring Migration

### High Priority (Core Detection Logic)

#### 1. `src/ball_image_proc.cpp` - 116 calls
**Categories:**
- `ball_identification.*` (79 calls) - Hough circle params, ONNX model config, CLAHE settings
- `spin_analysis.*` (12 calls) - Rotation analysis, Gabor filter params
- `ball_position.*` (11 calls) - Position detection algorithms
- `ball_exposure_selection.*` (partial)

**Impact:** Most critical - affects ball detection accuracy
**Estimated Effort:** 8-10 hours
**Target Config File:** `config/ball_detection.yaml`

**Sample Constants:**
```cpp
// ONNX Model Configuration
kONNXModelPath = "models/yolov8n_golf_ball.onnx"
kONNXConfidenceThreshold = 0.45
kONNXNMSThreshold = 0.5
kONNXInputSize = 640
kONNXBackend = "CPU"  // or "XNNPACK"
kONNXRuntimeThreads = 4

// Hough Circle Detection
kBestCircleParam1 = 100
kBestCircleParam2 = 30
kBestCircleCannyLower = 50
kBestCircleCannyUpper = 150
kBestCirclePreHoughBlurSize = 5
kBestCircleHoughDpParam1 = 1.0

// Spin Analysis
kCoarseXRotationDegreesStart = -15.0
kCoarseXRotationDegreesEnd = 15.0
kCoarseXRotationDegreesIncrement = 5.0
kGaborMinWhitePercent = 0.15
```

---

#### 2. `src/gs_camera.cpp` - 73 calls
**Categories:**
- `ball_exposure_selection.*` (31 calls) - Exposure quality, color difference thresholds
- `cameras.*` (22 calls) - Camera configuration, resolution, framerate
- `strobing.*` (partial) - Strobe timing
- `logging.*` (10 calls) - Image logging config
- `user_interface.*` (partial) - UI settings
- `modes.*` (2 calls) - Operating modes

**Impact:** High - affects image capture and exposure control
**Estimated Effort:** 6-8 hours
**Target Config File:** `config/camera_control.yaml`

**Sample Constants:**
```cpp
// Exposure Selection
kNumberHighQualityBallsToRetain = 3
kMaxStrobedBallColorDifferenceStrict = 20.0
kMaxStrobedBallColorDifferenceRelaxed = 35.0
kBallProximityMarginPercentStrict = 0.15
kMaximumOffTrajectoryDistance = 50.0

// Camera Configuration
kCamera1ResolutionX = 1456
kCamera1ResolutionY = 1088
kCamera1Framerate = 120
kCamera2ResolutionX = 1456
kCamera2ResolutionY = 1088
kCamera2Framerate = 120
kCamera1StrobeTimingUs = 100
kCamera2StrobeTimingUs = 150

// Logging
kLogIntermediateExposureImagesToFile = false
kLogWebserverImagesToFile = true
kLogDiagnosticImagesToUniqueFiles = false
```

---

#### 3. `src/gs_calibration.cpp` - 9 calls
**Categories:**
- `calibration.*` (12 calls total) - Calibration rig positions, focal length

**Impact:** High - affects coordinate system accuracy
**Estimated Effort:** 2-3 hours
**Target Config File:** `config/calibration.yaml`

**Sample Constants:**
```cpp
// Calibration
kNumberPicturesForFocalLengthAverage = 10
kNumberOfCalibrationFailuresToTolerate = 3
kCalibrationRigType = CalibrationRigType::kCustomRig
kCustomCalibrationRigPositionFromCamera1 = cv::Vec3d(1.5, 0.0, 0.3)
kCustomCalibrationRigPositionFromCamera2 = cv::Vec3d(-1.5, 0.0, 0.3)
kAutoCalibrationBaselineBallPositionFromCamera1Meters = cv::Vec3d(2.0, 0.0, 0.5)
```

---

### Medium Priority

#### 4. `src/pulse_strobe.cpp` - 26 calls
**Categories:**
- `strobing.*` (28 calls total) - GPIO pins, timing, pulse widths

**Impact:** Medium - affects strobe control
**Estimated Effort:** 3-4 hours
**Target Config File:** `config/camera_control.yaml` (strobing section)

#### 5. `src/gs_automated_testing.cpp` - 13 calls
**Categories:**
- `testing.*` (57 calls total) - Test injection, simulation modes

**Impact:** Low (testing only)
**Estimated Effort:** 2-3 hours
**Target Config File:** `config/testing.yaml`

#### 6. `src/lm_main.cpp` - 11 calls
**Categories:**
- `testing.*` (partial)
- `user_interface.*` (partial)

**Impact:** Medium
**Estimated Effort:** 2 hours
**Target Config File:** Multiple (testing.yaml, ui.yaml)

#### 7. `src/libcamera_interface.cpp` - 11 calls
**Categories:**
- `cameras.*` (partial) - Libcamera-specific settings

**Impact:** Medium
**Estimated Effort:** 2-3 hours
**Target Config File:** `config/camera_control.yaml`

---

### Low Priority

#### 8. `src/gs_club_data.cpp` - 8 calls
**Impact:** Low - club database config
**Target:** `config/club_data.yaml`

#### 9. `src/camera_hardware.cpp` - 5 calls
**Impact:** Low - hardware detection
**Target:** `config/camera_control.yaml`

#### 10. `src/sim/gspro/gs_gspro_interface.cpp` - 4 calls
**Impact:** Low - GSPro integration
**Target:** `config/simulator.yaml`

#### 11. `src/gs_fsm.cpp` - 4 calls
**Impact:** Low - FSM timing
**Target:** `config/fsm.yaml`

#### 12-14. Single-call files - 3 calls total
**Impact:** Very low
**Target:** Merge into existing configs

---

## Proposed YAML Configuration Structure

### `config/ball_detection.yaml`
```yaml
ball_detection:
  # ONNX Model Configuration
  onnx:
    model_path: "models/yolov8n_golf_ball.onnx"
    confidence_threshold: 0.45
    nms_threshold: 0.5
    input_size: 640
    backend: "XNNPACK"  # CPU, XNNPACK, CoreML
    runtime_threads: 4
    auto_fallback: true

  # Hough Circle Detection
  hough_circles:
    param1: 100
    param2: 30
    canny_lower: 50
    canny_upper: 150
    pre_hough_blur_size: 5
    dp_param: 1.0
    min_radius_ratio: 0.8
    max_radius_ratio: 1.2

  # CLAHE Preprocessing
  clahe:
    clip_limit: 2.0
    tiles_grid_size: 8

  # Detection Method
  method: "onnx"  # hough, onnx, hybrid
  placement_detection_method: "color_and_position"

  # Ball Position
  position:
    expected_ball_location_meters: [0.0, 0.0, 0.05]
    search_radius_pixels: 100
    dynamic_adjustment_enabled: true
    radii_to_average: 5

# Spin Analysis Configuration
spin_analysis:
  coarse_rotation:
    x:
      start_degrees: -15.0
      end_degrees: 15.0
      increment_degrees: 5.0
    y:
      start_degrees: -15.0
      end_degrees: 15.0
      increment_degrees: 5.0
    z:
      start_degrees: -180.0
      end_degrees: 180.0
      increment_degrees: 10.0

  gabor_filters:
    min_white_percent: 0.15
    filter_count: 8
```

### `config/camera_control.yaml`
```yaml
cameras:
  camera1:
    resolution:
      width: 1456
      height: 1088
    framerate: 120
    strobe_timing_us: 100
    exposure_us: 8000
    gain: 1.0

  camera2:
    resolution:
      width: 1456
      height: 1088
    framerate: 120
    strobe_timing_us: 150
    exposure_us: 8000
    gain: 1.0

# Exposure Selection
ball_exposure_selection:
  quality_retention:
    number_high_quality_balls: 3
    max_balls_to_retain: 10

  color_difference:
    strobed_strict: 20.0
    strobed_relaxed: 35.0
    putting_relaxed: 40.0
    rgb_multiplier_darker: 1.2
    rgb_multiplier_lighter: 0.8
    std_multiplier_darker: 1.5
    std_multiplier_lighter: 0.7

  geometry:
    ball_proximity_margin_percent_strict: 0.15
    ball_proximity_margin_percent_relaxed: 0.25
    maximum_off_trajectory_distance: 50.0
    max_radius_change_percent: 0.20
    closest_ball_pair_edge_backoff_pixels: 5

  launch_angle_constraints:
    min_quality_exposure_angle: 5.0
    max_quality_exposure_angle: 70.0
    min_putting_quality_angle: 0.0
    max_putting_quality_angle: 15.0

# Strobing Configuration
strobing:
  gpio:
    camera1_trigger_pin: 17
    camera2_trigger_pin: 27
    strobe1_pin: 22
    strobe2_pin: 23

  timing:
    pulse_width_us: 50
    inter_pulse_delay_us: 100
    camera1_delay_us: 100
    camera2_delay_us: 150

  calibration:
    enable_auto_adjustment: true
    target_brightness: 180
    max_adjustment_iterations: 5

# Logging Configuration
logging:
  images:
    log_intermediate_exposures: false
    log_webserver_images: true
    log_diagnostic_to_unique_files: false
    base_directory: "/var/log/pitrac/images/"
```

### `config/calibration.yaml`
```yaml
calibration:
  focal_length:
    number_pictures_for_average: 10
    number_failures_to_tolerate: 3

  rig_type: "custom"  # custom, auto_straight, auto_skewed

  # Custom Rig Positions (meters from camera, [x, y, z])
  custom_rig:
    position_from_camera1: [1.5, 0.0, 0.3]
    position_from_camera2: [-1.5, 0.0, 0.3]

  # Auto-Calibration Baselines
  auto_calibration:
    straight_cameras:
      baseline_ball_from_camera1: [2.0, 0.0, 0.5]
      baseline_ball_from_camera2: [-2.0, 0.0, 0.5]
    skewed_cameras:
      baseline_ball_from_camera1: [1.8, -0.3, 0.5]
      baseline_ball_from_camera2: [-1.8, 0.3, 0.5]
```

### `config/testing.yaml`
```yaml
testing:
  injection:
    enabled: false
    shot_data_file: "test_shots.json"
    inter_shot_pause_seconds: 5
    override_ball_detection: false

  simulation:
    mode: "normal"  # normal, replay, synthetic
    synthetic_ball_radius_pixels: 20
    synthetic_ball_color_rgb: [200, 200, 200]

  validation:
    strict_mode: false
    log_all_detections: true
    save_failure_images: true
```

---

## Migration Plan

### Phase 1: Infrastructure (Week 1)
1. ✅ Understand ConfigurationManager pattern from `src/ImageAnalysis/`
2. Create base YAML files (ball_detection.yaml, camera_control.yaml, calibration.yaml)
3. Extend ConfigurationManager to support new config groups
4. Add YAML validation schemas

### Phase 2: High Priority Migrations (Weeks 2-4)

#### Week 2: Ball Detection (ball_image_proc.cpp)
- Migrate 116 SetConstant() calls
- Create BallDetectionConfig struct
- Update ball_image_proc.cpp to use ConfigurationManager
- **Testing:** Verify ball detection accuracy unchanged (approval tests)

#### Week 3: Camera Control (gs_camera.cpp)
- Migrate 73 SetConstant() calls
- Create CameraConfig struct
- Update gs_camera.cpp to use ConfigurationManager
- **Testing:** Verify exposure selection logic

#### Week 4: Calibration (gs_calibration.cpp)
- Migrate 9 SetConstant() calls
- Create CalibrationConfig struct
- Update calibration logic
- **Testing:** Verify calibration accuracy

### Phase 3: Medium Priority (Weeks 5-6)
- pulse_strobe.cpp (26 calls)
- gs_automated_testing.cpp (13 calls)
- lm_main.cpp (11 calls)
- libcamera_interface.cpp (11 calls)

### Phase 4: Low Priority (Week 7)
- Remaining 34 calls across 8 files
- Consolidate configs
- Remove gs_config.cpp/h (legacy system)

### Phase 5: Cleanup (Week 8)
- Delete gs_config.json (replaced by YAML)
- Remove all SetConstant() calls
- Update documentation
- Run full test suite

---

## Benefits of Migration

### Before (Current State)
```cpp
// In ball_image_proc.cpp (constructor or static initializer)
GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXConfidenceThreshold",
                                   kONNXConfidenceThreshold);
// kONNXConfidenceThreshold is a static member variable
```

**Problems:**
- ❌ Tight coupling to static variables
- ❌ No runtime validation
- ❌ Hard to test (can't inject config)
- ❌ Changes require recompilation
- ❌ No type safety (string keys)
- ❌ 356 coupling points

### After (Target State)
```cpp
// In ball_image_proc.cpp (constructor with dependency injection)
BallImageProc::BallImageProc(const BallDetectionConfig& config)
    : onnx_confidence_threshold_(config.onnx.confidence_threshold)
    , hough_param1_(config.hough_circles.param1)
{
    // Validate config at construction time
    if (onnx_confidence_threshold_ < 0.0 || onnx_confidence_threshold_ > 1.0) {
        throw std::invalid_argument("ONNX confidence threshold must be 0.0-1.0");
    }
}

// In main.cpp (application startup)
auto config_mgr = ConfigurationManager::Load("config/ball_detection.yaml");
auto ball_detector = std::make_unique<BallImageProc>(config_mgr.ball_detection);
```

**Benefits:**
- ✅ Dependency injection (testable)
- ✅ Runtime validation with clear errors
- ✅ Type-safe configuration structs
- ✅ Hot-reload capability (YAML can be reloaded)
- ✅ No recompilation for config changes
- ✅ Zero coupling points (clean architecture)

---

## Risk Mitigation

### High Risk: Breaking Ball Detection
**Mitigation:**
1. ✅ Write approval tests BEFORE migration (capture current behavior)
2. ✅ Migrate one category at a time
3. ✅ Keep old system parallel during migration (feature flag)
4. ✅ A/B test: run both systems and compare results
5. ✅ Validation: 100 successful shots before declaring success

### Medium Risk: Config File Errors
**Mitigation:**
1. ✅ YAML schema validation on load
2. ✅ Default values for all parameters
3. ✅ Clear error messages with line numbers
4. ✅ Config validation tool (CLI): `pitrac_lm --validate-config`

### Low Risk: Performance Regression
**Mitigation:**
1. ✅ Benchmark config loading time (should be <10ms)
2. ✅ Cache parsed config (don't reload every shot)
3. ✅ Profile hot paths to ensure no overhead

---

## Success Metrics

| Metric | Current | Target | Verification |
|--------|---------|--------|--------------|
| SetConstant() calls | 343 | 0 | `grep -r "SetConstant" src/` returns 0 |
| Config files | 1 (gs_config.json) | 5 (YAML files) | All configs in `config/` directory |
| Config reload time | N/A (static) | <10ms | Benchmark on Pi 5 |
| Ball detection accuracy | Baseline | ±0% | Approval tests pass |
| Calibration accuracy | Baseline | ±0% | Calibration validation |
| Test coverage | 9.7% | 25% | Add config injection tests |

---

## Appendix: Full File Breakdown

### Complete List of Files with SetConstant() Calls

```
116  src/ball_image_proc.cpp
 73  src/gs_camera.cpp
 26  src/pulse_strobe.cpp
 13  src/gs_automated_testing.cpp
 11  src/lm_main.cpp
 11  src/libcamera_interface.cpp
  9  src/gs_calibration.cpp
  8  src/gs_club_data.cpp
  5  src/camera_hardware.cpp
  4  src/sim/gspro/gs_gspro_interface.cpp
  4  src/gs_fsm.cpp
  1  src/sim/common/gs_sim_interface.cpp
  1  src/libcamera_jpeg.cpp
  1  src/gs_ipc_system.cpp
---
343  TOTAL
```

### Configuration Category Reference

```
79  ball_identification      → config/ball_detection.yaml
57  testing                  → config/testing.yaml
31  ball_exposure_selection  → config/camera_control.yaml
28  strobing                 → config/camera_control.yaml
22  cameras                  → config/camera_control.yaml
12  spin_analysis            → config/ball_detection.yaml
12  calibration              → config/calibration.yaml
11  user_interface           → config/ui.yaml
11  motion_detect_stage      → config/post_processing.yaml
11  ball_position            → config/ball_detection.yaml
10  logging                  → config/camera_control.yaml
 9  golf_simulator_interfaces → config/simulator.yaml
 8  club_data                → config/club_data.yaml
 2  modes                    → config/camera_control.yaml
 2  ipc_interface            → config/ipc.yaml
 2  image_capture            → config/camera_control.yaml
 1  physical_constants       → config/physics.yaml
```

---

**Document Version:** 1.0
**Last Updated:** 2025-02-12
**Status:** Audit Complete - Ready for Phase 2 Migration
**Next Action:** Create YAML config files and begin ball_image_proc.cpp migration
