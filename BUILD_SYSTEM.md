# PiTrac Build System Documentation

## Overview

PiTrac uses a **dual build system** strategy:
- **Meson** for the primary production build (`pitrac_lm` executable and integrated modules)
- **CMake** for bounded-context modules with isolated test suites

This document explains when to use each system, how they interact, and the migration strategy.

---

## Build System Summary

| Aspect | Meson Build | CMake Builds |
|--------|-------------|--------------|
| **Purpose** | Production executable | Bounded-context modules with tests |
| **Scope** | Main `pitrac_lm` binary + core/vision/sim/utils libraries | `Camera` and `ImageAnalysis` modules |
| **Test Runner** | `meson test` (limited) | `ctest` (comprehensive) |
| **C++ Standard** | C++20 | C++17 (to be unified to C++20) |
| **Build Tool** | `meson` + `ninja` | `cmake` + `make` |
| **Entry Point** | `src/meson.build` | `src/Camera/CMakeLists.txt`, `src/ImageAnalysis/CMakeLists.txt` |

---

## When to Use Meson

Use Meson for:

✅ **Building the production executable:**
```bash
cd src
meson setup build --buildtype=release --prefix=/opt/pitrac
ninja -C build -j4
sudo ninja -C build install
```

✅ **Adding new source files to core, vision, sim, or utils modules:**
- Edit `src/meson.build` and add your `.cpp` file to the appropriate source list:
  - `core_sources` - Core launch monitor logic (FSM, IPC, calibration)
  - `vision_sources` - Image processing and ball detection
  - `sim_sources` - Simulator integrations (GSPro, etc.)
  - `utils_sources` - Utility functions (via `src/utils/meson.build`)

✅ **Creating new subdirectory modules:**
1. Create `src/your_module/meson.build`
2. Add `subdir('your_module')` to `src/meson.build`
3. Define your static library:
   ```meson
   your_module_lib = static_library('your_module',
       sources: [...],
       include_directories: pitrac_lm_include_dirs,
       dependencies: pitrac_lm_module_deps)
   ```
4. Link it in the main executable (line 224 of `src/meson.build`)

✅ **Modifying build flags, dependencies, or compiler options:**
- Edit `src/meson.build` lines 16-30 (global arguments) or lines 67-82 (dependencies)

---

## When to Use CMake

Use CMake for:

✅ **Working on Camera module:**
```bash
cmake -S src/Camera -B src/Camera/build
cmake --build src/Camera/build
ctest --test-dir src/Camera/build --output-on-failure
```

✅ **Working on ImageAnalysis module:**
```bash
cmake -S src/ImageAnalysis -B src/ImageAnalysis/build \
    -DOPENCV_DIR=/path/to/opencv \
    -DBOOST_ROOT=/path/to/boost
cmake --build src/ImageAnalysis/build
ctest --test-dir src/ImageAnalysis/build --output-on-failure
```

✅ **Running comprehensive test suites for bounded contexts:**
- Camera module: Tests for libcamera integration, buffer management, hardware interfaces
- ImageAnalysis module: Tests for computer vision algorithms, including approval tests

✅ **Developing isolated, testable modules:**
- Bounded contexts with their own test suites should use CMake
- This allows independent development and testing without rebuilding the entire system

---

## Why Two Build Systems?

### Historical Context
- **Meson** was adopted for the main build to modernize from legacy Makefiles
- **CMake** was used for bounded contexts (Camera, ImageAnalysis) to leverage CTest and Boost.Test
- Both systems remain to support different testing strategies

### Current State Challenges
1. **Maintenance Overhead:** Changes to dependencies or compiler flags must be applied in both systems
2. **Standard Drift:** Meson uses C++20, CMake modules use C++17
3. **Inconsistent Testing:** Core logic lacks the test infrastructure that bounded contexts have
4. **Onboarding Friction:** New developers must learn both build systems

### Benefits of Dual System
✅ Bounded contexts remain independently testable
✅ Main build is fast and focused on production
✅ Allows gradual migration without disrupting active development

---

## Build System File Locations

### Meson Build Files
```
src/meson.build                     # Main build configuration
src/utils/meson.build               # Utilities library
src/core/meson.build                # Core rpicam_app components
src/encoder/meson.build             # Video encoding
src/image/meson.build               # Image format handling
src/output/meson.build              # Output streams
src/preview/meson.build             # Preview rendering
src/post_processing_stages/meson.build  # Post-processing pipeline
```

### CMake Build Files
```
src/Camera/CMakeLists.txt           # Camera bounded context
src/Camera/tests/CMakeLists.txt     # Camera tests
src/ImageAnalysis/CMakeLists.txt    # ImageAnalysis bounded context
src/ImageAnalysis/tests/CMakeLists.txt  # ImageAnalysis tests
```

---

## Adding New Code: Decision Tree

