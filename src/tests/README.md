# PiTrac Test Infrastructure

## Overview

This directory contains the test suite for the PiTrac launch monitor, built on **Boost.Test** framework integrated with **Meson** build system.

**Current Status:** ✅ Infrastructure established, sample tests created

**Coverage Goal:** 25% by end of Phase 2, 45% by end of Phase 4

---

## Directory Structure

```
src/tests/
├── README.md                    # This file
├── meson.build                  # Meson test configuration
├── test_utilities.hpp           # Shared test fixtures and helpers
│
├── unit/                        # Unit tests (fast, isolated)
│   ├── test_cv_utils.cpp       # ✅ CvUtils utility tests (SAMPLE)
│   ├── test_ball_detection.cpp # TODO: Ball detection algorithms
│   ├── test_fsm_transitions.cpp # TODO: State machine transitions
│   ├── test_calibration.cpp    # TODO: Calibration math
│   └── test_ipc_serialization.cpp # TODO: IPC message handling
│
├── integration/                 # Integration tests (multi-module)
│   ├── test_camera_ball_detection.cpp # TODO: Camera + ball detection
│   └── test_fsm_camera_integration.cpp # TODO: FSM + camera control
│
└── approval/                    # Approval/regression tests
    ├── approval_test_config.cpp # TODO: Port from ImageAnalysis
    ├── result_formatter.cpp
    ├── visualization_service.cpp
    ├── comparison_service.cpp
    ├── diff_launcher.cpp
    ├── approval_test_orchestrator.cpp
    └── test_ball_detection_images.cpp # TODO: Real image tests
```

---

## Running Tests

### All Tests
```bash
# From project root
meson test -C src/build --print-errorlogs
```

### Unit Tests Only
```bash
meson test -C src/build --suite unit --print-errorlogs
```

### Specific Test
```bash
# Run CvUtils tests
meson test -C src/build "CvUtils Unit Tests" --print-errorlogs

# Or run the executable directly
./src/build/test_cv_utils
```

### With Verbose Output
```bash
meson test -C src/build --verbose --print-errorlogs
```

### Parallel Execution
```bash
meson test -C src/build -j4  # Run 4 tests in parallel
```

---

## Writing Tests

### Basic Unit Test Pattern

```cpp
#define BOOST_TEST_MODULE MyModuleTests
#include <boost/test/unit_test.hpp>
#include "../test_utilities.hpp"
#include "my_module.h"

using namespace golf_sim;

BOOST_AUTO_TEST_SUITE(MyModuleTests)

BOOST_AUTO_TEST_CASE(BasicTest) {
    // Arrange
    int input = 42;

    // Act
    int result = MyFunction(input);

    // Assert
    BOOST_CHECK_EQUAL(result, 84);
}

BOOST_AUTO_TEST_SUITE_END()
```

### Using Fixtures

```cpp
struct MyTestFixture {
    MyTestFixture() {
        // Setup before each test
        obj = std::make_unique<MyClass>();
    }

    ~MyTestFixture() {
        // Cleanup after each test
    }

    std::unique_ptr<MyClass> obj;
};

BOOST_FIXTURE_TEST_CASE(TestWithFixture, MyTestFixture) {
    BOOST_CHECK(obj->IsValid());
}
```

### Using Test Utilities

```cpp
#include "../test_utilities.hpp"

// OpenCV testing
BOOST_FIXTURE_TEST_CASE(ImageTest, golf_sim::testing::OpenCVTestFixture) {
    // Load test image
    cv::Mat img = LoadTestImage("ball_test.png");

    // Create synthetic image
    cv::Mat synthetic = CreateSyntheticBallImage(640, 480);

    // Assert images nearly equal
    AssertImagesNearlyEqual(img, synthetic, 2.0);
}

// Timing tests
BOOST_FIXTURE_TEST_CASE(PerformanceTest, golf_sim::testing::TimingTestFixture) {
    // Code to time
    MyExpensiveOperation();

    // Assert completed within 100ms
    AssertCompletedWithin(std::chrono::milliseconds(100));
}
```

---

## Boost.Test Assertion Reference

