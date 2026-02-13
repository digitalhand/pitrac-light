/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// HoughCircles-based ball detection using OpenCV's Hough Transform.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <vector>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "golf_ball.h"
#include "gs_camera.h"

namespace golf_sim {

// Forward declarations
class GolfSimCamera;

/**
 * HoughDetector - Hough Transform-based circle detection for golf balls
 *
 * Provides configurable Hough circle detection with multiple parameter sets
 * optimized for different ball search scenarios (placed, strobed, putting, etc.)
 */
class HoughDetector {
public:

    // --- Configuration constants for different detection modes ---

    // Placed Ball Parameters (pre-shot ball at rest)
    static double kPlacedBallCannyLower;
    static double kPlacedBallCannyUpper;
    static double kPlacedBallStartingParam2;
    static double kPlacedBallMinParam2;
    static double kPlacedBallMaxParam2;
    static double kPlacedBallCurrentParam1;
    static double kPlacedBallParam2Increment;
    static int kPlacedMinHoughReturnCircles;
    static int kPlacedMaxHoughReturnCircles;
    static int kPlacedPreHoughBlurSize;
    static int kPlacedPreCannyBlurSize;
    static double kPlacedBallHoughDpParam1;

    // Strobed Ball Parameters (ball captured with strobe flash)
    static double kStrobedBallsCannyLower;
    static double kStrobedBallsCannyUpper;
    static int kStrobedBallsPreCannyBlurSize;
    static int kStrobedBallsPreHoughBlurSize;
    static double kStrobedBallsStartingParam2;
    static double kStrobedBallsMinParam2;
    static double kStrobedBallsMaxParam2;
    static double kStrobedBallsCurrentParam1;
    static double kStrobedBallsParam2Increment;
    static int kStrobedBallsMinHoughReturnCircles;
    static int kStrobedBallsMaxHoughReturnCircles;
    static double kStrobedBallsHoughDpParam1;

    // Alternative Strobed Algorithm Parameters
    static bool kStrobedBallsUseAltHoughAlgorithm;
    static double kStrobedBallsAltCannyLower;
    static double kStrobedBallsAltCannyUpper;
    static int kStrobedBallsAltPreCannyBlurSize;
    static int kStrobedBallsAltPreHoughBlurSize;
    static double kStrobedBallsAltStartingParam2;
    static double kStrobedBallsAltMinParam2;
    static double kStrobedBallsAltMaxParam2;
    static double kStrobedBallsAltCurrentParam1;
    static double kStrobedBallsAltHoughDpParam1;
    static double kStrobedBallsAltParam2Increment;

    // CLAHE (Contrast Limited Adaptive Histogram Equalization) Parameters
    static bool kUseCLAHEProcessing;
    static int kCLAHEClipLimit;
    static int kCLAHETilesGridSize;

    // Putting Mode Parameters (shorter shots on putting green)
    static double kPuttingBallStartingParam2;
    static double kPuttingBallMinParam2;
    static double kPuttingBallMaxParam2;
    static double kPuttingBallCurrentParam1;
    static double kPuttingBallParam2Increment;
    static int kPuttingMinHoughReturnCircles;
    static int kPuttingMaxHoughReturnCircles;
    static int kPuttingPreHoughBlurSize;
    static double kPuttingHoughDpParam1;

    // Externally Strobed Environment Parameters (using external strobe)
    static double kExternallyStrobedEnvCannyLower;
    static double kExternallyStrobedEnvCannyUpper;
    static double kExternallyStrobedEnvCurrentParam1;
    static double kExternallyStrobedEnvMinParam2;
    static double kExternallyStrobedEnvMaxParam2;
    static double kExternallyStrobedEnvStartingParam2;
    static double kExternallyStrobedEnvNarrowingParam2;
    static double kExternallyStrobedEnvNarrowingDpParam;
    static double kExternallyStrobedEnvParam2Increment;
    static int kExternallyStrobedEnvMinHoughReturnCircles;
    static int kExternallyStrobedEnvMaxHoughReturnCircles;
    static int kExternallyStrobedEnvPreHoughBlurSize;
    static int kExternallyStrobedEnvPreCannyBlurSize;
    static double kExternallyStrobedEnvHoughDpParam1;
    static int kExternallyStrobedEnvMinimumSearchRadius;
    static int kExternallyStrobedEnvMaximumSearchRadius;
    static double kStrobedNarrowingRadiiDpParam;
    static double kStrobedNarrowingRadiiParam2;
    static int kExternallyStrobedEnvNarrowingPreCannyBlurSize;
    static int kExternallyStrobedEnvNarrowingPreHoughBlurSize;

    // Externally Strobed CLAHE Parameters
    static bool kExternallyStrobedUseCLAHEProcessing;
    static int kExternallyStrobedCLAHEClipLimit;
    static int kExternallyStrobedCLAHETilesGridSize;

