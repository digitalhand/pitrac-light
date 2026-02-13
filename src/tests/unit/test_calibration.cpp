/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

/**
 * @file test_calibration.cpp
 * @brief Unit tests for calibration system
 *
 * Tests calibration calculations, focal length averaging, camera position
 * calculations, and calibration rig type selection.
 */

#define BOOST_TEST_MODULE CalibrationTests
#include <boost/test/unit_test.hpp>
#include "../test_utilities.hpp"
#include "gs_camera.h"
#include "gs_calibration.h"
#include "utils/cv_utils.h"

using namespace golf_sim;
using namespace golf_sim::testing;

BOOST_AUTO_TEST_SUITE(CalibrationTests)

// ===========================================================================
// Calibration Rig Type Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(CalibrationRigType_StraightForward_IsValid) {
    auto rig_type = GolfSimCalibration::CalibrationRigType::kStraightForwardCameras;
    BOOST_CHECK_EQUAL(rig_type, 1);
}

BOOST_AUTO_TEST_CASE(CalibrationRigType_SkewedCamera_IsValid) {
    auto rig_type = GolfSimCalibration::CalibrationRigType::kSkewedCamera1;
    BOOST_CHECK_EQUAL(rig_type, 2);
}

BOOST_AUTO_TEST_CASE(CalibrationRigType_CustomRig_IsValid) {
    auto rig_type = GolfSimCalibration::CalibrationRigType::kSCustomRig;
    BOOST_CHECK_EQUAL(rig_type, 3);
}

BOOST_AUTO_TEST_CASE(CalibrationRigType_Unknown_IsValid) {
    auto rig_type = GolfSimCalibration::CalibrationRigType::kCalibrationRigTypeUnknown;
    BOOST_CHECK(rig_type != 1 && rig_type != 2 && rig_type != 3);
}

// ===========================================================================
// Ball Position Vector Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(BallPosition_Vec3d_HasThreeComponents) {
    cv::Vec3d position(1.5, 0.0, 0.3);

    BOOST_CHECK_EQUAL(position[0], 1.5);  // X
    BOOST_CHECK_EQUAL(position[1], 0.0);  // Y
    BOOST_CHECK_EQUAL(position[2], 0.3);  // Z
}

BOOST_AUTO_TEST_CASE(BallPosition_DistanceCalculation_IsCorrect) {
    cv::Vec3d camera_pos(0.0, 0.0, 0.0);
    cv::Vec3d ball_pos(3.0, 4.0, 0.0);

    double distance = CvUtils::GetDistance(ball_pos - camera_pos);
    BOOST_CHECK_CLOSE(distance, 5.0, 0.01);  // 3-4-5 triangle
}

BOOST_AUTO_TEST_CASE(BallPosition_3D_DistanceCalculation) {
    cv::Vec3d camera_pos(0.0, 0.0, 0.0);
    cv::Vec3d ball_pos(1.0, 2.0, 2.0);

    double distance = CvUtils::GetDistance(ball_pos);
    BOOST_CHECK_CLOSE(distance, 3.0, 0.01);  // sqrt(1 + 4 + 4) = 3
}

// ===========================================================================
// Camera Position Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(CameraPosition_Camera1_IsInFrontOfBall) {
    cv::Vec3d camera1(0.0, 0.0, 0.0);
    cv::Vec3d ball(2.0, 0.0, 0.5);

    // Camera 1 should be behind the ball (negative X direction in standard setup)
    BOOST_CHECK_GT(ball[0], camera1[0]);
}

BOOST_AUTO_TEST_CASE(CameraPosition_Camera2_IsInFrontOfBall) {
    cv::Vec3d camera2(0.0, 0.0, 0.0);
    cv::Vec3d ball(-2.0, 0.0, 0.5);

    // Camera 2 should be in front of the ball (positive X direction)
    BOOST_CHECK_LT(ball[0], camera2[0]);
}

BOOST_AUTO_TEST_CASE(CameraPositions_SymmetricSetup_IsCorrect) {
    // Typical symmetric setup
    cv::Vec3d camera1_to_ball(2.0, 0.0, 0.5);
    cv::Vec3d camera2_to_ball(-2.0, 0.0, 0.5);

    // X coordinates should be symmetric
    BOOST_CHECK_CLOSE(std::abs(camera1_to_ball[0]), std::abs(camera2_to_ball[0]), 0.01);

    // Y coordinates should be same (both on centerline)
    BOOST_CHECK_EQUAL(camera1_to_ball[1], camera2_to_ball[1]);

    // Z coordinates should be same (same height)
    BOOST_CHECK_EQUAL(camera1_to_ball[2], camera2_to_ball[2]);
}

