/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_cv_utils.cpp
 * @brief Unit tests for CvUtils utility functions
 *
 * Tests OpenCV helper methods, coordinate conversions, and geometry utilities.
 */

#define BOOST_TEST_MODULE CvUtilsTests
#include <boost/test/unit_test.hpp>
#include "../test_utilities.hpp"
#include "utils/cv_utils.h"

using namespace golf_sim;
using namespace golf_sim::testing;

BOOST_AUTO_TEST_SUITE(CvUtilsTests)

// Test fixture for CvUtils tests
struct CvUtilsTestFixture : public OpenCVTestFixture {
    CvUtilsTestFixture() {
        test_circle = GsCircle(100.0f, 200.0f, 25.0f);
    }

    GsCircle test_circle;
};

// ============================================================================
// Circle Utility Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(CircleRadiusExtraction, CvUtilsTestFixture) {
    double radius = CvUtils::CircleRadius(test_circle);
    BOOST_CHECK_EQUAL(radius, 25.0);
}

BOOST_FIXTURE_TEST_CASE(CircleXYExtraction, CvUtilsTestFixture) {
    cv::Vec2i xy = CvUtils::CircleXY(test_circle);
    BOOST_CHECK_EQUAL(xy[0], 100);
    BOOST_CHECK_EQUAL(xy[1], 200);
}

BOOST_FIXTURE_TEST_CASE(CircleXExtraction, CvUtilsTestFixture) {
    int x = CvUtils::CircleX(test_circle);
    BOOST_CHECK_EQUAL(x, 100);
}

BOOST_FIXTURE_TEST_CASE(CircleYExtraction, CvUtilsTestFixture) {
    int y = CvUtils::CircleY(test_circle);
    BOOST_CHECK_EQUAL(y, 200);
}

// ============================================================================
// Image Size Tests
// ============================================================================

BOOST_FIXTURE_TEST_CASE(ImageDimensionsExtraction, CvUtilsTestFixture) {
    cv::Mat test_img = CreateSyntheticBallImage(640, 480);

    int width = CvUtils::CvWidth(test_img);
    int height = CvUtils::CvHeight(test_img);
    cv::Vec2i size = CvUtils::CvSize(test_img);

    BOOST_CHECK_EQUAL(width, 640);
    BOOST_CHECK_EQUAL(height, 480);
    BOOST_CHECK_EQUAL(size[0], 640);
    BOOST_CHECK_EQUAL(size[1], 480);
}

// ============================================================================
// Rounding and Even Number Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(VectorRounding) {
    cv::Vec3f vec(1.4f, 2.6f, 3.1f);
    cv::Vec3f rounded = CvUtils::Round(vec);

    BOOST_CHECK_EQUAL(rounded[0], 1.0f);
    BOOST_CHECK_EQUAL(rounded[1], 3.0f);
    BOOST_CHECK_EQUAL(rounded[2], 3.0f);
}

BOOST_AUTO_TEST_CASE(MakeEven_OddNumber) {
    int value = 5;
    CvUtils::MakeEven(value);
    BOOST_CHECK_EQUAL(value, 6);
}

BOOST_AUTO_TEST_CASE(MakeEven_EvenNumber) {
    int value = 8;
    CvUtils::MakeEven(value);
    BOOST_CHECK_EQUAL(value, 8);
}

BOOST_AUTO_TEST_CASE(RoundAndMakeEven_Double) {
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(7.3), 8);
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(7.7), 8);
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(8.0), 8);
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(8.5), 8);
}

BOOST_AUTO_TEST_CASE(RoundAndMakeEven_Int) {
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(7), 8);
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(8), 8);
    BOOST_CHECK_EQUAL(CvUtils::RoundAndMakeEven(9), 10);
}

// ============================================================================
// Angle Conversion Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DegreesToRadians) {
    BOOST_CHECK_CLOSE(CvUtils::DegreesToRadians(0.0), 0.0, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::DegreesToRadians(90.0), CV_PI / 2.0, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::DegreesToRadians(180.0), CV_PI, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::DegreesToRadians(360.0), 2.0 * CV_PI, 0.01);
}

BOOST_AUTO_TEST_CASE(RadiansToDegrees) {
    BOOST_CHECK_CLOSE(CvUtils::RadiansToDegrees(0.0), 0.0, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::RadiansToDegrees(CV_PI / 2.0), 90.0, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::RadiansToDegrees(CV_PI), 180.0, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::RadiansToDegrees(2.0 * CV_PI), 360.0, 0.01);
}

