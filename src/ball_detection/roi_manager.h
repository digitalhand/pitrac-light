/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Region of interest extraction and ball movement detection for golf ball tracking.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "golf_ball.h"


namespace golf_sim {

// Forward declaration to avoid circular dependency with gs_camera.h
class GolfSimCamera;

class ROIManager {
public:

    // Returns the area of interest in front of the ball (ball-fly direction).
    static cv::Rect GetAreaOfInterest(const GolfBall& ball, const cv::Mat& img);

    // Checks whether a ball is present in the image.
    // Currently a stub that always returns true.
    static bool BallIsPresent(const cv::Mat& img);

    // Waits for movement near the ball (e.g., club swing) and returns the first
    // image containing the movement. Ignores initial startup frames.
    static bool WaitForBallMovement(GolfSimCamera& c,
                                    cv::Mat& firstMovementImage,
                                    const GolfBall& ball,
                                    const long waitTimeSecs);
};

}