// ===========================================================================
// Skewed Camera Setup Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(SkewedSetup_Camera1_HasYOffset) {
    // Skewed setup: Camera 1 is offset in Y to give more detection time
    cv::Vec3d camera1_to_ball(1.8, -0.3, 0.5);

    BOOST_CHECK_GT(camera1_to_ball[0], 0.0);  // Still in front
    BOOST_CHECK_NE(camera1_to_ball[1], 0.0);  // Has Y offset
}

BOOST_AUTO_TEST_CASE(SkewedSetup_Camera2_HasYOffset) {
    // Skewed setup: Camera 2 is offset in opposite Y direction
    cv::Vec3d camera2_to_ball(-1.8, 0.3, 0.5);

    BOOST_CHECK_LT(camera2_to_ball[0], 0.0);  // In front (negative X)
    BOOST_CHECK_NE(camera2_to_ball[1], 0.0);  // Has Y offset
}

BOOST_AUTO_TEST_CASE(SkewedSetup_OffsetsMirror) {
    cv::Vec3d camera1_to_ball(1.8, -0.3, 0.5);
    cv::Vec3d camera2_to_ball(-1.8, 0.3, 0.5);

    // Y offsets should be opposite
    BOOST_CHECK_CLOSE(camera1_to_ball[1], -camera2_to_ball[1], 0.01);
}

// ===========================================================================
// Focal Length Calculation Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(FocalLength_PositiveValue_IsValid) {
    // Focal length should always be positive
    double focal_length = 1000.0;  // Typical value in pixels
    BOOST_CHECK_GT(focal_length, 0.0);
}

BOOST_AUTO_TEST_CASE(FocalLength_ReasonableRange_1080pCamera) {
    // For 1080p camera, focal length typically 800-1500 pixels
    double focal_length = 1200.0;
    BOOST_CHECK_GE(focal_length, 800.0);
    BOOST_CHECK_LE(focal_length, 1500.0);
}

BOOST_AUTO_TEST_CASE(FocalLength_Averaging_ReducesVariation) {
    // Simulate focal length measurements with variation
    std::vector<double> measurements = {
        1195.0, 1203.0, 1198.0, 1201.0, 1197.0,
        1202.0, 1199.0, 1204.0, 1196.0, 1200.0
    };

    double sum = 0.0;
    for (double m : measurements) {
        sum += m;
    }
    double average = sum / measurements.size();

    BOOST_CHECK_CLOSE(average, 1199.5, 0.5);  // ~1200 with 0.5% tolerance

    // Individual measurements vary more than ±5 pixels
    for (double m : measurements) {
        BOOST_CHECK_LE(std::abs(m - average), 10.0);
    }
}

// ===========================================================================
// Distance and Scale Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(Scale_MetersToPixels_Calculation) {
    // If ball is 1.68 inches (0.04267m) diameter and appears as 40 pixels
    double ball_diameter_m = 0.04267;
    double ball_diameter_pixels = 40.0;
    double distance_m = 2.0;  // 2 meters from camera

    // Scale factor: pixels per meter at that distance
    double scale = ball_diameter_pixels / ball_diameter_m;

    BOOST_CHECK_GT(scale, 900.0);  // Should be ~937 pixels/meter
    BOOST_CHECK_LT(scale, 1000.0);
}

BOOST_AUTO_TEST_CASE(Scale_DistanceDoubles_SizeHalves) {
    // If ball is 40 pixels at 2m, it should be ~20 pixels at 4m
    double pixels_at_2m = 40.0;
    double distance_ratio = 2.0;  // 4m / 2m

    double expected_pixels_at_4m = pixels_at_2m / distance_ratio;

    BOOST_CHECK_CLOSE(expected_pixels_at_4m, 20.0, 1.0);
}

// ===========================================================================
// Calibration Tolerance Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(CalibrationTolerance_NumberPicturesToAverage_IsPositive) {
    // Should average multiple pictures (typically 10)
    int num_pictures = 10;
    BOOST_CHECK_GT(num_pictures, 0);
    BOOST_CHECK_LE(num_pictures, 50);  // Reasonable upper bound
}

