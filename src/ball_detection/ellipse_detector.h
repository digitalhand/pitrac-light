/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Ellipse-based ball detection using YAED and contour fitting algorithms.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "golf_ball.h"

namespace golf_sim {

/**
 * EllipseDetector - Ellipse-based detection for golf balls
 *
 * Provides two ellipse detection algorithms:
 * 1. YAED (Yet Another Ellipse Detector) - Fornaciari algorithm
 * 2. Contour-based ellipse fitting using OpenCV's fitEllipse
 *
 * Used when Hough circle detection produces elliptical results
 * (e.g., ball captured at an angle or with motion blur)
 */
class EllipseDetector {
public:

    /**
     * Finds the largest ellipse using YAED (Yet Another Ellipse Detector) algorithm
     *
     * This is the Fornaciari ellipse detection method, which is more robust
     * for detecting ellipses directly from edge pixels.
     *
     * @param img Input grayscale image containing the ball
     * @param reference_ball_circle Approximate ball location and radius
     * @param mask_radius Radius for masking operations
     * @return Detected ellipse as cv::RotatedRect (returns empty rect if detection fails)
     */
    static cv::RotatedRect FindBestEllipseFornaciari(cv::Mat& img,
                                                     const GsCircle& reference_ball_circle,
                                                     int mask_radius);

    /**
     * Finds the largest ellipse using contour-based fitting
     *
     * Performs Canny edge detection, extracts contours, and fits ellipses
     * to each contour. Returns the largest valid ellipse.
     *
     * @param img Input grayscale image containing the ball
     * @param reference_ball_circle Approximate ball location and radius
     * @param mask_radius Radius for masking operations
     * @return Detected ellipse as cv::RotatedRect (returns empty rect if detection fails)
     */
    static cv::RotatedRect FindLargestEllipse(cv::Mat& img,
                                              const GsCircle& reference_ball_circle,
                                              int mask_radius);

private:
    // No private methods yet - may add ellipse validation logic in future
};

} // namespace golf_sim
