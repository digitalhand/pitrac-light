/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// HSV color mask generation for golf ball detection.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "golf_ball.h"


namespace golf_sim {

class ColorFilter {
public:

    // Returns a mask with 1 bits wherever the corresponding pixel is OUTSIDE the upper/lower HSV range.
    // Handles hue wrap-around at the 180-degree boundary for reddish colors.
    static cv::Mat GetColorMaskImage(const cv::Mat& hsvImage,
                                     const GsColorTriplet input_lowerHsv,
                                     const GsColorTriplet input_upperHsv,
                                     double wideningAmount = 0.0);

    // Convenience overload that extracts HSV ranges from the ball's color.
    static cv::Mat GetColorMaskImage(const cv::Mat& hsvImage,
                                     const GolfBall& ball,
                                     double wideningAmount = 0.0);
};

}