// ============================================================================
// Unit Conversion Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(MetersToFeet) {
    BOOST_CHECK_CLOSE(CvUtils::MetersToFeet(1.0), 3.281, 0.1);
    BOOST_CHECK_CLOSE(CvUtils::MetersToFeet(10.0), 32.81, 0.1);
}

BOOST_AUTO_TEST_CASE(MetersToInches) {
    BOOST_CHECK_CLOSE(CvUtils::MetersToInches(1.0), 39.37, 0.1);
    BOOST_CHECK_CLOSE(CvUtils::MetersToInches(0.0254), 1.0, 0.1);
}

BOOST_AUTO_TEST_CASE(InchesToMeters) {
    BOOST_CHECK_CLOSE(CvUtils::InchesToMeters(1.0), 0.0254, 0.01);
    BOOST_CHECK_CLOSE(CvUtils::InchesToMeters(39.37), 1.0, 0.1);
}

BOOST_AUTO_TEST_CASE(MetersPerSecondToMPH) {
    BOOST_CHECK_CLOSE(CvUtils::MetersPerSecondToMPH(1.0), 2.237, 0.1);
    BOOST_CHECK_CLOSE(CvUtils::MetersPerSecondToMPH(44.7), 100.0, 0.5);
}

BOOST_AUTO_TEST_CASE(MetersToYards) {
    BOOST_CHECK_CLOSE(CvUtils::MetersToYards(1.0), 1.094, 0.1);
    BOOST_CHECK_CLOSE(CvUtils::MetersToYards(100.0), 109.4, 0.5);
}

// ============================================================================
// Distance Calculation Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(DistanceFromOrigin) {
    cv::Vec3d location(3.0, 4.0, 0.0);
    double distance = CvUtils::GetDistance(location);
    BOOST_CHECK_CLOSE(distance, 5.0, 0.01);  // 3-4-5 triangle
}

BOOST_AUTO_TEST_CASE(Distance3D) {
    cv::Vec3d location(1.0, 2.0, 2.0);
    double distance = CvUtils::GetDistance(location);
    BOOST_CHECK_CLOSE(distance, 3.0, 0.01);  // sqrt(1 + 4 + 4) = 3
}

BOOST_AUTO_TEST_CASE(DistanceBetweenPoints) {
    cv::Point p1(0, 0);
    cv::Point p2(3, 4);
    double distance = CvUtils::GetDistance(p1, p2);
    BOOST_CHECK_CLOSE(distance, 5.0, 0.01);  // 3-4-5 triangle
}

// ============================================================================
// Color Comparison Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(ColorDistance) {
    GsColorTriplet color1(100.0f, 150.0f, 200.0f);
    GsColorTriplet color2(100.0f, 150.0f, 200.0f);
    GsColorTriplet color3(110.0f, 160.0f, 210.0f);

    float dist_same = CvUtils::ColorDistance(color1, color2);
    float dist_different = CvUtils::ColorDistance(color1, color3);

    BOOST_CHECK_SMALL(dist_same, 0.01f);
    BOOST_CHECK_CLOSE(dist_different, 17.32f, 1.0f);  // sqrt(100 + 100 + 100)
}

BOOST_AUTO_TEST_CASE(IsDarker_Comparison) {
    GsColorTriplet dark(50.0f, 50.0f, 50.0f);
    GsColorTriplet bright(200.0f, 200.0f, 200.0f);

    BOOST_CHECK(CvUtils::IsDarker(dark, bright));
    BOOST_CHECK(!CvUtils::IsDarker(bright, dark));
}

// ============================================================================
// Upright Rectangle Detection Tests
// ============================================================================

BOOST_AUTO_TEST_CASE(IsUprightRect_NearZero) {
    BOOST_CHECK(CvUtils::IsUprightRect(0.0f));
    BOOST_CHECK(CvUtils::IsUprightRect(5.0f));
    BOOST_CHECK(CvUtils::IsUprightRect(-5.0f));
}

BOOST_AUTO_TEST_CASE(IsUprightRect_Near90) {
    BOOST_CHECK(CvUtils::IsUprightRect(90.0f));
    BOOST_CHECK(CvUtils::IsUprightRect(85.0f));
    BOOST_CHECK(CvUtils::IsUprightRect(95.0f));
}

BOOST_AUTO_TEST_CASE(IsUprightRect_Diagonal) {
    BOOST_CHECK(!CvUtils::IsUprightRect(45.0f));
    BOOST_CHECK(!CvUtils::IsUprightRect(135.0f));
}

BOOST_AUTO_TEST_SUITE_END()
