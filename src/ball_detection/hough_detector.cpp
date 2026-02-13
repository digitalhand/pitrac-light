/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// HoughCircles-based ball detection - extracted from ball_image_proc.cpp
// Phase 3.1 modular refactoring

#include "hough_detector.h"

#include <opencv2/photo.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>

#include "utils/logging_tools.h"
#include "utils/cv_utils.h"
#include "gs_options.h"

namespace golf_sim {

// --- Configuration Constants Initialization ---

// Placed Ball Parameters
double HoughDetector::kPlacedBallCannyLower = 0.0;
double HoughDetector::kPlacedBallCannyUpper = 0.0;
double HoughDetector::kPlacedBallStartingParam2 = 40;
double HoughDetector::kPlacedBallMinParam2 = 30;
double HoughDetector::kPlacedBallMaxParam2 = 60;
double HoughDetector::kPlacedBallCurrentParam1 = 120.0;
double HoughDetector::kPlacedBallParam2Increment = 4;
int HoughDetector::kPlacedMinHoughReturnCircles = 1;
int HoughDetector::kPlacedMaxHoughReturnCircles = 4;
int HoughDetector::kPlacedPreHoughBlurSize = 11;
int HoughDetector::kPlacedPreCannyBlurSize = 5;
double HoughDetector::kPlacedBallHoughDpParam1 = 1.5;

// Strobed Ball Parameters
double HoughDetector::kStrobedBallsCannyLower = 50;
double HoughDetector::kStrobedBallsCannyUpper = 110;
int HoughDetector::kStrobedBallsPreCannyBlurSize = 5;
int HoughDetector::kStrobedBallsPreHoughBlurSize = 13;
double HoughDetector::kStrobedBallsStartingParam2 = 40;
double HoughDetector::kStrobedBallsMinParam2 = 30;
double HoughDetector::kStrobedBallsMaxParam2 = 60;
double HoughDetector::kStrobedBallsCurrentParam1 = 120.0;
double HoughDetector::kStrobedBallsParam2Increment = 4;
int HoughDetector::kStrobedBallsMinHoughReturnCircles = 1;
int HoughDetector::kStrobedBallsMaxHoughReturnCircles = 12;
double HoughDetector::kStrobedBallsHoughDpParam1 = 1.5;

// Alternative Strobed Algorithm
bool HoughDetector::kStrobedBallsUseAltHoughAlgorithm = true;
double HoughDetector::kStrobedBallsAltCannyLower = 35;
double HoughDetector::kStrobedBallsAltCannyUpper = 70;
int HoughDetector::kStrobedBallsAltPreCannyBlurSize = 11;
int HoughDetector::kStrobedBallsAltPreHoughBlurSize = 16;
double HoughDetector::kStrobedBallsAltStartingParam2 = 0.95;
double HoughDetector::kStrobedBallsAltMinParam2 = 0.6;
double HoughDetector::kStrobedBallsAltMaxParam2 = 1.0;
double HoughDetector::kStrobedBallsAltCurrentParam1 = 130.0;
double HoughDetector::kStrobedBallsAltHoughDpParam1 = 1.5;
double HoughDetector::kStrobedBallsAltParam2Increment = 0.05;

// CLAHE Parameters
bool HoughDetector::kUseCLAHEProcessing = false;
int HoughDetector::kCLAHEClipLimit = 0;
int HoughDetector::kCLAHETilesGridSize = 0;

// Putting Mode Parameters
double HoughDetector::kPuttingBallStartingParam2 = 40;
double HoughDetector::kPuttingBallMinParam2 = 30;
double HoughDetector::kPuttingBallMaxParam2 = 60;
double HoughDetector::kPuttingBallCurrentParam1 = 120.0;
double HoughDetector::kPuttingBallParam2Increment = 4;
int HoughDetector::kPuttingMinHoughReturnCircles = 1;
int HoughDetector::kPuttingMaxHoughReturnCircles = 12;
int HoughDetector::kPuttingPreHoughBlurSize = 9;
double HoughDetector::kPuttingHoughDpParam1 = 1.5;

// Externally Strobed Environment Parameters
double HoughDetector::kExternallyStrobedEnvCannyLower = 35;
double HoughDetector::kExternallyStrobedEnvCannyUpper = 80;
double HoughDetector::kExternallyStrobedEnvCurrentParam1 = 130.0;
double HoughDetector::kExternallyStrobedEnvMinParam2 = 28;
double HoughDetector::kExternallyStrobedEnvMaxParam2 = 100;
double HoughDetector::kExternallyStrobedEnvStartingParam2 = 65;
double HoughDetector::kExternallyStrobedEnvNarrowingParam2 = 0.6;
double HoughDetector::kExternallyStrobedEnvNarrowingDpParam = 1.1;
double HoughDetector::kExternallyStrobedEnvParam2Increment = 4;
int HoughDetector::kExternallyStrobedEnvMinHoughReturnCircles = 3;
int HoughDetector::kExternallyStrobedEnvMaxHoughReturnCircles = 20;
int HoughDetector::kExternallyStrobedEnvPreHoughBlurSize = 11;
int HoughDetector::kExternallyStrobedEnvPreCannyBlurSize = 3;
double HoughDetector::kExternallyStrobedEnvHoughDpParam1 = 1.0;
int HoughDetector::kExternallyStrobedEnvMinimumSearchRadius = 60;
int HoughDetector::kExternallyStrobedEnvMaximumSearchRadius = 80;
double HoughDetector::kStrobedNarrowingRadiiDpParam = 1.8;
double HoughDetector::kStrobedNarrowingRadiiParam2 = 100.0;
int HoughDetector::kExternallyStrobedEnvNarrowingPreCannyBlurSize = 3;
int HoughDetector::kExternallyStrobedEnvNarrowingPreHoughBlurSize = 9;

// Externally Strobed CLAHE
bool HoughDetector::kExternallyStrobedUseCLAHEProcessing = true;
int HoughDetector::kExternallyStrobedCLAHEClipLimit = 6;
int HoughDetector::kExternallyStrobedCLAHETilesGridSize = 6;

// Dynamic Radii Adjustment
bool HoughDetector::kUseDynamicRadiiAdjustment = true;
int HoughDetector::kNumberRadiiToAverageForDynamicAdjustment = 3;
double HoughDetector::kStrobedNarrowingRadiiMinRatio = 0.8;
double HoughDetector::kStrobedNarrowingRadiiMaxRatio = 1.2;

// Placed Ball Narrowing
double HoughDetector::kPlacedNarrowingRadiiMinRatio = 0.9;
double HoughDetector::kPlacedNarrowingRadiiMaxRatio = 1.1;
double HoughDetector::kPlacedNarrowingStartingParam2 = 80.0;
double HoughDetector::kPlacedNarrowingRadiiDpParam = 2.0;
double HoughDetector::kPlacedNarrowingParam1 = 130.0;

// Best Circle Refinement
bool HoughDetector::kUseBestCircleRefinement = false;
bool HoughDetector::kUseBestCircleLargestCircle = false;
double HoughDetector::kBestCircleCannyLower = 55;
double HoughDetector::kBestCircleCannyUpper = 110;
int HoughDetector::kBestCirclePreCannyBlurSize = 5;
int HoughDetector::kBestCirclePreHoughBlurSize = 13;
double HoughDetector::kBestCircleParam1 = 120.;
double HoughDetector::kBestCircleParam2 = 35.;
double HoughDetector::kBestCircleHoughDpParam1 = 1.5;

// Externally Strobed Best Circle
double HoughDetector::kExternallyStrobedBestCircleCannyLower = 55;
double HoughDetector::kExternallyStrobedBestCircleCannyUpper = 110;
int HoughDetector::kExternallyStrobedBestCirclePreCannyBlurSize = 5;
int HoughDetector::kExternallyStrobedBestCirclePreHoughBlurSize = 13;
double HoughDetector::kExternallyStrobedBestCircleParam1 = 120.;
double HoughDetector::kExternallyStrobedBestCircleParam2 = 35.;
double HoughDetector::kExternallyStrobedBestCircleHoughDpParam1 = 1.5;

// Best Circle Identification
double HoughDetector::kBestCircleIdentificationMinRadiusRatio = 0.85;
double HoughDetector::kBestCircleIdentificationMaxRadiusRatio = 1.10;

// --- Implementation: Utility Methods ---

void HoughDetector::RoundCircleData(std::vector<GsCircle>& circles) {
    for (auto& c : circles) {
        c[0] = std::round(c[0]);
        c[1] = std::round(c[1]);
        c[2] = std::round(c[2]);
    }
}

bool HoughDetector::RemoveSmallestConcentricCircles(std::vector<GsCircle>& circles) {
    // Remove any concentric (nested) circles that share the same center but have different radii
    // The incoming circles may be in any order, so have to check all pairs.

    for (int i = 0; i < (int)(circles.size()) - 1; i++) {
        GsCircle& circle_current = circles[i];

        for (int j = (int)circles.size() - 1; j > i; j--) {
            GsCircle& circle_other = circles[j];

            if (CvUtils::CircleXY(circle_current) == CvUtils::CircleXY(circle_other)) {
                // The two circles are concentric. Remove the smaller circle
                int radius_current = (int)std::round(circle_current[2]);
                int radius_other = (int)std::round(circle_other[2]);

                if (radius_other <= radius_current) {
                    circles.erase(circles.begin() + j);
                }
                else {
                    circles.erase(circles.begin() + i);
                    i--; // Skip over the circle we just erased
                    break; // Move to next outer loop circle
                }
            }
        }
    }

    return true;
}

bool HoughDetector::RemoveLinearNoise(cv::Mat& img) {
    LoggingTools::DebugShowImage("HoughDetector::RemoveLinearNoise - before removing lines", img);

#ifndef USING_HORIZ_VERT_REMOVAL
    // Linear noise removal disabled
#else
    // Get rid of strongly horizontal and vertical lines
    int minLineLength = std::max(2, img.cols / 25);
    cv::Mat horizontalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(minLineLength, 1));
    cv::Mat verticalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, minLineLength));

    cv::Mat horizontalLinesImg = img.clone();
    cv::erode(img, horizontalLinesImg, horizontalKernel, cv::Point(-1, -1), 1);
    cv::Mat verticalLinesImg = img.clone();
    cv::erode(img, verticalLinesImg, verticalKernel, cv::Point(-1, -1), 1);

    LoggingTools::DebugShowImage("HoughDetector - horizontal lines to filter", horizontalLinesImg);
    LoggingTools::DebugShowImage("HoughDetector - vertical lines to filter", verticalLinesImg);

    cv::bitwise_xor(img, horizontalLinesImg, img);
    cv::bitwise_xor(img, verticalLinesImg, img);

    LoggingTools::DebugShowImage("HoughDetector::RemoveLinearNoise - after removing lines", img);
