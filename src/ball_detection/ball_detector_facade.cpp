/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Ball detection facade implementation - orchestrates all detection modules
// Phase 3.1 modular refactoring

#include "ball_detector_facade.h"

#include <chrono>
#include <opencv2/imgproc.hpp>

#include "hough_detector.h"
#include "ellipse_detector.h"
#include "color_filter.h"
#include "roi_manager.h"
#include "search_strategy.h"
#include "utils/logging_tools.h"
#include "utils/cv_utils.h"
#include "gs_options.h"

// Edge detection (for putting mode)
#include "EDPF.h"

namespace golf_sim {

// Constants from original implementation
const bool PREBLUR_IMAGE = false;
const bool IS_COLOR_MASKING = false;
const bool FINAL_BLUR = true;

bool BallDetectorFacade::GetBall(const cv::Mat& img,
                                 const GolfBall& baseBallWithSearchParams,
                                 std::vector<GolfBall>& return_balls,
                                 cv::Rect& expectedBallArea,
                                 SearchStrategy::Mode search_mode,
                                 bool chooseLargestFinalBall,
                                 bool report_find_failures) {

    auto start_time = std::chrono::high_resolution_clock::now();
    GS_LOG_TRACE_MSG(trace, "BallDetectorFacade::GetBall - mode: " +
                     std::string(SearchStrategy::GetModeName(search_mode)));

    if (img.empty()) {
        GS_LOG_MSG(error, "GetBall called with empty image");
        return false;
    }

    // Check if ONNX detection is enabled (experimental path)
    // TODO: This would check kDetectionMethod == "experimental"
    // For now, always use Hough detection

    return GetBallHough(img, baseBallWithSearchParams, return_balls,
                       expectedBallArea, search_mode,
                       chooseLargestFinalBall, report_find_failures);
}

bool BallDetectorFacade::GetBallONNX(const cv::Mat& img,
                                    const GolfBall& baseBallWithSearchParams,
                                    std::vector<GolfBall>& return_balls,
                                    SearchStrategy::Mode search_mode) {
    GS_LOG_TRACE_MSG(trace, "BallDetectorFacade::GetBallONNX - Not yet implemented");

    // TODO: Implement ONNX detection path
    // This would call DetectBallsONNX from HoughDetector
    // and convert GsCircle results to GolfBall objects

    return false;
}

bool BallDetectorFacade::GetBallHough(const cv::Mat& img,
                                     const GolfBall& baseBallWithSearchParams,
                                     std::vector<GolfBall>& return_balls,
                                     cv::Rect& expectedBallArea,
                                     SearchStrategy::Mode search_mode,
                                     bool chooseLargestFinalBall,
                                     bool report_find_failures) {

    GS_LOG_TRACE_MSG(trace, "BallDetectorFacade::GetBallHough - mode: " +
                     std::string(SearchStrategy::GetModeName(search_mode)));

    // Get mode-specific detection parameters
    SearchStrategy::DetectionParams params = SearchStrategy::GetParamsForMode(search_mode);

    // Step 1: Convert to grayscale and optionally blur
    cv::Mat blurImg;
    if (PREBLUR_IMAGE) {
        cv::GaussianBlur(img, blurImg, cv::Size(7, 7), 0);
        LoggingTools::DebugShowImage("Pre-blurred image", blurImg);
    } else {
        blurImg = img;  // No clone needed - read-only usage
    }

    // Step 2: Optional color masking (currently disabled in constants)
    cv::Mat color_mask_image;
    if (IS_COLOR_MASKING) {
        cv::Mat hsvImage;
        cv::cvtColor(blurImg, hsvImage, cv::COLOR_BGR2HSV);
        color_mask_image = ColorFilter::GetColorMaskImage(hsvImage, baseBallWithSearchParams);
        LoggingTools::DebugShowImage("Color mask", color_mask_image);
    }

    // Step 3: Convert to grayscale for Hough detection
    cv::Mat grayImage;
    cv::cvtColor(blurImg, grayImage, cv::COLOR_BGR2GRAY);

    // Step 4: Apply color mask if enabled
    cv::Mat search_image;
    if (IS_COLOR_MASKING && !color_mask_image.empty()) {
        cv::bitwise_and(grayImage, color_mask_image, search_image);
        LoggingTools::DebugShowImage("Color-masked search image", search_image);
    } else {
        search_image = grayImage;  // No clone needed - grayImage not used after this
    }

    // Step 5: Mode-specific preprocessing
    if (!PreprocessForMode(search_image, search_mode)) {
        GS_LOG_MSG(error, "Preprocessing failed for mode: " +
                   std::string(SearchStrategy::GetModeName(search_mode)));
        return false;
    }

    LoggingTools::DebugShowImage("Final preprocessed search image", search_image);

    // Step 6: Determine search radius constraints
    // TODO: This should be extracted to a separate method that handles min/max radius calculation
    // For now, use simple heuristics
    int minimum_search_radius = int(search_image.rows / 15);  // Placeholder
    int maximum_search_radius = int(search_image.rows / 6);   // Placeholder

    // Step 7: Handle ROI extraction if expectedBallArea is provided
    cv::Point offset_sub_to_full(0, 0);
    cv::Point offset_full_to_sub(0, 0);
    cv::Mat final_search_image;

    if (expectedBallArea.tl().x != 0 || expectedBallArea.tl().y != 0 ||
        expectedBallArea.br().x != 0 || expectedBallArea.br().y != 0) {
        final_search_image = CvUtils::GetSubImage(search_image, expectedBallArea,
                                                   offset_sub_to_full, offset_full_to_sub);
    } else {
        final_search_image = search_image;
    }

    // Step 8: Perform iterative Hough circle detection
    std::vector<GsCircle> circles;
    int finalNumberOfFoundCircles = 0;

    // Adaptive Hough parameter adjustment loop
    bool done = false;
    double currentParam2 = params.starting_param2;
    int priorNumCircles = 0;
    bool currentlyLooseningSearch = false;

    // Determine minimum distance between circles based on mode
    double minimum_distance = minimum_search_radius * 0.5;
    if (search_mode == SearchStrategy::kStrobed) {
        minimum_distance = minimum_search_radius * 0.3;
    } else if (search_mode == SearchStrategy::kExternallyStrobed) {
        minimum_distance = minimum_search_radius * 0.2;
    }

    // Determine Hough algorithm mode
    cv::HoughModes hough_mode = cv::HOUGH_GRADIENT_ALT;
    if (search_mode != SearchStrategy::kFindPlacedBall &&
        !SearchStrategy::UseAlternativeHoughAlgorithm(search_mode)) {
        hough_mode = cv::HOUGH_GRADIENT;
    }

    GS_LOG_TRACE_MSG(trace, "Starting adaptive Hough parameter adjustment loop");

    while (!done) {
        // Round radii to even numbers for consistency
        minimum_search_radius = CvUtils::RoundAndMakeEven(minimum_search_radius);
        maximum_search_radius = CvUtils::RoundAndMakeEven(maximum_search_radius);

        GS_LOG_TRACE_MSG(trace, "Executing HoughCircles with dp=" + std::to_string(params.hough_dp_param1) +
            ", minDist=" + std::to_string(minimum_distance) +
            ", param1=" + std::to_string(params.param1) +
            ", param2=" + std::to_string(currentParam2) +
            ", minRadius=" + std::to_string(minimum_search_radius) +
            ", maxRadius=" + std::to_string(maximum_search_radius));

        std::vector<GsCircle> test_circles;
        cv::HoughCircles(final_search_image, test_circles, hough_mode,
                        params.hough_dp_param1, minimum_distance,
                        params.param1, currentParam2,
                        minimum_search_radius, maximum_search_radius);

        // Save prior circle count
        priorNumCircles = circles.empty() ? 0 : static_cast<int>(circles.size());

        int numCircles = static_cast<int>(test_circles.size());
        if (numCircles > 0) {
            GS_LOG_TRACE_MSG(trace, "Hough found " + std::to_string(numCircles) + " circles");
        }

        // Remove concentric circles
        HoughDetector::RemoveSmallestConcentricCircles(test_circles);
        numCircles = static_cast<int>(test_circles.size());

        // Check if we found acceptable number of circles
        if (numCircles >= params.min_hough_return_circles &&
            numCircles <= params.max_hough_return_circles) {
            circles = test_circles;
            finalNumberOfFoundCircles = numCircles;
            done = true;
            break;
        }

        // Too many circles - tighten parameters
        if (numCircles > params.max_hough_return_circles) {
            GS_LOG_TRACE_MSG(trace, "Too many circles (" + std::to_string(numCircles) + ")");

            if ((priorNumCircles == 0) && (currentParam2 != params.starting_param2)) {
                // Had none before, have too many now - accept it
                circles = test_circles;
                finalNumberOfFoundCircles = numCircles;
                done = true;
            } else if (currentParam2 >= params.max_param2) {
                // Can't tighten anymore
                circles = test_circles;
                finalNumberOfFoundCircles = numCircles;
                done = true;
            } else {
                // Tighten by increasing param2
                circles = test_circles;
                currentParam2 += params.param2_increment;
                currentlyLooseningSearch = false;
            }
        }
        // Too few circles - loosen parameters
        else {
            if (numCircles == 0 && priorNumCircles == 0) {
                // No circles found yet
                if (currentParam2 <= params.min_param2) {
                    // Can't loosen anymore
                    if (report_find_failures) {
                        GS_LOG_MSG(error, "Could not find any balls");
                    }
                    done = true;
                } else {
                    // Loosen by decreasing param2
                    currentParam2 -= params.param2_increment;
                    currentlyLooseningSearch = true;
                }
            } else if (((numCircles > 0 && numCircles < params.min_hough_return_circles) &&
                       priorNumCircles == 0) || currentlyLooseningSearch) {
                // Found some but not enough
                if (currentParam2 <= params.min_param2) {
                    circles = test_circles;
                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                } else {
                    currentParam2 -= params.param2_increment;
                    currentlyLooseningSearch = true;
                    circles = test_circles;
                }
            } else if (numCircles == 0 && priorNumCircles > 0) {
                // Tightened too much, return prior results
                finalNumberOfFoundCircles = priorNumCircles;
                done = true;
            }
        }
    }

    if (finalNumberOfFoundCircles == 0) {
        if (report_find_failures) {
            GS_LOG_MSG(warning, "No circles found after parameter adjustment");
        }
        return false;
    }

    GS_LOG_TRACE_MSG(trace, "Final circle count: " + std::to_string(finalNumberOfFoundCircles));

    // Translate circles back to full image coordinates
    for (auto& c : circles) {
        c[0] += offset_sub_to_full.x;
        c[1] += offset_sub_to_full.y;
    }

    // Step 9: Filter and score candidates
    return FilterAndScoreCandidates(circles, baseBallWithSearchParams, return_balls, img, search_mode, report_find_failures);
}

bool BallDetectorFacade::PreprocessForMode(cv::Mat& search_image, SearchStrategy::Mode mode) {
    SearchStrategy::DetectionParams params = SearchStrategy::GetParamsForMode(mode);

    switch (mode) {
        case SearchStrategy::kFindPlacedBall: {
            // Placed ball: Blur + Canny + Blur
            cv::GaussianBlur(search_image, search_image,
                           cv::Size(params.pre_canny_blur_size, params.pre_canny_blur_size), 0);

            LoggingTools::DebugShowImage("Placed Ball - Ready for Edge Detection", search_image);

            cv::Mat cannyOutput;
            cv::Canny(search_image, cannyOutput, params.canny_lower, params.canny_upper);
            LoggingTools::DebugShowImage("Canny output", cannyOutput);

            cv::GaussianBlur(cannyOutput, search_image,
                           cv::Size(params.pre_hough_blur_size, params.pre_hough_blur_size), 0);
            return true;
        }

        case SearchStrategy::kStrobed:
        case SearchStrategy::kExternallyStrobed: {
            // Strobed: Use HoughDetector's CLAHE preprocessing
            return HoughDetector::PreProcessStrobedImage(search_image,
                   mode == SearchStrategy::kStrobed ?
                   HoughDetector::kStrobed : HoughDetector::kExternallyStrobed);
        }

        case SearchStrategy::kPutting: {
            // Putting: Median blur + EDPF edge detection
            cv::medianBlur(search_image, search_image, params.pre_hough_blur_size);
            LoggingTools::DebugShowImage("Putting - Ready for Edge Detection", search_image);

            EDPF edgeDetector(search_image);
            cv::Mat edgeImage = edgeDetector.getEdgeImage();
            edgeImage = edgeImage * -1 + 255;  // Invert
            search_image = edgeImage;

            cv::GaussianBlur(search_image, search_image, cv::Size(5, 5), 0);
            return true;
        }

        case SearchStrategy::kUnknown:
        default:
            GS_LOG_MSG(error, "Invalid search mode for preprocessing");
            return false;
    }
}

bool BallDetectorFacade::FilterAndScoreCandidates(const std::vector<GsCircle>& circles,
                                                  const GolfBall& baseBall,
                                                  std::vector<GolfBall>& return_balls,
                                                  const cv::Mat& rgbImg,
                                                  SearchStrategy::Mode search_mode,
                                                  bool report_find_failures) {
    GS_LOG_TRACE_MSG(trace, "FilterAndScoreCandidates - Processing " +
                     std::to_string(circles.size()) + " candidates");

    if (circles.empty()) {
        if (report_find_failures) {
            GS_LOG_MSG(error, "No circles to filter");
        }
        return false;
    }

    // Constants
    const int MIN_BALL_CANDIDATE_RADIUS = 10;
    const int CANDIDATE_BALL_COLOR_TOLERANCE = 50;
    const int MAX_CIRCLES_TO_EVALUATE = 200;

    // Determine expected ball color
    bool expectedBallColorExists = false;
    GsColorTriplet expectedBallRGBAverage;
    GsColorTriplet expectedBallRGBMedian;
    GsColorTriplet expectedBallRGBStd;

    if (baseBall.average_color_ != GsColorTriplet(0, 0, 0)) {
        expectedBallRGBAverage = baseBall.average_color_;
        expectedBallRGBMedian = baseBall.median_color_;
        expectedBallRGBStd = baseBall.std_color_;
        expectedBallColorExists = true;
    } else {
        // Use center of HSV range as expected color
        expectedBallRGBAverage = baseBall.GetRGBCenterFromHSVRange();
        expectedBallRGBMedian = expectedBallRGBAverage;
        expectedBallRGBStd = GsColorTriplet(0, 0, 0);
        expectedBallColorExists = false;
    }

    GS_LOG_TRACE_MSG(trace, "Expected ball color (BGR): " +
                     LoggingTools::FormatGsColorTriplet(expectedBallRGBAverage));

    // Structure for candidate scoring
    struct CircleCandidateListElement {
        std::string name;
        GsCircle circle;
        double calculated_color_difference;
        int found_radius;
        GsColorTriplet avg_RGB;
        float rgb_avg_diff;
        float rgb_median_diff;
        float rgb_std_diff;
    };

    std::vector<CircleCandidateListElement> foundCircleList;

    // Score each candidate circle
    int i = 0;
    for (const auto& c : circles) {
        i++;
        if (i > MAX_CIRCLES_TO_EVALUATE) break;

        int found_radius = static_cast<int>(std::round(c[2]));

        // Skip tiny circles
        if (found_radius < MIN_BALL_CANDIDATE_RADIUS) {
            GS_LOG_TRACE_MSG(trace, "Skipping too-small circle of radius " + std::to_string(found_radius));
            continue;
        }

        double calculated_color_difference = 0;
        GsColorTriplet avg_RGB(0, 0, 0);
        GsColorTriplet medianRGB(0, 0, 0);
        GsColorTriplet stdRGB(0, 0, 0);
        float rgb_avg_diff = 0.0f;
        float rgb_median_diff = 0.0f;
        float rgb_std_diff = 0.0f;

        // Calculate color statistics if needed
        if (expectedBallColorExists || search_mode == SearchStrategy::kPutting) {
            std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(rgbImg, c);
            avg_RGB = stats[0];
            medianRGB = stats[1];
            stdRGB = stats[2];

            GS_LOG_TRACE_MSG(trace, "Circle " + std::to_string(i) + " radius=" + std::to_string(found_radius) +
                           " avgRGB=" + LoggingTools::FormatGsColorTriplet(avg_RGB));

            // Calculate color distance
            rgb_avg_diff = CvUtils::ColorDistance(avg_RGB, expectedBallRGBAverage);
            rgb_median_diff = CvUtils::ColorDistance(medianRGB, expectedBallRGBMedian);
            rgb_std_diff = CvUtils::ColorDistance(stdRGB, expectedBallRGBStd);

            // Combined score: color match + consistency + ordering penalty
            calculated_color_difference = std::pow(rgb_avg_diff, 2) +
                                         20.0 * std::pow(rgb_std_diff, 2) +
                                         200.0 * std::pow(10 * i, 3);
        }

        foundCircleList.push_back(CircleCandidateListElement{
            "Ball " + std::to_string(i),
            c,
            calculated_color_difference,
            found_radius,
            avg_RGB,
            rgb_avg_diff,
            rgb_median_diff,
            rgb_std_diff
        });
    }

    if (foundCircleList.empty()) {
        if (report_find_failures) {
            GS_LOG_MSG(error, "No valid circle candidates after filtering");
        }
        return false;
    }

    // Sort by color difference (if color matching enabled and not strobed mode)
    if (search_mode != SearchStrategy::kStrobed && expectedBallColorExists) {
        std::sort(foundCircleList.begin(), foundCircleList.end(),
                 [](const CircleCandidateListElement& a, const CircleCandidateListElement& b) {
                     return a.calculated_color_difference < b.calculated_color_difference;
                 });
        GS_LOG_TRACE_MSG(trace, "Sorted candidates by color match");
    }

    // For strobed mode: filter by color tolerance and sort by radius
    std::vector<CircleCandidateListElement> finalCandidates;

    if (search_mode == SearchStrategy::kStrobed && expectedBallColorExists) {
        const CircleCandidateListElement& firstCircle = foundCircleList.front();
        float maxRGBDistance = firstCircle.calculated_color_difference + CANDIDATE_BALL_COLOR_TOLERANCE;

        std::vector<CircleCandidateListElement> candidates;
        for (const auto& e : foundCircleList) {
            if (e.calculated_color_difference <= maxRGBDistance) {
                candidates.push_back(e);
            }
        }

        GS_LOG_TRACE_MSG(trace, "After color filtering: " + std::to_string(candidates.size()) + " candidates");

        // Sort by radius (largest first)
        std::sort(candidates.begin(), candidates.end(),
                 [](const CircleCandidateListElement& a, const CircleCandidateListElement& b) {
                     return a.found_radius > b.found_radius;
                 });

        finalCandidates = candidates;
    } else {
        finalCandidates = foundCircleList;
    }

    if (finalCandidates.empty()) {
        if (report_find_failures) {
            GS_LOG_MSG(error, "No final candidates after filtering");
        }
        return false;
    }

    // Convert to GolfBall objects
    return_balls.clear();
    int index = 0;
    for (const auto& c : finalCandidates) {
        GolfBall ball;
        ball.quality_ranking = index;
        ball.set_circle(c.circle);
        ball.measured_radius_pixels_ = c.found_radius;
        ball.average_color_ = c.avg_RGB;
        ball.median_color_ = c.avg_RGB;  // Using avg as placeholder
        ball.std_color_ = GsColorTriplet(0, 0, 0);
        return_balls.push_back(ball);
        index++;
    }

    GS_LOG_TRACE_MSG(trace, "Returning " + std::to_string(return_balls.size()) + " balls");
    return true;
}

bool BallDetectorFacade::RefineBestCircle(const cv::Mat& gray_image,
                                         const GolfBall& candidate,
                                         bool chooseLargest,
                                         GsCircle& refined_circle) {
    GS_LOG_TRACE_MSG(trace, "RefineBestCircle");

    // Delegate to HoughDetector's best circle refinement
    return HoughDetector::DetermineBestCircle(gray_image, candidate,
                                             chooseLargest, refined_circle);
}

} // namespace golf_sim
