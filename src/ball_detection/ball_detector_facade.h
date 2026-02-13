/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Facade that orchestrates the ball detection pipeline using extracted modules.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <vector>
#include <opencv2/core.hpp>

#include "golf_ball.h"
#include "search_strategy.h"

namespace golf_sim {

/**
 * BallDetectorFacade - Orchestrates the complete ball detection pipeline
 *
 * This facade coordinates all extracted detection modules:
 * - SearchStrategy: Mode-specific parameter selection
 * - HoughDetector: Circle detection and preprocessing
 * - EllipseDetector: Ellipse fitting for non-circular balls
 * - ColorFilter: HSV color validation
 * - ROIManager: Region of interest extraction
 * - SpinAnalyzer: Rotation detection (when needed)
 *
 * Provides a unified interface for ball detection across all modes
 * (placed, strobed, putting, externally strobed).
 */
class BallDetectorFacade {
public:
    /**
     * Main ball detection method - orchestrates the complete pipeline
     *
     * This method:
     * 1. Validates input image
     * 2. Selects detection strategy based on search_mode
     * 3. Preprocesses image (CLAHE, blur, Canny as needed)
     * 4. Performs circle/ellipse detection
     * 5. Filters and scores candidates
     * 6. Returns best ball(s)
     *
     * @param img Input RGB image containing the ball
     * @param baseBallWithSearchParams Reference ball with search parameters (color, expected position)
     * @param return_balls Output vector of detected balls (sorted by quality)
     * @param expectedBallArea Expected region where ball should be found
     * @param search_mode Detection mode (placed, strobed, putting, externally strobed)
     * @param chooseLargestFinalBall If true, prefer larger circles over better-scored ones
     * @param report_find_failures If true, log detailed failure information
     * @return true if at least one ball detected, false otherwise
     */
    static bool GetBall(const cv::Mat& img,
                       const GolfBall& baseBallWithSearchParams,
                       std::vector<GolfBall>& return_balls,
                       cv::Rect& expectedBallArea,
                       SearchStrategy::Mode search_mode,
                       bool chooseLargestFinalBall = false,
                       bool report_find_failures = true);

    /**
     * Detect balls using ONNX/DNN models (experimental path)
     *
     * Alternative detection using ML models (YOLOv8, etc.)
     * Bypasses traditional Hough/ellipse detection
     *
     * @param img Input RGB image
     * @param baseBallWithSearchParams Reference ball parameters
     * @param return_balls Output vector of detected balls
     * @param search_mode Detection mode
     * @return true if detection succeeded
     */
    static bool GetBallONNX(const cv::Mat& img,
                           const GolfBall& baseBallWithSearchParams,
                           std::vector<GolfBall>& return_balls,
                           SearchStrategy::Mode search_mode);

    /**
     * Detect balls using legacy HoughCircles approach
     *
     * Traditional circle detection with mode-specific preprocessing
     *
     * @param img Input RGB image
     * @param baseBallWithSearchParams Reference ball parameters
     * @param return_balls Output vector of detected balls
     * @param expectedBallArea Expected region
     * @param search_mode Detection mode
     * @param chooseLargestFinalBall Prefer larger circles
     * @param report_find_failures Log failures
     * @return true if detection succeeded
     */
    static bool GetBallHough(const cv::Mat& img,
                            const GolfBall& baseBallWithSearchParams,
                            std::vector<GolfBall>& return_balls,
                            cv::Rect& expectedBallArea,
                            SearchStrategy::Mode search_mode,
                            bool chooseLargestFinalBall,
                            bool report_find_failures);

private:
    /**
     * Preprocess image based on search mode
     *
     * Applies mode-specific preprocessing:
     * - Placed: Canny edge detection with blur
     * - Strobed: CLAHE + Canny + blur
     * - Externally strobed: Artifact removal + CLAHE + Canny
     * - Putting: EDPF edge detection
     *
     * @param search_image Input/output image (modified in place)
     * @param mode Search mode
     * @return true if preprocessing succeeded
     */
    static bool PreprocessForMode(cv::Mat& search_image, SearchStrategy::Mode mode);

    /**
     * Filter and score circle candidates
     *
     * Applies quality metrics:
     * - Color matching (RGB distance)
     * - Radius consistency
     * - Position likelihood
     * - Mode-specific sorting (color vs radius)
     *
     * @param circles Input vector of detected circles
     * @param baseBall Reference ball for comparison
     * @param return_balls Output scored and filtered balls
     * @param rgbImg Original RGB image for color analysis
     * @param search_mode Detection mode (affects scoring strategy)
     * @param report_find_failures Whether to log failures
     * @return true if at least one valid candidate remains
     */
    static bool FilterAndScoreCandidates(const std::vector<GsCircle>& circles,
                                        const GolfBall& baseBall,
                                        std::vector<GolfBall>& return_balls,
                                        const cv::Mat& rgbImg,
                                        SearchStrategy::Mode search_mode,
                                        bool report_find_failures);

    /**
     * Perform best circle refinement
     *
     * Narrows detection parameters around a candidate ball
     * to get more precise position and radius
     *
     * @param gray_image Input grayscale image
     * @param candidate Candidate ball to refine
     * @param chooseLargest Prefer largest circle
     * @param refined_circle Output refined circle
     * @return true if refinement succeeded
     */
    static bool RefineBestCircle(const cv::Mat& gray_image,
                                const GolfBall& candidate,
                                bool chooseLargest,
                                GsCircle& refined_circle);
};

} // namespace golf_sim