```
┌─────────────────────────────────────┐
│  Are you adding a new feature?      │
└───────────┬─────────────────────────┘
            │
            ├─── Is it core launch monitor logic (FSM, IPC, calibration)?
            │    └─→ Add to `core_sources` in src/meson.build
            │
            ├─── Is it image processing or ball detection?
            │    └─→ Add to `vision_sources` in src/meson.build
            │
            ├─── Is it simulator integration (GSPro, etc.)?
            │    └─→ Add to `sim_sources` in src/meson.build
            │
            ├─── Is it a reusable utility (math, conversion, helpers)?
            │    └─→ Add to src/utils/ and update src/utils/meson.build
            │
            ├─── Is it camera hardware abstraction?
            │    └─→ Add to src/Camera/ and update src/Camera/CMakeLists.txt
            │
            └─── Is it image analysis with comprehensive tests?
                 └─→ Add to src/ImageAnalysis/ and update src/ImageAnalysis/CMakeLists.txt
```

---

## Common Build Tasks

### Clean Build
```bash
# Meson
rm -rf src/build
meson setup src/build --buildtype=release
ninja -C src/build

# CMake (Camera)
rm -rf src/Camera/build
cmake -S src/Camera -B src/Camera/build
cmake --build src/Camera/build
```

### Debug Build
```bash
# Meson
meson setup src/build --buildtype=debug
ninja -C src/build

# CMake
cmake -S src/Camera -B src/Camera/build -DCMAKE_BUILD_TYPE=Debug
cmake --build src/Camera/build
```

### Run Tests
```bash
# Meson (limited tests currently)
meson test -C src/build --print-errorlogs

# CMake bounded contexts
ctest --test-dir src/Camera/build --output-on-failure
ctest --test-dir src/ImageAnalysis/build --output-on-failure
```

### Install (Meson only)
```bash
sudo ninja -C src/build install
# Installs to --prefix (default: /opt/pitrac)
```

---

## Migration Strategy (Planned)

### Phase 1: Unify C++ Standard (Target: Q2 2025)
- Update all CMake projects to C++20
- Verify compatibility with existing code
- Document in CLAUDE.md

### Phase 2: Add Test Infrastructure to Meson Build (Target: Q3 2025)
- Port Boost.Test framework patterns from bounded contexts
- Create `src/tests/` directory structure
- Enable `meson test` for core logic

### Phase 3: Evaluate Unified Build (Target: Q4 2025)
- Convert CMake bounded contexts to Meson subprojects
- Preserve test infrastructure during migration
- Maintain CMake as optional developer workflow
- Decision point: Keep dual system or fully unify?

### Success Criteria for Unification
- ✅ Single command builds entire project with tests
- ✅ All tests run via `meson test`
- ✅ No loss of test coverage
- ✅ Build time does not increase significantly
- ✅ Developer experience improved (simpler onboarding)

---

## Troubleshooting

### "Meson: command not found"
```bash
# Install Meson
sudo apt install meson ninja-build  # Debian/Ubuntu
pip3 install --user meson           # Via pip
```

### "CMake: could not find Boost"
```bash
# Specify Boost location
cmake -S src/Camera -B src/Camera/build -DBOOST_ROOT=/path/to/boost
```

### "undefined reference to libcamera symbols"
- Ensure libcamera-dev is installed: `sudo apt install libcamera-dev`
- Check that `libcamera_dep` is in `pitrac_lm_module_deps` (src/meson.build line 71)

### Build fails with "error: ..."
- Check that `werror=true` in `src/meson.build` line 10
- This converts warnings to errors - fix the underlying warning or add targeted suppression

---

## Key Configuration Details

### Compiler Flags (Meson)
- **Standard:** C++20 (`cpp_std=c++20` in project default_options)
- **Warning Level:** 2 (`warning_level=2`)
- **Errors on Warnings:** Yes (`werror=true`)
- **Targeted Suppressions:**
  - `-Wno-deprecated-enum-enum-conversion` (C++20 enum scoping)
  - `-Wno-deprecated-declarations` (legacy API usage)
  - `-Wno-comment` (documentation formatting)
  - `-Wno-unused` (intentional unused parameters)

### Dependencies (Meson)
- libcamera (camera control)
- OpenCV 4.9.0+ (computer vision)
- Boost 1.74.0+ (logging, threading, filesystem)
- ActiveMQ-CPP (IPC messaging)
- YAML-CPP (configuration)
- ONNX Runtime (neural network inference)
- fmt (string formatting)
- msgpack (serialization)

### ARM Optimizations (Pi 4/5)
- CPU target: Cortex-A76
- Vectorization enabled
- XNNPACK execution provider for ONNX Runtime

---

## References

- [Meson Build Documentation](https://mesonbuild.com/)
- [CMake Documentation](https://cmake.org/documentation/)
- [CLAUDE.md](./CLAUDE.md) - Project-specific build instructions and guidelines

---

## Questions or Issues?

- **Build System Drift:** If Meson and CMake builds diverge, update both systems and document changes
- **New Module Type:** If you're unsure which build system to use, consult the decision tree above
- **Migration Feedback:** As we progress through the migration phases, provide feedback on what's working

**Last Updated:** 2025-02-12
**Status:** Active dual build system, migration planning in progress