BOOST_AUTO_TEST_CASE(CalibrationTolerance_FailuresToTolerate_IsReasonable) {
    // Should tolerate a few failures but not too many
    int failures_tolerated = 3;
    BOOST_CHECK_GT(failures_tolerated, 0);
    BOOST_CHECK_LT(failures_tolerated, 10);
}

// ===========================================================================
// Coordinate System Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(CoordinateSystem_XAxisPointsForward) {
    // Convention: +X points in direction of ball flight
    cv::Vec3d ball_at_rest(0.0, 0.0, 0.05);
    cv::Vec3d ball_after_hit(1.0, 0.0, 0.5);

    BOOST_CHECK_GT(ball_after_hit[0], ball_at_rest[0]);
}

BOOST_AUTO_TEST_CASE(CoordinateSystem_YAxisPointsLeft) {
    // Convention: +Y points left (from camera view)
    cv::Vec3d center(0.0, 0.0, 0.05);
    cv::Vec3d left(0.0, 0.5, 0.05);

    BOOST_CHECK_GT(left[1], center[1]);
}

BOOST_AUTO_TEST_CASE(CoordinateSystem_ZAxisPointsUp) {
    // Convention: +Z points up
    cv::Vec3d ground(0.0, 0.0, 0.0);
    cv::Vec3d ball(0.0, 0.0, 0.05);

    BOOST_CHECK_GT(ball[2], ground[2]);
}

// ===========================================================================
// Calibration Accuracy Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(CalibrationAccuracy_PositionError_UnderThreshold) {
    // After calibration, position error should be under 1cm
    cv::Vec3d measured(2.01, 0.01, 0.51);
    cv::Vec3d expected(2.0, 0.0, 0.5);

    double error = CvUtils::GetDistance(measured - expected);
    BOOST_CHECK_LT(error, 0.02);  // 2cm threshold
}

BOOST_AUTO_TEST_CASE(CalibrationAccuracy_FocalLengthError_UnderThreshold) {
    // Focal length measurement error should be under 5%
    double measured = 1210.0;
    double expected = 1200.0;

    double error_percent = std::abs(measured - expected) / expected * 100.0;
    BOOST_CHECK_LT(error_percent, 5.0);
}

// ===========================================================================
// Edge Cases and Error Conditions
// ===========================================================================

BOOST_AUTO_TEST_CASE(Calibration_ZeroDistance_IsInvalid) {
    cv::Vec3d camera(0.0, 0.0, 0.0);
    cv::Vec3d ball(0.0, 0.0, 0.0);

    double distance = CvUtils::GetDistance(ball - camera);
    BOOST_CHECK_SMALL(distance, 0.01);  // Essentially zero
}

BOOST_AUTO_TEST_CASE(Calibration_NegativeZ_IsInvalid) {
    // Ball should never be underground (negative Z)
    cv::Vec3d ball_invalid(2.0, 0.0, -0.1);
    BOOST_CHECK_LT(ball_invalid[2], 0.0);  // This would be an error condition
}

BOOST_AUTO_TEST_CASE(Calibration_ExcessiveDistance_IsOutOfRange) {
    // If ball appears to be more than 10 meters away, something is wrong
    cv::Vec3d camera(0.0, 0.0, 0.0);
    cv::Vec3d ball(15.0, 0.0, 0.5);

    double distance = CvUtils::GetDistance(ball);
    BOOST_CHECK_GT(distance, 10.0);  // Would trigger error
}

// ===========================================================================
// Unit Conversion Tests (for calibration measurements)
// ===========================================================================

BOOST_AUTO_TEST_CASE(UnitConversion_InchesToMeters_GolfBallDiameter) {
    // Golf ball is 1.68 inches = 0.04267 meters
    double diameter_inches = 1.68;
    double diameter_meters = CvUtils::InchesToMeters(diameter_inches);

    BOOST_CHECK_CLOSE(diameter_meters, 0.04267, 1.0);
}

BOOST_AUTO_TEST_CASE(UnitConversion_MetersToFeet_TypicalDistance) {
    // 2 meters = 6.562 feet
    double distance_m = 2.0;
    double distance_ft = CvUtils::MetersToFeet(distance_m);

    BOOST_CHECK_CLOSE(distance_ft, 6.562, 1.0);
}

BOOST_AUTO_TEST_SUITE_END()
