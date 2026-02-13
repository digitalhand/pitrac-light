/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include "ball_detection/color_filter.h"
#include "utils/cv_utils.h"
#include "utils/logging_tools.h"


namespace golf_sim {

    static const double kColorMaskWideningAmount = 35;

    // Returns a mask with 1 bits wherever the corresponding pixel is OUTSIDE the upper/lower HSV range
    cv::Mat ColorFilter::GetColorMaskImage(const cv::Mat& hsvImage,
                                             const GsColorTriplet input_lowerHsv,
                                             const GsColorTriplet input_upperHsv,
                                             double wideningAmount) {

        GsColorTriplet lowerHsv = input_lowerHsv;
        GsColorTriplet upperHsv = input_upperHsv;

        // TBD - Straighten out double versus uchar/int here

        for (int i = 0; i < 3; i++) {
            lowerHsv[i] -= kColorMaskWideningAmount;   // (int)std::round(((double)lowerHsv[i] * kColorMaskWideningRatio));
            upperHsv[i] += kColorMaskWideningAmount;   //(int)std::round(((double)upperHsv[i] * kColorMaskWideningRatio));
        }


        // Ensure we didn't go too big on the S or V upper bound (which is 255)
        upperHsv[1] = std::min((int)upperHsv[1], 255);
        upperHsv[2] = std::min((int)upperHsv[2], 255);

        // Because we are creating a binary mask, it should be CV_8U or CV_8S (TBD - I think?)
        cv::Mat color_mask_image_(hsvImage.rows, hsvImage.cols, CV_8U, cv::Scalar(0));
        //        CvUtils::SetMatSize(hsvImage, color_mask_image_);
        // color_mask_image_ = hsvImage.clone();

        // We will need TWO masks if the hue range crosses over the 180 - degreee "loop" point for reddist colors
        // TBD - should we convert the ranges to scalars?
        if ((lowerHsv[0] >= 0) && (upperHsv[0] <= (float)CvUtils::kOpenCvHueMax)) {
            cv::inRange(hsvImage, cv::Scalar(lowerHsv), cv::Scalar(upperHsv), color_mask_image_);
        }
        else {
            // 'First' and 'Second' refer to the Hsv triplets that will be used for he first and second masks
            cv::Vec3f firstLowerHsv;
            cv::Vec3f secondLowerHsv;
            cv::Vec3f firstUpperHsv;
            cv::Vec3f secondUpperHsv;

            cv::Vec3f leftMostLowerHsv;
            cv::Vec3f leftMostUpperHsv;
            cv::Vec3f rightMostLowerHsv;
            cv::Vec3f rightMostUpperHsv;

            // Check the hue range - does it loop around 180 degrees?
            if (lowerHsv[0] < 0) {
                // the lower hue is below 0
                leftMostLowerHsv = cv::Vec3f(0.f, (float)lowerHsv[1], (float)lowerHsv[2]);
                leftMostUpperHsv = cv::Vec3f((float)upperHsv[0], (float)upperHsv[1], (float)upperHsv[2]);
                rightMostLowerHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax + (float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }
            else {
                // the upper hue is over 180 degrees
                leftMostLowerHsv = cv::Vec3f(0.f, (float)lowerHsv[1], (float)lowerHsv[2]);
                leftMostUpperHsv = cv::Vec3f((float)upperHsv[0] - 180.f, (float)upperHsv[1], (float)upperHsv[2]);
                rightMostLowerHsv = cv::Vec3f((float)lowerHsv[0], (float)lowerHsv[1], (float)lowerHsv[2]);
                rightMostUpperHsv = cv::Vec3f((float)CvUtils::kOpenCvHueMax, (float)upperHsv[1], (float)upperHsv[2]);
            }

            //GS_LOG_TRACE_MSG(trace, "leftMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(leftMostLowerHsv) + ", " + LoggingTools::FormatVec3f(leftMostUpperHsv) + ".");
            //GS_LOG_TRACE_MSG(trace, "righttMost Lower/Upper HSV{ " + LoggingTools::FormatVec3f(rightMostLowerHsv) + ", " + LoggingTools::FormatVec3f(rightMostUpperHsv) + ".");

            cv::Mat firstColorMaskImage;
            cv::inRange(hsvImage, leftMostLowerHsv, leftMostUpperHsv, firstColorMaskImage);

            cv::Mat secondColorMaskImage;
            cv::inRange(hsvImage, rightMostLowerHsv, rightMostUpperHsv, secondColorMaskImage);

            //LoggingTools::DebugShowImage(image_name_ + "  firstColorMaskImage", firstColorMaskImage);
            //LoggingTools::DebugShowImage(image_name_ + "  secondColorMaskImage", secondColorMaskImage);

            cv::bitwise_or(firstColorMaskImage, secondColorMaskImage, color_mask_image_);
        }

        //LoggingTools::DebugShowImage("BallImagProc::GetColorMaskImage returning color_mask_image_", color_mask_image_);

        return color_mask_image_;
    }


    cv::Mat ColorFilter::GetColorMaskImage(const cv::Mat& hsvImage, const GolfBall& ball, double widening_amount) {

        GsColorTriplet lowerHsv = ball.GetBallLowerHSV(ball.ball_color_);
        GsColorTriplet upperHsv = ball.GetBallUpperHSV(ball.ball_color_);

        return ColorFilter::GetColorMaskImage(hsvImage, lowerHsv, upperHsv, widening_amount);

    }

}
