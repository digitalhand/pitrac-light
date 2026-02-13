/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Strategy pattern for ball detection modes (placed, strobed, putting, etc.)
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <memory>
#include <opencv2/core.hpp>

#include "golf_ball.h"
#include "hough_detector.h"

namespace golf_sim {

/**
 * SearchStrategy - Strategy pattern for ball detection modes
 *
 * Encapsulates mode-specific detection logic:
 * - Parameter selection (Hough params, blur sizes, CLAHE settings)
 * - Preprocessing steps (CLAHE, blur, Canny)
 * - Detection algorithm selection (HoughCircles, ONNX, ellipse)
 *
 * Each mode (placed, strobed, putting) has different requirements:
 * - Placed ball: Single stationary ball, focus on precision
 * - Strobed ball: Multiple balls in rapid succession, need speed
 * - Putting: Shorter range, different lighting conditions
 * - Externally strobed: External strobe trigger, different timing
 */
class SearchStrategy {
public:
    /**
     * Ball search modes - determines detection strategy
     */
    enum Mode {
        kUnknown = 0,
        kFindPlacedBall = 1,
        kStrobed = 2,
        kExternallyStrobed = 3,
        kPutting = 4
    };

    /**
     * Detection parameters for a specific search mode
     * Encapsulates all mode-specific tuning values
     */
    struct DetectionParams {
        // Hough parameters
        double hough_dp_param1;
        double canny_lower;
        double canny_upper;
        double param1;
        double starting_param2;
        double min_param2;
        double max_param2;
        double param2_increment;
        int min_hough_return_circles;
        int max_hough_return_circles;
        int pre_canny_blur_size;
        int pre_hough_blur_size;

        // CLAHE parameters
        bool use_clahe;
        int clahe_clip_limit;
        int clahe_tiles_grid_size;

        // Search constraints
        int minimum_search_radius;
        int maximum_search_radius;

        // Narrowing parameters (for refinement)
        double narrowing_radii_min_ratio;
        double narrowing_radii_max_ratio;
        double narrowing_starting_param2;
        double narrowing_radii_dp_param;
        double narrowing_param1;
        double narrowing_radii_param2;
        int narrowing_pre_canny_blur_size;
        int narrowing_pre_hough_blur_size;

        // Dynamic adjustment
        bool use_dynamic_radii_adjustment;
        int num_radii_to_average;
    };

    /**
     * Get detection parameters for a specific mode
     *
     * @param mode The ball search mode
     * @return DetectionParams structure with mode-specific parameters
     */
    static DetectionParams GetParamsForMode(Mode mode);

    /**
     * Check if a mode requires preprocessing (CLAHE, blur, Canny)
     *
     * @param mode The ball search mode
     * @return true if preprocessing required, false otherwise
     */
    static bool RequiresPreprocessing(Mode mode);

    /**
     * Check if a mode should use alternative Hough algorithm
     * Only applies to strobed mode
     *
     * @param mode The ball search mode
     * @return true if alternative algorithm should be used
     */
    static bool UseAlternativeHoughAlgorithm(Mode mode);

    /**
     * Get mode name as string (for logging)
     *
     * @param mode The ball search mode
     * @return String representation of mode
     */
    static const char* GetModeName(Mode mode);

    /**
     * Determine if best circle refinement should be used
     *
     * @param mode The ball search mode
     * @return true if refinement should be performed
     */
    static bool UseBestCircleRefinement(Mode mode);

    /**
     * Factory method: Create appropriate strategy for a mode
     * (Future enhancement: return concrete strategy objects)
     *
     * @param mode The ball search mode
     * @return Shared pointer to strategy (currently returns nullptr, placeholder for future)
     */
    static std::shared_ptr<SearchStrategy> CreateStrategy(Mode mode);

private:
    // Private constructor - use factory method
    SearchStrategy() = default;
};

} // namespace golf_sim