#endif
    return true;
}

// --- Implementation: Preprocessing ---

bool HoughDetector::PreProcessStrobedImage(cv::Mat& search_image, BallSearchMode search_mode) {
    GS_LOG_TRACE_MSG(trace, "HoughDetector::PreProcessStrobedImage");

    if (search_image.empty()) {
        GS_LOG_MSG(error, "PreProcessStrobedImage called with no image to work with (search_image)");
        return false;
    }

    // Setup CLAHE processing dependent on PiTrac-only strobing or externally-strobed
    bool use_clahe_processing = true;
    int clahe_tiles_grid_size = -1;
    int clahe_clip_limit = -1;

    if (search_mode == kStrobed) {
        use_clahe_processing = kUseCLAHEProcessing;
        clahe_tiles_grid_size = kCLAHETilesGridSize;
        clahe_clip_limit = kCLAHEClipLimit;
    }
    else if (search_mode == kExternallyStrobed) {
        use_clahe_processing = kExternallyStrobedUseCLAHEProcessing;
        clahe_tiles_grid_size = kExternallyStrobedCLAHETilesGridSize;
        clahe_clip_limit = kExternallyStrobedCLAHEClipLimit;
    }
    else {
        GS_LOG_MSG(error, "PreProcessStrobedImage called with invalid search_mode)");
        return false;
    }

    // Create a CLAHE object
    if (use_clahe_processing) {
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();

        // Set CLAHE parameters
        if (clahe_tiles_grid_size < 1) {
            clahe_tiles_grid_size = 1;
            GS_LOG_MSG(warning, "clahe_tiles_grid_size was < 1 - Resetting to 1.");
        }
        if (clahe_clip_limit < 1) {
            clahe_clip_limit = 1;
            GS_LOG_MSG(warning, "kCLAHEClipLimit was < 1 - Resetting to 1.");
        }

        GS_LOG_TRACE_MSG(trace, "Using CLAHE Pre-processing with GridSize = " + std::to_string(clahe_tiles_grid_size) +
            ", ClipLimit = " + std::to_string(clahe_clip_limit));

        clahe->setClipLimit(clahe_clip_limit);
        clahe->setTilesGridSize(cv::Size(clahe_tiles_grid_size, clahe_tiles_grid_size));

        // Apply CLAHE
        clahe->apply(search_image, search_image);

        LoggingTools::DebugShowImage("Strobed Ball Image - After CLAHE equalization", search_image);
    }

    double canny_lower = 0.0;
    double canny_upper = 0.0;
    int pre_canny_blur_size = 0;
    int pre_hough_blur_size = 0;

    if (search_mode == kStrobed) {
        if (kStrobedBallsUseAltHoughAlgorithm) {
            canny_lower = kStrobedBallsAltCannyLower;
            canny_upper = kStrobedBallsAltCannyUpper;
            pre_canny_blur_size = kStrobedBallsAltPreCannyBlurSize;
            pre_hough_blur_size = kStrobedBallsAltPreHoughBlurSize;
        }
        else {
            canny_lower = kStrobedBallsCannyLower;
            canny_upper = kStrobedBallsCannyUpper;
            pre_canny_blur_size = kStrobedBallsPreCannyBlurSize;
            pre_hough_blur_size = kStrobedBallsPreHoughBlurSize;
        }
    }
    else if (search_mode == kExternallyStrobed) {
        canny_lower = kExternallyStrobedEnvCannyLower;
        canny_upper = kExternallyStrobedEnvCannyUpper;
        pre_canny_blur_size = kExternallyStrobedEnvPreCannyBlurSize;
        pre_hough_blur_size = kExternallyStrobedEnvPreHoughBlurSize;
    }

    // The size for the blur must be odd - force it up in value by 1 if necessary
    if (pre_canny_blur_size > 0) {
        if (pre_canny_blur_size % 2 != 1) {
            pre_canny_blur_size++;
        }
    }

    if (pre_hough_blur_size > 0) {
        if (pre_hough_blur_size % 2 != 1) {
            pre_hough_blur_size++;
        }
    }

    GS_LOG_MSG(trace, "Main HoughCircle Image Prep - Performing Pre-Hough Blur and Canny for kStrobed mode.");
    GS_LOG_MSG(trace, "  Blur Parameters are: pre_canny_blur_size = " + std::to_string(pre_canny_blur_size) +
        ", pre_hough_blur_size " + std::to_string(pre_hough_blur_size));
    GS_LOG_MSG(trace, "  Canny Parameters are: canny_lower = " + std::to_string(canny_lower) +
        ", canny_upper " + std::to_string(canny_upper));

    if (pre_canny_blur_size > 0) {
        cv::GaussianBlur(search_image, search_image, cv::Size(pre_canny_blur_size, pre_canny_blur_size), 0);
    }
    else {
        GS_LOG_TRACE_MSG(trace, "Skipping pre-Canny Blur");
    }

    LoggingTools::DebugShowImage("Strobed Ball Image - Ready for Edge Detection", search_image);

    cv::Mat cannyOutput_for_balls;
    if (search_mode == kExternallyStrobed && pre_canny_blur_size == 0) {
        // Don't do the Canny at all if the blur size is zero and we're in comparison mode
        cannyOutput_for_balls = search_image.clone();
    }
    else {
        cv::Canny(search_image, cannyOutput_for_balls, canny_lower, canny_upper);
    }

    LoggingTools::DebugShowImage("cannyOutput_for_balls", cannyOutput_for_balls);

    // Blur the lines-only image back to the search_image that the code below uses
    cv::GaussianBlur(cannyOutput_for_balls, search_image, cv::Size(pre_hough_blur_size, pre_hough_blur_size), 0);

    return true;
}