### Basic Assertions
```cpp
BOOST_CHECK(condition);                // Warning if false
BOOST_REQUIRE(condition);              // Stop test if false
BOOST_CHECK_EQUAL(val1, val2);        // Check equality
BOOST_CHECK_NE(val1, val2);           // Check inequality
BOOST_CHECK_LT(val1, val2);           // Check less than
BOOST_CHECK_LE(val1, val2);           // Check less or equal
BOOST_CHECK_GT(val1, val2);           // Check greater than
BOOST_CHECK_GE(val1, val2);           // Check greater or equal
```

### Floating Point Assertions
```cpp
BOOST_CHECK_CLOSE(val1, val2, tolerance_percent);  // Check within percentage
BOOST_CHECK_SMALL(val, tolerance);                 // Check near zero
BOOST_CHECK_CLOSE_FRACTION(val1, val2, fraction);  // Check within fraction
```

### Collection Assertions
```cpp
BOOST_CHECK_EQUAL_COLLECTIONS(
    begin1, end1, begin2, end2);  // Check collections equal
```

### Exception Assertions
```cpp
BOOST_CHECK_THROW(expr, exception_type);      // Check throws specific exception
BOOST_CHECK_NO_THROW(expr);                   // Check doesn't throw
BOOST_CHECK_EXCEPTION(expr, exception_type, predicate);  // Check exception details
```

### Messages
```cpp
BOOST_CHECK_MESSAGE(condition, "Custom message");
BOOST_REQUIRE_MESSAGE(condition, "Custom message");
```

---

## Adding New Tests

### Step 1: Create Test File

Create `src/tests/unit/test_my_module.cpp`:

```cpp
#define BOOST_TEST_MODULE MyModuleTests
#include <boost/test/unit_test.hpp>
#include "../test_utilities.hpp"
#include "my_module.h"

BOOST_AUTO_TEST_SUITE(MyModuleTests)

BOOST_AUTO_TEST_CASE(FirstTest) {
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
```

### Step 2: Add to meson.build

Edit `src/tests/meson.build`:

```meson
test_my_module = executable('test_my_module',
    'unit/test_my_module.cpp',
    include_directories : test_include_dirs,
    link_with : [core_lib, utils_lib],  # Add needed libraries
    dependencies : [boost_test_dep, opencv_dep],
    build_by_default : true)

test('MyModule Unit Tests',
    test_my_module,
    suite : ['unit', 'core'],
    timeout : 30)
```

### Step 3: Build and Run

```bash
meson compile -C src/build
meson test -C src/build "MyModule Unit Tests" --print-errorlogs
```

---

## Test Suites and Labels

Tests are organized into suites for easy filtering:

### Unit Test Suites
- `unit` - All unit tests
- `utils` - Utility function tests
- `core` - Core logic tests
- `vision` - Vision/image processing tests

### Integration Test Suites
- `integration` - All integration tests
- `camera` - Camera integration tests
- `fsm` - State machine integration tests

### Approval Test Suites
- `approval` - All approval tests
- `regression` - Regression test images

### Run Specific Suite
```bash
meson test -C src/build --suite unit
meson test -C src/build --suite integration
meson test -C src/build --suite approval
```

---

## Test Data

### Location
Test data (images, config files, etc.) should be placed in:
```
/home/jesher/Code/Github/digitalhand/pitrac-light/test_data/
├── images/              # Test images
├── configs/             # Test configuration files
└── approval_artifacts/  # Approval test baselines
```

### Loading Test Data
Use `TestPaths` helper from `test_utilities.hpp`:

```cpp
#include "../test_utilities.hpp"
using namespace golf_sim::testing;

// Get test data directory
auto data_dir = TestPaths::GetTestDataDir();

// Load test image
auto img_path = TestPaths::GetTestImagesDir() / "ball_test.png";
cv::Mat img = cv::imread(img_path.string());

// Get approval artifacts directory
auto artifacts_dir = TestPaths::GetApprovalArtifactsDir();
```

---

## Approval Testing

Approval tests capture "golden" reference outputs and compare new runs against them.

### Pattern (from ImageAnalysis)

```cpp
#include "approval/approval_test_orchestrator.hpp"

BOOST_AUTO_TEST_CASE(BallDetectionApproval) {
    ApprovalTestOrchestrator orchestrator;

    // Load test image
    cv::Mat img = LoadTestImage("ball_shot_001.png");

    // Run ball detection
    auto result = DetectBall(img);

    // Compare against approved baseline
    orchestrator.ApproveResult(result, "ball_shot_001");
}
```

