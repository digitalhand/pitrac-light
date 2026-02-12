/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

/**
 * @file test_utilities.hpp
 * @brief Shared test utilities and fixtures for PiTrac launch monitor tests
 *
 * This file provides common test infrastructure used across unit, integration,
 * and approval tests. It includes test fixtures, helper functions, and
 * assertion utilities that follow Boost.Test patterns.
 */

#pragma once

#include <boost/test/unit_test.hpp>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <memory>

namespace golf_sim {
namespace testing {

/**
 * @brief Test data directory paths
 */
struct TestPaths {
    static std::filesystem::path GetTestDataDir() {
        // Look for test data relative to project root
        auto current = std::filesystem::current_path();
        while (current.has_parent_path()) {
            auto test_data = current / "test_data";
            if (std::filesystem::exists(test_data)) {
                return test_data;
            }
            current = current.parent_path();
        }
        return std::filesystem::path("test_data");
    }

    static std::filesystem::path GetTestImagesDir() {
        return GetTestDataDir() / "images";
    }

    static std::filesystem::path GetApprovalArtifactsDir() {
        return GetTestDataDir() / "approval_artifacts";
    }
};

/**
 * @brief Base fixture for tests requiring OpenCV setup
 */
struct OpenCVTestFixture {
    OpenCVTestFixture() {
        // Suppress OpenCV warnings during tests
        // Note: cv::setLogLevel not available in all OpenCV versions
        // cv::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
    }

    ~OpenCVTestFixture() {
        // Restore default logging
        // cv::setLogLevel(cv::utils::logging::LOG_LEVEL_INFO);
    }

    /**
     * @brief Load a test image from test_data/images/
     */
    cv::Mat LoadTestImage(const std::string& filename) const {
        auto path = TestPaths::GetTestImagesDir() / filename;
        cv::Mat img = cv::imread(path.string(), cv::IMREAD_COLOR);
        BOOST_REQUIRE_MESSAGE(!img.empty(),
            "Failed to load test image: " + path.string());
        return img;
    }

    /**
     * @brief Create a synthetic test image with a circle
     */
    cv::Mat CreateSyntheticBallImage(
        int width = 640,
        int height = 480,
        cv::Point center = cv::Point(320, 240),
        int radius = 20,
        cv::Scalar ball_color = cv::Scalar(200, 200, 200),
        cv::Scalar background_color = cv::Scalar(50, 50, 50)) const
    {
        cv::Mat img(height, width, CV_8UC3, background_color);
        cv::circle(img, center, radius, ball_color, -1);  // Filled circle
        // Add slight blur to make it more realistic
        cv::GaussianBlur(img, img, cv::Size(3, 3), 0);
        return img;
    }

    /**
     * @brief Assert two images are nearly equal (allowing for small differences)
     */
    void AssertImagesNearlyEqual(
        const cv::Mat& img1,
        const cv::Mat& img2,
        double max_mean_diff = 1.0) const
    {
        BOOST_REQUIRE_EQUAL(img1.rows, img2.rows);
        BOOST_REQUIRE_EQUAL(img1.cols, img2.cols);
        BOOST_REQUIRE_EQUAL(img1.type(), img2.type());

        cv::Mat diff;
        cv::absdiff(img1, img2, diff);
        cv::Scalar mean_diff = cv::mean(diff);
        double overall_mean = (mean_diff[0] + mean_diff[1] + mean_diff[2]) / 3.0;

        BOOST_CHECK_LT(overall_mean, max_mean_diff);
    }
};

/**
 * @brief Fixture for tests requiring timing measurements
 */
struct TimingTestFixture {
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::microseconds;

    TimingTestFixture() : start_time(Clock::now()) {}

    /**
     * @brief Get elapsed time since fixture construction
     */
    Duration GetElapsedTime() const {
        return std::chrono::duration_cast<Duration>(
            Clock::now() - start_time
        );
    }

    /**
     * @brief Assert that an operation completed within a time limit
     */
    void AssertCompletedWithin(Duration max_duration) const {
        auto elapsed = GetElapsedTime();
        BOOST_CHECK_LE(elapsed.count(), max_duration.count());
    }

    /**
     * @brief Reset the timing reference point
     */
    void Reset() {
        start_time = Clock::now();
    }

private:
    TimePoint start_time;
};

/**
 * @brief Helper to create temporary files for testing
 */
class TempFileHelper {
public:
    TempFileHelper() {
        temp_dir = std::filesystem::temp_directory_path() /
                   ("pitrac_test_" + std::to_string(std::rand()));
        std::filesystem::create_directories(temp_dir);
    }

    ~TempFileHelper() {
        if (std::filesystem::exists(temp_dir)) {
            std::filesystem::remove_all(temp_dir);
        }
    }

    std::filesystem::path GetTempPath(const std::string& filename) const {
        return temp_dir / filename;
    }

    std::string GetTempPathString(const std::string& filename) const {
        return GetTempPath(filename).string();
    }

private:
    std::filesystem::path temp_dir;
};

/**
 * @brief Assertion helpers for common patterns
 */
namespace assertions {
    /**
     * @brief Assert a value is within a percentage range
     */
    template<typename T>
    void AssertWithinPercent(T actual, T expected, double percent_tolerance) {
        double tolerance = std::abs(expected * percent_tolerance / 100.0);
        BOOST_CHECK_CLOSE(actual, expected, percent_tolerance);
    }

    /**
     * @brief Assert a vector is normalized (magnitude ≈ 1.0)
     */
    inline void AssertVectorNormalized(const cv::Vec3d& vec, double tolerance = 0.01) {
        double magnitude = cv::norm(vec);
        BOOST_CHECK_CLOSE(magnitude, 1.0, tolerance * 100.0);
    }

    /**
     * @brief Assert two 3D points are close
     */
    inline void AssertPointsClose(
        const cv::Vec3d& p1,
        const cv::Vec3d& p2,
        double tolerance = 0.01)
    {
        BOOST_CHECK_SMALL(std::abs(p1[0] - p2[0]), tolerance);
        BOOST_CHECK_SMALL(std::abs(p1[1] - p2[1]), tolerance);
        BOOST_CHECK_SMALL(std::abs(p1[2] - p2[2]), tolerance);
    }
}

/**
 * @brief Mock objects for dependency injection in tests
 */
namespace mocks {
    // TODO: Add mock implementations as needed for testing
    // Example: MockCameraInterface, MockConfigurationManager, etc.
}

} // namespace testing
} // namespace golf_sim
