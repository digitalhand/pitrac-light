/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Ellipse detection implementations - extracted from ball_image_proc.cpp
// Phase 3.1 modular refactoring

#include "ellipse_detector.h"

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "utils/logging_tools.h"
#include "utils/cv_utils.h"
#include "EllipseDetectorYaed.h"

namespace golf_sim {

cv::RotatedRect EllipseDetector::FindBestEllipseFornaciari(cv::Mat& img,
                                                           const GsCircle& reference_ball_circle,
                                                           int mask_radius) {
    // Finding ellipses is expensive - use it only in the region of interest
    cv::Size sz = img.size();

    int circleX = CvUtils::CircleX(reference_ball_circle);
    int circleY = CvUtils::CircleY(reference_ball_circle);
    int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

    const double cannySubImageSizeMultiplier = 1.35;
    int expandedRadiusForCanny = (int)(cannySubImageSizeMultiplier * (double)ballRadius);
    cv::Rect ball_ROI_rect{ (int)(circleX - expandedRadiusForCanny), (int)(circleY - expandedRadiusForCanny),
                       (int)(2. * expandedRadiusForCanny), (int)(2. * expandedRadiusForCanny) };

    cv::Point offset_sub_to_full;
    cv::Point offset_full_to_sub;

    cv::Mat processedImg = CvUtils::GetSubImage(img, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);

    LoggingTools::DebugShowImage("EllipseDetector::FindBestEllipseFornaciari - Original (SUB) input image", processedImg);

    // Preprocessing: blur, erode, dilate to reduce noise
    cv::GaussianBlur(processedImg, processedImg, cv::Size(3, 3), 0);
    cv::erode(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
    cv::dilate(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);

    LoggingTools::DebugShowImage("EllipseDetector::FindBestEllipseFornaciari - blurred/eroded/dilated image", processedImg);

    // YAED Parameters (Sect. 4.2 from Fornaciari paper)
    int iThLength = 16;
    float fThObb = 3.0f;
    float fThPos = 1.0f;
    float fTaoCenters = 0.05f;
    int iNs = 16;
    float fMaxCenterDistance = sqrt(float(sz.width * sz.width + sz.height * sz.height)) * fTaoCenters;
    float fThScoreScore = 0.72f;

    // Gaussian filter parameters for pre-processing
    cv::Size szPreProcessingGaussKernelSize = cv::Size(5, 5);
    double dPreProcessingGaussSigma = 1.0;

    float fDistanceToEllipseContour = 0.1f;
    float fMinReliability = 0.4f;

    // Initialize YAED Detector with parameters
    CEllipseDetectorYaed detector;
    detector.SetParameters(szPreProcessingGaussKernelSize,
        dPreProcessingGaussSigma,
        fThPos,
        fMaxCenterDistance,
        iThLength,
        fThObb,
        fDistanceToEllipseContour,
        fThScoreScore,
        fMinReliability,
        iNs
    );

    // Detect ellipses
    std::vector<Ellipse> ellipses;
    cv::Mat workingImg = processedImg.clone();
    detector.Detect(workingImg, ellipses);

    GS_LOG_TRACE_MSG(trace, "Found " + std::to_string(ellipses.size()) + " candidate ellipses");

    // Find the best ellipse that seems reasonably sized
    cv::Mat ellipseImg = cv::Mat::zeros(img.size(), CV_8UC3);
    cv::RNG rng(12345);
    std::vector<cv::RotatedRect> minEllipse(ellipses.size());
    int numEllipses = 0;

    cv::RotatedRect largestEllipse;
    double largestArea = 0;

    cv::Scalar ellipseColor{ 255, 255, 255 };
    int numDrawn = 0;
    bool foundBestEllipse = false;

    // Look at ellipses to find the best (highest ranked) one that is reasonable
    for (auto& es : ellipses) {
        Ellipse ellipseStruct = es;
        cv::RotatedRect e(cv::Point(cvRound(es._xc), cvRound(es._yc)),
                         cv::Size(cvRound(2.0 * es._a), cvRound(2.0 * es._b)),
                         (float)(es._rad * 180.0 / CV_PI));

        cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

        // Translate the ellipse to full image coordinates
        e.center.x += offset_sub_to_full.x;
        e.center.y += offset_sub_to_full.y;

        float xc = e.center.x;
        float yc = e.center.y;
        float a = e.size.width;
        float b = e.size.height;
        float theta = e.angle;
        float area = a * b;
        float aspectRatio = std::max(a, b) / std::min(a, b);

        // Cull out unrealistic ellipses based on position and size
        if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
            (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
            area < pow(ballRadius, 2.0) ||
            area > 6 * pow(ballRadius, 2.0) ||
            (!CvUtils::IsUprightRect(theta) && false) ||
            aspectRatio > 1.15) {
            GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) + "," +
                std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) +
                " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) +
                " aspectRatio = " + std::to_string(aspectRatio) + " (REJECTED)");
            GS_LOG_TRACE_MSG(trace, "     Expected max found ball radius was = " +
                std::to_string(ballRadius / 1.5) + ", min area: " + std::to_string(pow(ballRadius, 2.0)) +
                ", max area: " + std::to_string(5 * pow(ballRadius, 2.0)) +
                ", aspectRatio: " + std::to_string(aspectRatio) + ". (REJECTED)");

            if (numDrawn++ > 5) {
                GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
            }
            else {
                cv::ellipse(ellipseImg, e, color, 2);
            }
            numEllipses++;
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) +
                " rw,rh = " + std::to_string(a) + "," + std::to_string(b) +
                " rectArea = " + std::to_string(a * b));

            if (numDrawn++ > 5) {
                GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
                break; // Too far down the quality list
            }
            else {
                cv::ellipse(ellipseImg, e, color, 2);
            }
            numEllipses++;

            if (area > largestArea) {
                // Save this ellipse as our current best candidate
                largestArea = area;
                largestEllipse = e;
                foundBestEllipse = true;
            }
        }
    }

    LoggingTools::DebugShowImage("EllipseDetector::FindBestEllipseFornaciari - Ellipses(" +
                                std::to_string(numEllipses) + "):", ellipseImg);

    if (!foundBestEllipse) {
        LoggingTools::Warning("EllipseDetector::FindBestEllipseFornaciari - Unable to find ellipse.");
        return largestEllipse;
    }

    return largestEllipse;
}