### Updating Baselines

When algorithm behavior intentionally changes:

1. Review new outputs carefully
2. Copy `.received.*` files to `.approved.*` in `test_data/approval_artifacts/`
3. Commit updated baselines with clear explanation

**⚠️ Never update baselines without review!**

---

## Coverage Targets and Strategy

### Phase 2 Target: 25% Coverage

**Priority Files (2,400+ lines to cover):**

1. **Ball Detection (`ball_image_proc.cpp` - 1,175 lines)**
   - Hough circle detection
   - ONNX model inference
   - Color filtering
   - ROI extraction

2. **State Machine (`gs_fsm.cpp` - 245 lines)**
   - State transitions
   - Timer handling
   - Ball stabilization

3. **Calibration (`gs_calibration.cpp` - 128 lines)**
   - Focal length averaging
   - Camera position calculations
   - Rig type selection

4. **IPC System (`gs_ipc_*.cpp` - ~500 lines)**
   - Message serialization
   - Mat image transmission
   - Queue management

### Phase 4 Target: 45% Coverage

Add coverage for:
- Camera control (`gs_camera.cpp`)
- Pulse strobe (`pulse_strobe.cpp`)
- Configuration management
- Simulator interfaces

### Measuring Coverage

```bash
# Build with coverage enabled
meson configure src/build -Db_coverage=true
meson compile -C src/build

# Run tests
meson test -C src/build

# Generate coverage report
ninja -C src/build coverage-html

# View report
firefox src/build/meson-logs/coveragereport/index.html
```

---

## Best Practices

### ✅ DO:
- Write tests BEFORE refactoring critical code
- Use descriptive test names: `BOOST_AUTO_TEST_CASE(CircleRadiusExtraction_ReturnsCorrectValue)`
- Test edge cases (null, zero, negative, max values)
- Use fixtures to share setup code
- Keep tests fast (<30s timeout)
- Run tests before committing

### ❌ DON'T:
- Don't test external libraries (OpenCV, Boost)
- Don't write tests with random behavior
- Don't depend on test execution order
- Don't hardcode absolute paths
- Don't commit commented-out tests

### Test Naming Convention
```cpp
// Format: <FunctionName>_<Scenario>_<ExpectedResult>
BOOST_AUTO_TEST_CASE(DistanceBetweenPoints_3_4_Triangle_Returns5) { ... }
BOOST_AUTO_TEST_CASE(ConfigLoad_MissingFile_ThrowsException) { ... }
BOOST_AUTO_TEST_CASE(BallDetection_WhiteBall_DetectsSuccessfully) { ... }
```

---

## Continuous Integration

Tests run automatically on:
- ✅ Every commit (via pre-commit hook)
- ✅ Pull request creation
- ✅ Merge to main branch

**CI Pipeline (TODO: Phase 4.2):**
```yaml
# .github/workflows/ci.yml
- name: Build and Test
  run: |
    meson setup build --buildtype=release
    ninja -C build
    meson test -C build --print-errorlogs
```

---

## Troubleshooting

### "Boost Test Framework not found"
```bash
# Install Boost with unit_test_framework
sudo apt install libboost-test-dev  # Debian/Ubuntu
```

### "Test timeout"
```bash
# Increase timeout in meson.build
test('MyTest', ..., timeout : 60)  # 60 seconds
```

### "Test data not found"
```bash
# Verify test_data directory exists
ls -la /home/jesher/Code/Github/digitalhand/pitrac-light/test_data/

# Create if missing
mkdir -p test_data/images
```

### "Linking errors"
- Ensure test links correct libraries in `meson.build`
- Check `link_with : [core_lib, utils_lib, ...]`

---

## References

- [Boost.Test Documentation](https://www.boost.org/doc/libs/1_74_0/libs/test/doc/html/index.html)
- [Meson Test Guide](https://mesonbuild.com/Unit-tests.html)
- [ImageAnalysis Tests](../ImageAnalysis/tests/) - Reference implementation
- [Camera Tests](../Camera/tests/) - Another reference

---

**Last Updated:** 2025-02-12
**Status:** Infrastructure complete, sample test added, ready for Phase 2.3 (write critical path tests)
