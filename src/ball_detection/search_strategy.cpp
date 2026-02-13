/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Strategy pattern implementation for ball detection modes
// Phase 3.1 modular refactoring

#include "search_strategy.h"
#include "hough_detector.h"

namespace golf_sim {

SearchStrategy::DetectionParams SearchStrategy::GetParamsForMode(Mode mode) {
    DetectionParams params;

    switch (mode) {
        case kFindPlacedBall:
            // Placed ball: Single stationary ball at rest before shot
            params.hough_dp_param1 = HoughDetector::kPlacedBallHoughDpParam1;
            params.canny_lower = HoughDetector::kPlacedBallCannyLower;
            params.canny_upper = HoughDetector::kPlacedBallCannyUpper;
            params.param1 = HoughDetector::kPlacedBallCurrentParam1;
            params.starting_param2 = HoughDetector::kPlacedBallStartingParam2;
            params.min_param2 = HoughDetector::kPlacedBallMinParam2;
            params.max_param2 = HoughDetector::kPlacedBallMaxParam2;
            params.param2_increment = HoughDetector::kPlacedBallParam2Increment;
            params.min_hough_return_circles = HoughDetector::kPlacedMinHoughReturnCircles;
            params.max_hough_return_circles = HoughDetector::kPlacedMaxHoughReturnCircles;
            params.pre_canny_blur_size = HoughDetector::kPlacedPreCannyBlurSize;
            params.pre_hough_blur_size = HoughDetector::kPlacedPreHoughBlurSize;

            params.use_clahe = HoughDetector::kUseCLAHEProcessing;
            params.clahe_clip_limit = HoughDetector::kCLAHEClipLimit;
            params.clahe_tiles_grid_size = HoughDetector::kCLAHETilesGridSize;

            params.minimum_search_radius = -1;  // Not constrained
            params.maximum_search_radius = -1;  // Not constrained

            params.narrowing_radii_min_ratio = HoughDetector::kPlacedNarrowingRadiiMinRatio;
            params.narrowing_radii_max_ratio = HoughDetector::kPlacedNarrowingRadiiMaxRatio;
            params.narrowing_starting_param2 = HoughDetector::kPlacedNarrowingStartingParam2;
            params.narrowing_radii_dp_param = HoughDetector::kPlacedNarrowingRadiiDpParam;
            params.narrowing_param1 = HoughDetector::kPlacedNarrowingParam1;
            params.narrowing_radii_param2 = 0.0;  // Not used for placed
            params.narrowing_pre_canny_blur_size = HoughDetector::kPlacedPreCannyBlurSize;
            params.narrowing_pre_hough_blur_size = HoughDetector::kPlacedPreHoughBlurSize;

            params.use_dynamic_radii_adjustment = HoughDetector::kUseDynamicRadiiAdjustment;
            params.num_radii_to_average = HoughDetector::kNumberRadiiToAverageForDynamicAdjustment;
            break;

        case kStrobed:
            // Strobed ball: Multiple balls captured with strobe flash
            if (HoughDetector::kStrobedBallsUseAltHoughAlgorithm) {
                // Alternative Hough algorithm (HOUGH_GRADIENT_ALT)
                params.hough_dp_param1 = HoughDetector::kStrobedBallsAltHoughDpParam1;
                params.canny_lower = HoughDetector::kStrobedBallsAltCannyLower;
                params.canny_upper = HoughDetector::kStrobedBallsAltCannyUpper;
                params.param1 = HoughDetector::kStrobedBallsAltCurrentParam1;
                params.starting_param2 = HoughDetector::kStrobedBallsAltStartingParam2;
                params.min_param2 = HoughDetector::kStrobedBallsAltMinParam2;
                params.max_param2 = HoughDetector::kStrobedBallsAltMaxParam2;
                params.param2_increment = HoughDetector::kStrobedBallsAltParam2Increment;
                params.pre_canny_blur_size = HoughDetector::kStrobedBallsAltPreCannyBlurSize;
                params.pre_hough_blur_size = HoughDetector::kStrobedBallsAltPreHoughBlurSize;
            } else {
                // Standard Hough algorithm
                params.hough_dp_param1 = HoughDetector::kStrobedBallsHoughDpParam1;
                params.canny_lower = HoughDetector::kStrobedBallsCannyLower;
                params.canny_upper = HoughDetector::kStrobedBallsCannyUpper;
                params.param1 = HoughDetector::kStrobedBallsCurrentParam1;
                params.starting_param2 = HoughDetector::kStrobedBallsStartingParam2;
                params.min_param2 = HoughDetector::kStrobedBallsMinParam2;
                params.max_param2 = HoughDetector::kStrobedBallsMaxParam2;
                params.param2_increment = HoughDetector::kStrobedBallsParam2Increment;
                params.pre_canny_blur_size = HoughDetector::kStrobedBallsPreCannyBlurSize;
                params.pre_hough_blur_size = HoughDetector::kStrobedBallsPreHoughBlurSize;
            }

            params.min_hough_return_circles = HoughDetector::kStrobedBallsMinHoughReturnCircles;
            params.max_hough_return_circles = HoughDetector::kStrobedBallsMaxHoughReturnCircles;

            params.use_clahe = HoughDetector::kUseCLAHEProcessing;
            params.clahe_clip_limit = HoughDetector::kCLAHEClipLimit;
            params.clahe_tiles_grid_size = HoughDetector::kCLAHETilesGridSize;

            params.minimum_search_radius = -1;  // Not constrained
            params.maximum_search_radius = -1;  // Not constrained

            params.narrowing_radii_min_ratio = HoughDetector::kStrobedNarrowingRadiiMinRatio;
            params.narrowing_radii_max_ratio = HoughDetector::kStrobedNarrowingRadiiMaxRatio;
            params.narrowing_starting_param2 = 0.0;  // Not used for strobed
            params.narrowing_radii_dp_param = HoughDetector::kStrobedNarrowingRadiiDpParam;
            params.narrowing_param1 = 0.0;  // Not used for strobed
            params.narrowing_radii_param2 = HoughDetector::kStrobedNarrowingRadiiParam2;
            params.narrowing_pre_canny_blur_size = params.pre_canny_blur_size;
            params.narrowing_pre_hough_blur_size = params.pre_hough_blur_size;

            params.use_dynamic_radii_adjustment = HoughDetector::kUseDynamicRadiiAdjustment;
            params.num_radii_to_average = HoughDetector::kNumberRadiiToAverageForDynamicAdjustment;
            break;

        case kExternallyStrobed:
            // Externally strobed: Using external strobe trigger (comparison mode)
            params.hough_dp_param1 = HoughDetector::kExternallyStrobedEnvHoughDpParam1;
            params.canny_lower = HoughDetector::kExternallyStrobedEnvCannyLower;
            params.canny_upper = HoughDetector::kExternallyStrobedEnvCannyUpper;
            params.param1 = HoughDetector::kExternallyStrobedEnvCurrentParam1;
            params.starting_param2 = HoughDetector::kExternallyStrobedEnvStartingParam2;
            params.min_param2 = HoughDetector::kExternallyStrobedEnvMinParam2;
            params.max_param2 = HoughDetector::kExternallyStrobedEnvMaxParam2;
            params.param2_increment = HoughDetector::kExternallyStrobedEnvParam2Increment;
            params.min_hough_return_circles = HoughDetector::kExternallyStrobedEnvMinHoughReturnCircles;
            params.max_hough_return_circles = HoughDetector::kExternallyStrobedEnvMaxHoughReturnCircles;
            params.pre_canny_blur_size = HoughDetector::kExternallyStrobedEnvPreCannyBlurSize;
            params.pre_hough_blur_size = HoughDetector::kExternallyStrobedEnvPreHoughBlurSize;

            params.use_clahe = HoughDetector::kExternallyStrobedUseCLAHEProcessing;
            params.clahe_clip_limit = HoughDetector::kExternallyStrobedCLAHEClipLimit;
            params.clahe_tiles_grid_size = HoughDetector::kExternallyStrobedCLAHETilesGridSize;

            params.minimum_search_radius = HoughDetector::kExternallyStrobedEnvMinimumSearchRadius;
            params.maximum_search_radius = HoughDetector::kExternallyStrobedEnvMaximumSearchRadius;

            params.narrowing_radii_min_ratio = HoughDetector::kStrobedNarrowingRadiiMinRatio;
            params.narrowing_radii_max_ratio = HoughDetector::kStrobedNarrowingRadiiMaxRatio;
            params.narrowing_starting_param2 = HoughDetector::kExternallyStrobedEnvNarrowingParam2;
            params.narrowing_radii_dp_param = HoughDetector::kExternallyStrobedEnvNarrowingDpParam;
            params.narrowing_param1 = params.param1;
            params.narrowing_radii_param2 = HoughDetector::kExternallyStrobedEnvNarrowingParam2;
            params.narrowing_pre_canny_blur_size = HoughDetector::kExternallyStrobedEnvNarrowingPreCannyBlurSize;
            params.narrowing_pre_hough_blur_size = HoughDetector::kExternallyStrobedEnvNarrowingPreHoughBlurSize;

            params.use_dynamic_radii_adjustment = HoughDetector::kUseDynamicRadiiAdjustment;
            params.num_radii_to_average = HoughDetector::kNumberRadiiToAverageForDynamicAdjustment;
            break;

        case kPutting:
            // Putting mode: Shorter range shots on putting green
            params.hough_dp_param1 = HoughDetector::kPuttingHoughDpParam1;
            params.canny_lower = 0.0;  // Not specified in original code
            params.canny_upper = 0.0;  // Not specified in original code
            params.param1 = HoughDetector::kPuttingBallCurrentParam1;
            params.starting_param2 = HoughDetector::kPuttingBallStartingParam2;
            params.min_param2 = HoughDetector::kPuttingBallMinParam2;
            params.max_param2 = HoughDetector::kPuttingBallMaxParam2;
            params.param2_increment = HoughDetector::kPuttingBallParam2Increment;
            params.min_hough_return_circles = HoughDetector::kPuttingMinHoughReturnCircles;
            params.max_hough_return_circles = HoughDetector::kPuttingMaxHoughReturnCircles;
            params.pre_canny_blur_size = 0;  // Not specified
            params.pre_hough_blur_size = HoughDetector::kPuttingPreHoughBlurSize;

            params.use_clahe = HoughDetector::kUseCLAHEProcessing;
            params.clahe_clip_limit = HoughDetector::kCLAHEClipLimit;
            params.clahe_tiles_grid_size = HoughDetector::kCLAHETilesGridSize;

            params.minimum_search_radius = -1;  // Not constrained
            params.maximum_search_radius = -1;  // Not constrained

            // Putting uses similar narrowing to placed ball
            params.narrowing_radii_min_ratio = HoughDetector::kPlacedNarrowingRadiiMinRatio;
            params.narrowing_radii_max_ratio = HoughDetector::kPlacedNarrowingRadiiMaxRatio;
            params.narrowing_starting_param2 = HoughDetector::kPlacedNarrowingStartingParam2;
            params.narrowing_radii_dp_param = HoughDetector::kPlacedNarrowingRadiiDpParam;
            params.narrowing_param1 = HoughDetector::kPlacedNarrowingParam1;
            params.narrowing_radii_param2 = 0.0;
            params.narrowing_pre_canny_blur_size = 0;
            params.narrowing_pre_hough_blur_size = params.pre_hough_blur_size;

            params.use_dynamic_radii_adjustment = HoughDetector::kUseDynamicRadiiAdjustment;
            params.num_radii_to_average = HoughDetector::kNumberRadiiToAverageForDynamicAdjustment;
            break;

        case kUnknown:
        default:
            // Default to placed ball parameters
            return GetParamsForMode(kFindPlacedBall);
    }

    return params;
}

bool SearchStrategy::RequiresPreprocessing(Mode mode) {
    switch (mode) {
        case kStrobed:
        case kExternallyStrobed:
            return true;  // These modes use CLAHE preprocessing
        case kFindPlacedBall:
        case kPutting:
        case kUnknown:
        default:
            return false;  // Placed ball typically doesn't need CLAHE
    }
}

bool SearchStrategy::UseAlternativeHoughAlgorithm(Mode mode) {
    if (mode == kStrobed) {
        return HoughDetector::kStrobedBallsUseAltHoughAlgorithm;
    }
    return false;
}

const char* SearchStrategy::GetModeName(Mode mode) {
    switch (mode) {
        case kFindPlacedBall:
            return "PlacedBall";
        case kStrobed:
            return "Strobed";
        case kExternallyStrobed:
            return "ExternallyStrobed";
        case kPutting:
            return "Putting";
        case kUnknown:
        default:
            return "Unknown";
    }
}

bool SearchStrategy::UseBestCircleRefinement(Mode mode) {
    // Best circle refinement is used for all modes if enabled globally
    return HoughDetector::kUseBestCircleRefinement;
}

std::shared_ptr<SearchStrategy> SearchStrategy::CreateStrategy(Mode mode) {
    // Future enhancement: Return concrete strategy subclasses
    // For now, SearchStrategy is stateless and uses static methods
    return nullptr;
}

} // namespace golf_sim
