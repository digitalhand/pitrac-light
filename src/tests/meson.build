# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2022-2025, Verdant Consultants, LLC.
#

# =============================================================================
# Test Infrastructure for PiTrac Launch Monitor
# =============================================================================
# This file configures Boost.Test-based testing for the main build system.
# Pattern based on src/ImageAnalysis and src/Camera bounded contexts.

# Find Boost unit_test_framework
# Note: boost_dep is already defined in parent meson.build
boost_test_dep = dependency('boost',
    method: 'pkg-config',
    version : '>=1.74.0',
    modules : ['unit_test_framework'])

# Test include directories (includes parent src/ directory)
test_include_dirs = include_directories('.', '..')

# =============================================================================
# Unit Tests
# =============================================================================

# Test: CvUtils utility functions
test_cv_utils = executable('test_cv_utils',
    'unit/test_cv_utils.cpp',
    include_directories : test_include_dirs,
    link_with : [utils_lib],
    dependencies : [boost_test_dep, opencv_dep, boost_dep, fmt_dep],
    build_by_default : true)

test('CvUtils Unit Tests',
    test_cv_utils,
    suite : ['unit', 'utils'],
    timeout : 30)

# Test: FSM State Transitions
test_fsm_transitions = executable('test_fsm_transitions',
    'unit/test_fsm_transitions.cpp',
    include_directories : test_include_dirs,
    link_with : [core_lib, utils_lib],
    dependencies : [boost_test_dep, opencv_dep, boost_dep],
    build_by_default : true)

test('FSM Transition Tests',
    test_fsm_transitions,
    suite : ['unit', 'core', 'fsm'],
    timeout : 30)

# Test: Calibration Math and Logic
test_calibration = executable('test_calibration',
    'unit/test_calibration.cpp',
    include_directories : test_include_dirs,
    link_with : [core_lib, utils_lib],
    dependencies : [boost_test_dep, opencv_dep, boost_dep],
    build_by_default : true)

test('Calibration Tests',
    test_calibration,
    suite : ['unit', 'core', 'calibration'],
    timeout : 30)

# Test: IPC Message Serialization
test_ipc_serialization = executable('test_ipc_serialization',
    'unit/test_ipc_serialization.cpp',
    include_directories : test_include_dirs,
    link_with : [core_lib, utils_lib],
    dependencies : [boost_test_dep, opencv_dep, boost_dep, msgpack_dep],
    build_by_default : true)

test('IPC Serialization Tests',
    test_ipc_serialization,
    suite : ['unit', 'core', 'ipc'],
    timeout : 30)

# TODO: Add ball detection tests (requires refactoring of ball_image_proc.cpp)
# test_ball_detection = executable('test_ball_detection', ...)

# =============================================================================
# Integration Tests
# =============================================================================

# TODO: Add integration tests (multi-module interactions)
# test_camera_ball_detection = executable('test_camera_ball_detection', ...)
# test_fsm_camera_integration = executable('test_fsm_camera_integration', ...)

# =============================================================================
# Approval Tests
# =============================================================================

# TODO: Port approval test framework from src/ImageAnalysis/tests/approval/
# This will require creating approval test infrastructure in src/tests/approval/

# approval_framework_sources = [
#     'approval/approval_test_config.cpp',
#     'approval/result_formatter.cpp',
#     'approval/visualization_service.cpp',
#     'approval/comparison_service.cpp',
#     'approval/diff_launcher.cpp',
#     'approval/approval_test_orchestrator.cpp',
# ]

# test_ball_detection_approval = executable('test_ball_detection_approval',
#     'approval/test_ball_detection_images.cpp',
#     approval_framework_sources,
#     ...)

# =============================================================================
# Test Utilities and Helpers
# =============================================================================

# test_utilities.hpp is header-only, so no library needed
# It's automatically available via test_include_dirs

# =============================================================================
# Running Tests
# =============================================================================

# Run all tests:
#   meson test -C src/build --print-errorlogs
#
# Run only unit tests:
#   meson test -C src/build --suite unit --print-errorlogs
#
# Run specific suite:
#   meson test -C src/build --suite core --print-errorlogs