    // Dynamic Radii Adjustment Parameters
    static bool kUseDynamicRadiiAdjustment;
    static int kNumberRadiiToAverageForDynamicAdjustment;
    static double kStrobedNarrowingRadiiMinRatio;
    static double kStrobedNarrowingRadiiMaxRatio;

    // Placed Ball Narrowing Parameters
    static double kPlacedNarrowingRadiiMinRatio;
    static double kPlacedNarrowingRadiiMaxRatio;
    static double kPlacedNarrowingStartingParam2;
    static double kPlacedNarrowingRadiiDpParam;
    static double kPlacedNarrowingParam1;

    // Best Circle Refinement Parameters
    static bool kUseBestCircleRefinement;
    static bool kUseBestCircleLargestCircle;
    static double kBestCircleCannyLower;
    static double kBestCircleCannyUpper;
    static int kBestCirclePreCannyBlurSize;
    static int kBestCirclePreHoughBlurSize;
    static double kBestCircleParam1;
    static double kBestCircleParam2;
    static double kBestCircleHoughDpParam1;

    // Externally Strobed Best Circle Parameters
    static double kExternallyStrobedBestCircleCannyLower;
    static double kExternallyStrobedBestCircleCannyUpper;
    static int kExternallyStrobedBestCirclePreCannyBlurSize;
    static int kExternallyStrobedBestCirclePreHoughBlurSize;
    static double kExternallyStrobedBestCircleParam1;
    static double kExternallyStrobedBestCircleParam2;
    static double kExternallyStrobedBestCircleHoughDpParam1;

    // Best Circle Identification Parameters
    static double kBestCircleIdentificationMinRadiusRatio;
    static double kBestCircleIdentificationMaxRadiusRatio;

    // --- Public API ---

    enum BallSearchMode {
        kUnknown = 0,
        kFindPlacedBall = 1,
        kStrobed = 2,
        kExternallyStrobed = 3,
        kPutting = 4
    };

    /**
     * Preprocesses strobed images with CLAHE (Contrast Limited Adaptive Histogram Equalization)
     * Enhances local contrast to improve ball detection in varying lighting conditions
     *
     * @param search_image Input/output image to preprocess (modified in place)
     * @param search_mode The ball search mode (determines CLAHE parameters)
     * @return true if preprocessing succeeded, false otherwise
     */
    static bool PreProcessStrobedImage(cv::Mat& search_image, BallSearchMode search_mode);

    /**
     * Performs iterative refinement to identify the best ball circle
     * Uses narrowed parameter ranges around a reference ball to improve accuracy
     *
     * @param gray_image Input grayscale image containing the ball
     * @param reference_ball Reference ball with approximate location/radius
     * @param choose_largest_final_ball If true, prefer larger circles over better-scored ones
     * @param final_circle Output: refined circle result
     * @return true if refinement succeeded, false otherwise
     */
    static bool DetermineBestCircle(const cv::Mat& gray_image,
                                    const GolfBall& reference_ball,
                                    bool choose_largest_final_ball,
                                    GsCircle& final_circle);

    /**
     * Detection Algorithm Dispatcher
     * Routes detection to HoughCircles or ONNX based on kDetectionMethod configuration
     *
     * @param preprocessed_img Preprocessed image ready for detection
     * @param search_mode The ball search mode
     * @param detected_circles Output: detected circles
     * @return true if detection found circles, false otherwise
     */
    static bool DetectBalls(const cv::Mat& preprocessed_img,
                           BallSearchMode search_mode,
                           std::vector<GsCircle>& detected_circles);

    /**
     * Legacy HoughCircles Detection
     * Performs Hough Transform circle detection with mode-specific parameters
     *
     * @param preprocessed_img Preprocessed image ready for detection
     * @param search_mode The ball search mode
     * @param detected_circles Output: detected circles
     * @return true if detection found circles, false otherwise
     */
    static bool DetectBallsHoughCircles(const cv::Mat& preprocessed_img,
                                       BallSearchMode search_mode,
                                       std::vector<GsCircle>& detected_circles);

    /**
     * Removes concentric circles from detection results
     * Keeps only the outermost circle when multiple concentric circles are detected
     *
     * @param circles Input/output vector of circles (modified to remove concentric circles)
     * @return true if any circles were removed
     */
    static bool RemoveSmallestConcentricCircles(std::vector<GsCircle>& circles);

private:
    /**
     * Removes linear noise artifacts from Canny edge detection
     * Uses morphological operations to eliminate horizontal/vertical line artifacts
     *
     * @param img Input/output image (modified in place)
     * @return true if noise removal succeeded
     */
    static bool RemoveLinearNoise(cv::Mat& img);

    /**
     * Rounds circle data to integer values
     * Utility function to clean up circle coordinates and radii
     *
     * @param circles Input/output vector of circles (modified in place)
     */
    static void RoundCircleData(std::vector<GsCircle>& circles);
};

} // namespace golf_sim