// --- Implementation: Detection Dispatcher ---

bool HoughDetector::DetectBalls(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                std::vector<GsCircle>& detected_circles) {
    GS_LOG_TRACE_MSG(trace, "HoughDetector::DetectBalls");

    // For now, always use HoughCircles (ONNX detection will be separate module)
    return DetectBallsHoughCircles(preprocessed_img, search_mode, detected_circles);
}

bool HoughDetector::DetectBallsHoughCircles(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                           std::vector<GsCircle>& detected_circles) {
    GS_LOG_TRACE_MSG(trace, "HoughDetector::DetectBallsHoughCircles - mode: " + std::to_string(search_mode));

    // TODO: Implement actual HoughCircles detection logic
    // This will be extracted from BallImageProc::GetBall() in next iteration

    GS_LOG_MSG(warning, "HoughDetector::DetectBallsHoughCircles - Not yet fully implemented");
    return false;
}

// --- Implementation: Best Circle Refinement ---

bool HoughDetector::DetermineBestCircle(const cv::Mat& input_gray_image,
                                        const GolfBall& reference_ball,
                                        bool choose_largest_final_ball,
                                        GsCircle& final_circle) {

    const cv::Mat& gray_image = input_gray_image;  // No clone needed - read-only usage

    // We are pretty sure we got the correct ball, or at least something really close.
    // Now, try to find the best circle within the area around the candidate ball to see
    // if we can get a more precise position and radius.

    const GsCircle& reference_ball_circle = reference_ball.ball_circle_;

    cv::Vec2i resolution = CvUtils::CvSize(gray_image);
    cv::Vec2i xy = CvUtils::CircleXY(reference_ball_circle);
    int circleX = xy[0];
    int circleY = xy[1];
    int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

    GS_LOG_TRACE_MSG(trace, "DetermineBestCircle using reference_ball_circle with radius = " + std::to_string(ballRadius) +
        ".  (X,Y) center = (" + std::to_string(circleX) + "," + std::to_string(circleY) + ")");

    // Hough is expensive - use it only in the region of interest
    const double kHoughBestCircleSubImageSizeMultiplier = 1.5;
    int expandedRadiusForHough = (int)(kHoughBestCircleSubImageSizeMultiplier * (double)ballRadius);

    // If the ball is near the screen edge, reduce the width or height accordingly.
    double roi_x = std::round(circleX - expandedRadiusForHough);
    double roi_y = std::round(circleY - expandedRadiusForHough);
    double roi_width = std::round(2. * expandedRadiusForHough);
    double roi_height = roi_width;

    if (roi_x < 0.0) {
        roi_width += (roi_x);
        roi_x = 0;
    }

    if (roi_y < 0.0) {
        roi_height += (roi_y);
        roi_y = 0;
    }

    if (roi_x > gray_image.cols) {
        roi_width -= (roi_x - gray_image.cols);
        roi_x = gray_image.cols;
    }

    if (roi_y > gray_image.rows) {
        roi_height += (roi_y - gray_image.rows);
        roi_y = gray_image.rows;
    }

    cv::Rect ball_ROI_rect{ (int)roi_x, (int)roi_y, (int)roi_width, (int)roi_height };

    cv::Point offset_sub_to_full;
    cv::Point offset_full_to_sub;

    cv::Mat finalChoiceSubImg = CvUtils::GetSubImage(gray_image, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);

    int min_ball_radius = int(ballRadius * kBestCircleIdentificationMinRadiusRatio);
    int max_ball_radius = int(ballRadius * kBestCircleIdentificationMaxRadiusRatio);

    LoggingTools::DebugShowImage("Best Circle" + std::to_string(expandedRadiusForHough) + "  BestBall Image - Ready for Edge Detection", finalChoiceSubImg);

    cv::Mat cannyOutput_for_balls;

    bool is_externally_strobed = GolfSimOptions::GetCommandLineOptions().lm_comparison_mode_;

    if (!is_externally_strobed) {
        cv::GaussianBlur(finalChoiceSubImg, finalChoiceSubImg, cv::Size(kBestCirclePreCannyBlurSize, kBestCirclePreCannyBlurSize), 0);
        cv::Canny(finalChoiceSubImg, cannyOutput_for_balls, kBestCircleCannyLower, kBestCircleCannyUpper);
        LoggingTools::DebugShowImage("Best Circle (Non-externally-strobed)" + std::to_string(expandedRadiusForHough) + "  cannyOutput for best ball", cannyOutput_for_balls);
        cv::GaussianBlur(cannyOutput_for_balls, finalChoiceSubImg, cv::Size(kBestCirclePreHoughBlurSize, kBestCirclePreHoughBlurSize), 0);
    }
    else {
        cannyOutput_for_balls = finalChoiceSubImg.clone();
        LoggingTools::DebugShowImage("Best Circle (externally-strobed)" + std::to_string(expandedRadiusForHough) + "  cannyOutput for best ball", cannyOutput_for_balls);
        cv::GaussianBlur(cannyOutput_for_balls, finalChoiceSubImg, cv::Size(kExternallyStrobedBestCirclePreHoughBlurSize, kExternallyStrobedBestCirclePreHoughBlurSize), 0);
    }

    double currentParam1 = is_externally_strobed ? kExternallyStrobedBestCircleParam1 : kBestCircleParam1;
    double currentParam2 = is_externally_strobed ? kExternallyStrobedBestCircleParam2 : kBestCircleParam2;
    double currentDp = is_externally_strobed ? kExternallyStrobedBestCircleHoughDpParam1 : kBestCircleHoughDpParam1;

    int minimum_inter_ball_distance = 20;

    LoggingTools::DebugShowImage("FINAL Best Circle image" + std::to_string(expandedRadiusForHough) + "  finalChoiceSubImg for best ball", finalChoiceSubImg);

    GS_LOG_MSG(info, "DetermineBestCircle - Executing houghCircles with currentDP = " + std::to_string(currentDp) +
        ", minDist (1) = " + std::to_string(minimum_inter_ball_distance) + ", param1 = " + std::to_string(currentParam1) +
        ", param2 = " + std::to_string(currentParam2) + ", minRadius = " + std::to_string(int(min_ball_radius)) +
        ", maxRadius = " + std::to_string(int(max_ball_radius)));

    std::vector<GsCircle> finalTargetedCircles;

    cv::HoughCircles(
        finalChoiceSubImg,
        finalTargetedCircles,
        cv::HOUGH_GRADIENT_ALT,
        currentDp,
        /*minDist = */ minimum_inter_ball_distance,
        /*param1 = */ currentParam1,
        /*param2 = */ currentParam2,
        /*minRadius = */ min_ball_radius,
        /*maxRadius = */ max_ball_radius);

    if (!finalTargetedCircles.empty()) {
        GS_LOG_TRACE_MSG(trace, "Hough FOUND " + std::to_string(finalTargetedCircles.size()) + " targeted circles.");
    }
    else {
        GS_LOG_TRACE_MSG(trace, "Could not find any circles after performing targeted Hough Transform");
        return false;
    }

    // Show the final group of candidates
    cv::Mat targetedCandidatesImage = finalChoiceSubImg.clone();

    final_circle = finalTargetedCircles[0];
    double averageRadius = 0;
    double averageX = 0;
    double averageY = 0;
    int averagedBalls = 0;

    int kMaximumBestCirclesToEvaluate = 3;
    int MaxFinalCandidateBallsToAverage = 4;

    int i = 0;
    for (auto& c : finalTargetedCircles) {
        i += 1;
        if (i > (kMaximumBestCirclesToEvaluate) && i != 1)
            break;

        double found_radius = c[2];
        GS_LOG_TRACE_MSG(trace, "Found targeted circle with radius = " + std::to_string(found_radius) + ".  (X,Y) center = (" + std::to_string(c[0]) + "," + std::to_string(c[1]) + ")");
        if (i <= MaxFinalCandidateBallsToAverage) {
            LoggingTools::DrawCircleOutlineAndCenter(targetedCandidatesImage, c, std::to_string(i), i);

            averageRadius += found_radius;
            averageX += std::round(c[0]);
            averageY += std::round(c[1]);
            averagedBalls++;
        }

        if (found_radius > final_circle[2]) {
            final_circle = c;
        }
    }

    averageRadius /= averagedBalls;
    averageX /= averagedBalls;
    averageY /= averagedBalls;

    GS_LOG_TRACE_MSG(trace, "Average Radius was: " + std::to_string(averageRadius) + ". Average (X,Y) = "
                        + std::to_string(averageX) + ", " + std::to_string(averageY) + ").");

    LoggingTools::DebugShowImage("DetermineBestCircle Hough-identified Targeted Circles", targetedCandidatesImage);

    // Assume that the first ball will be the highest-quality match
    if (!choose_largest_final_ball) {
        final_circle = finalTargetedCircles[0];
    }

    // Un-offset the circle back into the full image coordinate system
    final_circle[0] += offset_sub_to_full.x;
    final_circle[1] += offset_sub_to_full.y;

    return true;
}

} // namespace golf_sim