cv::RotatedRect EllipseDetector::FindLargestEllipse(cv::Mat& img,
                                                    const GsCircle& reference_ball_circle,
                                                    int mask_radius) {
    LoggingTools::DebugShowImage("EllipseDetector::FindLargestEllipse - input image", img);

    int lowThresh = 30;
    int highThresh = 70;

    const double kMinFinalizationCannyMean = 8.0;
    const double kMaxFinalizationCannyMean = 15.0;

    cv::Scalar meanArray;
    cv::Scalar stdDevArray;

    cv::Mat cannyOutput;

    bool edgeDetectDone = false;
    int cannyIterations = 0;

    int circleX = CvUtils::CircleX(reference_ball_circle);
    int circleY = CvUtils::CircleY(reference_ball_circle);
    int ballRadius = (int)std::round(CvUtils::CircleRadius(reference_ball_circle));

    // Canny is expensive - use it only in the region of interest
    const double cannySubImageSizeMultiplier = 1.35;
    int expandedRadiusForCanny = (int)(cannySubImageSizeMultiplier * (double)ballRadius);
    cv::Rect ball_ROI_rect{ (int)(circleX - expandedRadiusForCanny), (int)(circleY - expandedRadiusForCanny),
                       (int)(2. * expandedRadiusForCanny), (int)(2. * expandedRadiusForCanny) };

    cv::Point offset_sub_to_full;
    cv::Point offset_full_to_sub;

    cv::Mat finalChoiceSubImg = CvUtils::GetSubImage(img, ball_ROI_rect, offset_sub_to_full, offset_full_to_sub);
    bool edgeDetectionFailed = false;

    // Try to remove noise around the ball
    cv::erode(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);
    cv::dilate(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);

    LoggingTools::DebugShowImage("EllipseDetector::FindLargestEllipse - after erode/dilate", finalChoiceSubImg);

    // Iteratively adjust Canny thresholds to get optimal edge density
    while (!edgeDetectDone) {
        cv::Canny(finalChoiceSubImg, cannyOutput, lowThresh, highThresh);

        // Remove contour artifacts at mask edge and inner ball area
        cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub,
                  mask_radius, cv::Scalar{ 0, 0, 0 }, (int)((double)ballRadius / 12.0));
        cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub,
                  (int)(ballRadius * 0.7), cv::Scalar{ 0, 0, 0 }, cv::FILLED);

        cv::meanStdDev(cannyOutput, meanArray, stdDevArray);

        double mean = meanArray.val[0];
        double stddev = stdDevArray.val[0];

        GS_LOG_TRACE_MSG(trace, "Ball circle finalization - Canny edges at tolerance (low,high)= " +
            std::to_string(lowThresh) + ", " + std::to_string(highThresh) +
            "): mean: " + std::to_string(mean) + " std : " + std::to_string(stddev));

        // Adjust to get more/less edge lines depending on image busyness
        const int kCannyToleranceIncrement = 4;

        if (mean > kMaxFinalizationCannyMean) {
            lowThresh += kCannyToleranceIncrement;
            highThresh += kCannyToleranceIncrement;
        }
        else if (mean < kMinFinalizationCannyMean) {
            lowThresh -= kCannyToleranceIncrement;
            highThresh -= kCannyToleranceIncrement;
        }
        else {
            edgeDetectDone = true;
        }

        // Safety net to prevent infinite loop
        if (cannyIterations > 30) {
            edgeDetectDone = true;
            edgeDetectionFailed = true;
        }
    }

    if (edgeDetectionFailed) {
        LoggingTools::Warning("EllipseDetector::FindLargestEllipse - Failed to detect edges");
        cv::RotatedRect nullRect;
        return nullRect;
    }

    // Note: RemoveLinearNoise moved to HoughDetector
    // RemoveLinearNoise(cannyOutput);

    // Try to fill in any gaps in the ellipse edge lines
    for (int dilations = 0; dilations < 2; dilations++) {
        cv::dilate(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        cv::erode(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
    }
    LoggingTools::DebugShowImage("EllipseDetector::FindLargestEllipse - Dilated/eroded Canny", cannyOutput);

    // Find contours and fit ellipses
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(cannyOutput, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE, cv::Point(0, 0));

    cv::Mat contourImg = cv::Mat::zeros(img.size(), CV_8UC3);
    cv::Mat ellipseImg = cv::Mat::zeros(img.size(), CV_8UC3);
    cv::RNG rng(12345);
    std::vector<cv::RotatedRect> minEllipse(contours.size());
    int numEllipses = 0;

    cv::RotatedRect largestEllipse;
    double largestArea = 0;

    for (size_t i = 0; i < contours.size(); i++) {
        cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

        // Need at least 25 points to fit an ellipse
        if (contours[i].size() > 25) {
            minEllipse[i] = cv::fitEllipse(contours[i]);

            // Translate the ellipse to full image coordinates
            minEllipse[i].center.x += offset_sub_to_full.x;
            minEllipse[i].center.y += offset_sub_to_full.y;

            float xc = minEllipse[i].center.x;
            float yc = minEllipse[i].center.y;
            float a = minEllipse[i].size.width;
            float b = minEllipse[i].size.height;
            float theta = minEllipse[i].angle;
            float area = a * b;

            // Cull out unrealistic ellipses based on position and size
            if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
                (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
                area < pow(ballRadius, 2.0) ||
                area > 5 * pow(ballRadius, 2.0) ||
                (!CvUtils::IsUprightRect(theta) && false)) {
                GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) +
                    "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) +
                    " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) + " (REJECTED)");

                cv::ellipse(ellipseImg, minEllipse[i], color, 2);
                numEllipses++;
                cv::drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) +
                    " rw,rh = " + std::to_string(a) + "," + std::to_string(b) +
                    " rectArea = " + std::to_string(a * b));

                cv::ellipse(ellipseImg, minEllipse[i], color, 2);
                numEllipses++;
                cv::drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);

                if (area > largestArea) {
                    // Save this ellipse as our current best candidate
                    largestArea = area;
                    largestEllipse = minEllipse[i];
                }
            }
        }
    }

    LoggingTools::DebugShowImage("EllipseDetector::FindLargestEllipse - Contours", contourImg);
    LoggingTools::DebugShowImage("EllipseDetector::FindLargestEllipse - Ellipses(" +
                                std::to_string(numEllipses) + ")", ellipseImg);

    return largestEllipse;
}

} // namespace golf_sim
