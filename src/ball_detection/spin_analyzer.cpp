/*****************************************************************//**
 * \file   spin_analyzer.cpp
 * \brief  Golf ball spin (rotation) analysis using Gabor filters and 3D hemisphere projection.
 *         Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.
 *
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/
/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

#include <algorithm>
#include <vector>
#include <chrono>
#include <fstream>
#include <cmath>

#include <boost/timer/timer.hpp>
#include <boost/log/trivial.hpp>
#include <opencv2/photo.hpp>

#include "ball_detection/spin_analyzer.h"
#include "utils/logging_tools.h"
#include "utils/cv_utils.h"
#include "gs_config.h"
#include "gs_options.h"
#include "gs_ui_system.h"


namespace golf_sim {

    // Currently, equalizing the brightness of the input images appears to help the results
#define GS_USING_IMAGE_EQ

    // Sentinel value for "do not compare" pixels in spin analysis images
    const uchar kPixelIgnoreValue = 128;

    // Serialized operations for debug (normally false for parallel execution)
    static const bool kSerializeOpsForDebug = false;

    // --- Static member initialization ---

    int SpinAnalyzer::kCoarseXRotationDegreesIncrement = 6;
    int SpinAnalyzer::kCoarseXRotationDegreesStart = -42;
    int SpinAnalyzer::kCoarseXRotationDegreesEnd = 42;
    int SpinAnalyzer::kCoarseYRotationDegreesIncrement = 5;
    int SpinAnalyzer::kCoarseYRotationDegreesStart = -30;
    int SpinAnalyzer::kCoarseYRotationDegreesEnd = 30;
    int SpinAnalyzer::kCoarseZRotationDegreesIncrement = 6;
    int SpinAnalyzer::kCoarseZRotationDegreesStart = -50;
    int SpinAnalyzer::kCoarseZRotationDegreesEnd = 60;

    int SpinAnalyzer::kGaborMaxWhitePercent = 44;
    int SpinAnalyzer::kGaborMinWhitePercent = 38;

    bool SpinAnalyzer::kLogIntermediateSpinImagesToFile = false;

    // --- Configuration loading ---

    void SpinAnalyzer::LoadConfigurationValues() {
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesIncrement", kCoarseXRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesStart", kCoarseXRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseXRotationDegreesEnd", kCoarseXRotationDegreesEnd);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesIncrement", kCoarseYRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesStart", kCoarseYRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseYRotationDegreesEnd", kCoarseYRotationDegreesEnd);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesIncrement", kCoarseZRotationDegreesIncrement);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesStart", kCoarseZRotationDegreesStart);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kCoarseZRotationDegreesEnd", kCoarseZRotationDegreesEnd);

        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kGaborMinWhitePercent", kGaborMinWhitePercent);
        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kGaborMaxWhitePercent", kGaborMaxWhitePercent);

        GolfSimConfiguration::SetConstant("gs_config.logging.kLogIntermediateSpinImagesToFile", kLogIntermediateSpinImagesToFile);
    }

    // --- Histogram analysis ---

    void SpinAnalyzer::GetImageCharacteristics(const cv::Mat& img,
                                               const int brightness_percentage,
                                               int& brightness_cutoff,
                                               int& lowest_brightness,
                                               int& highest_brightness) {

        /// Establish the number of bins
        const int histSize = 256;

        /// Set the ranges ( for B,G,R) )
        float range[] = { 0, 256 };
        const float* histRange = { range };

        bool uniform = true; bool accumulate = false;

        cv::Mat b_hist;

        /// Compute the histograms:
        calcHist(&img, 1, 0, cv::Mat(), b_hist, 1, &histSize, &histRange, uniform, accumulate);

        // Draw the histograms for B, G and R
        int hist_w = 512; int hist_h = 400;
        int bin_w = cvRound((double)hist_w / histSize);

        long totalPoints = img.rows * img.cols;
        long accum = 0;
        int i = histSize - 1;
        bool foundPercentPoint = false;
        highest_brightness = -1;
        double targetPoints = (double)totalPoints * (100 - brightness_percentage) / 100.0;

        while (i >= 0 && !foundPercentPoint )
        {
            int numPixelsInBin = cvRound(b_hist.at<float>(i));
            accum += numPixelsInBin;
            foundPercentPoint = (accum >= targetPoints) ? true : false;
            if (highest_brightness < 0 && numPixelsInBin > 0) {
                highest_brightness = i;
            }
            i--;  // move to the next bin to the left
        }

        brightness_cutoff = i + 1;
    }

    // --- Reflection removal ---

    const int kReflectionMinimumRGBValue = 245;

    void SpinAnalyzer::RemoveReflections(const cv::Mat& original_image, cv::Mat& filtered_image, const cv::Mat& mask) {

        int hh = original_image.rows;
        int ww = original_image.cols;

        static int imgNumber = 1;
        imgNumber++;

        // Define the idea of a "bright" reflection dynamically
        const int brightness_percentage = 99;
        int brightness_cutoff;
        int lowestBrightess;
        int highest_brightness;
        GetImageCharacteristics(original_image, brightness_percentage, brightness_cutoff, lowestBrightess, highest_brightness);

        GS_LOG_TRACE_MSG(trace, "Lower cutoff for brightness is " + std::to_string(brightness_percentage) + "%, grayscale value = " + std::to_string(brightness_cutoff));

        brightness_cutoff--;  // Make sure we don't filter out EVERYTHING
        GsColorTriplet lower = ((uchar)kReflectionMinimumRGBValue, (uchar)kReflectionMinimumRGBValue, (uchar)kReflectionMinimumRGBValue);
        GsColorTriplet upper{ 255,255,255 };

        cv::Mat thresh(original_image.rows, original_image.cols, original_image.type(), cv::Scalar(0));
        cv::inRange(original_image, lower, upper, thresh);

        // Expand the bright reflection areas
        static const int kReflectionKernelDilationSize = 5;
        const int kCloseKernelSize = 3;

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kCloseKernelSize, kCloseKernelSize));
        cv::Mat morph;
        cv::morphologyEx(thresh, morph, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), /*iterations = */ 1);

        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(kReflectionKernelDilationSize, kReflectionKernelDilationSize));
        cv::morphologyEx(morph, morph, cv::MORPH_DILATE, kernel, cv::Point(-1, -1),  /*iterations = */ 1);

        // Set corresponding pixels to "ignore" in the filtered_image
        for (int x = 0; x < original_image.cols; x++) {
            for (int y = 0; y < original_image.rows; y++) {
                uchar p1 = morph.at<uchar>(x, y);

                if (p1 == 255) {
                    filtered_image.at<uchar>(x, y) = kPixelIgnoreValue;
                }
             }
        }

        LoggingTools::DebugShowImage("RemoveReflections - final filtered image = ", filtered_image);
    }

    // DEPRECATED - No longer used
    cv::Mat SpinAnalyzer::ReduceReflections(const cv::Mat& img, const cv::Mat& mask) {

        int hh = img.rows;
        int ww = img.cols;

        LoggingTools::DebugShowImage("ReduceReflections - input img = ", img);
        LoggingTools::DebugShowImage("ReduceReflections - mask = ", mask);

        GsColorTriplet lower{ kReflectionMinimumRGBValue,kReflectionMinimumRGBValue,kReflectionMinimumRGBValue };
        GsColorTriplet upper{ 255,255,255 };

        cv::Mat thresh(img.rows, img.cols, img.type(), cv::Scalar(0));
        cv::inRange(img, lower, upper, thresh);

        LoggingTools::DebugShowImage("ReduceReflections - thresholded image = ", thresh);

        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7));
        cv::Mat morph;
        cv::morphologyEx(thresh, morph, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), /*iterations = */ 1);

        kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(8, 8));
        cv::morphologyEx(morph, morph, cv::MORPH_DILATE, kernel, cv::Point(-1, -1),  /*iterations = */ 1);

        cv::bitwise_and(morph, mask, morph);

        LoggingTools::DebugShowImage("ReduceReflections - morphology = ", morph);

        cv::Mat result1;
        int inPaintRadius = (int)(std::min(ww, hh) / 30);
        cv::inpaint(img, morph, result1, inPaintRadius, cv::INPAINT_TELEA);
        LoggingTools::DebugShowImage("ReduceReflections - result1 (INPAINT_TELEA) (radius=" + std::to_string(inPaintRadius) + ") = ", result1);

        return result1;
    }

    // --- Ball isolation and masking ---

    cv::Mat SpinAnalyzer::IsolateBall(const cv::Mat& img, GolfBall& ball) {

        // We will grab a rectangle a little larger than the actual ball size
        const float ballSurroundMult = 1.05f;

        int r1 = (int)std::round(ball.measured_radius_pixels_ * ballSurroundMult);
        int rInc = (long)(r1 - ball.measured_radius_pixels_);

        int x1 = ball.x() - r1;
        int y1 = ball.y() - r1;
        int x_width = 2 * r1;
        int y_height = 2 * r1;

        // Ensure the isolated image is entirely in the larger image
        x1 = max(0, x1);
        y1 = max(0, y1);

        if (x1 + x_width >= img.cols) {
            x1 = img.cols - x_width - 1;
        }
        if (y1 + y_height >= img.rows) {
            y1 = img.rows - y_height - 1;
        }

        cv::Rect ballRect{ x1, y1, x_width, y_height };

        // Re-center the ball's x and y position in the new, smaller picture
        ball.set_x( (float)std::round(rInc + ball.measured_radius_pixels_));
        ball.set_y( (float)std::round(rInc + ball.measured_radius_pixels_));

        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;
        cv::Mat ball_image = CvUtils::GetSubImage(img, ballRect, offset_sub_to_full, offset_full_to_sub);

        const float referenceBallMaskReductionFactor = 0.995f;

#ifdef GS_USING_IMAGE_EQ
        cv::equalizeHist(ball_image, ball_image);
#endif

        cv::Mat finalResult = MaskAreaOutsideBall(ball_image, ball, referenceBallMaskReductionFactor, cv::Scalar(0, 0, 0));

        return finalResult;
    }

    cv::Mat SpinAnalyzer::MaskAreaOutsideBall(cv::Mat& ball_image, const GolfBall& ball, float mask_reduction_factor, const cv::Scalar& maskValue) {

        int mask_radius = (int)(ball.measured_radius_pixels_ * mask_reduction_factor);

        cv::Mat maskImage = cv::Mat::zeros(ball_image.rows, ball_image.cols, ball_image.type());
        cv::circle(maskImage, cv::Point(ball.x(), ball.y()), mask_radius, cv::Scalar(255, 255, 255), -1);

        cv::Mat result = ball_image.clone();
        cv::bitwise_and(ball_image, maskImage, result);

        // XOR the image-on-black with a rectangle of desired color and a black circle
        cv::Rect r(cv::Point(0, 0), cv::Point(ball_image.cols, ball_image.rows));
        cv::rectangle(maskImage, r, maskValue, cv::FILLED);
        cv::circle(maskImage, cv::Point(ball.x(), ball.y()), mask_radius, cv::Scalar(0, 0, 0), -1);

        cv::bitwise_xor(result, maskImage, result);

        return result;
    }

    // --- Gabor filter ---

    cv::Mat SpinAnalyzer::CreateGaborKernel(int ks, double sig, double th, double lm, double gm, double ps) {

        int hks = (ks - 1) / 2;
        double theta = th * CV_PI / 180;
        double psi = ps * CV_PI / 180;
        double del = 2.0 / (ks - 1);
        double lmbd = lm / 100.0;
        double Lambda = lm;
        double sigma = sig / ks;
        cv::Mat kernel(ks, ks, CV_32F);
        double gamma = gm;

        kernel = cv::getGaborKernel(cv::Size(ks, ks), sig, theta, Lambda, gamma, psi, CV_32F);
        return kernel;
    }

    cv::Mat SpinAnalyzer::ApplyGaborFilterToBall(const cv::Mat& image_gray, const GolfBall& ball, float & calibrated_binary_threshold, float prior_binary_threshold) {
        CV_Assert( (image_gray.type() == CV_8UC1) );

        cv::Mat img_f32;
        image_gray.convertTo(img_f32, CV_32F, 1.0 / 255, 0);

#ifdef GS_USING_IMAGE_EQ
        const int kernel_size = 21;
        int pos_sigma = 2;
        int pos_lambda = 6;
        int pos_gamma = 4;
        int pos_th = 60;
        int pos_psi = 9;
        float binary_threshold = 11.;
#else
        const int kernel_size = 21;
        int pos_sigma = 2;
        int pos_lambda = 6;
        int pos_gamma = 4;
        int pos_th = 60;
        int pos_psi = 27;
        float binary_threshold = 8.5;
#endif
        // Override the starting binary threshold if we have a prior one
        if (prior_binary_threshold > 0) {
            binary_threshold = prior_binary_threshold;
        }

        double sig = pos_sigma / 2.0;
        double lm = (double)pos_lambda;
        double th = (double)pos_th * 2;
        double ps = (double)pos_psi * 10.0;
        double gm = (double)pos_gamma / 20.0;

        int white_percent = 0;

        cv::Mat dimpleImg = ApplyTestGaborFilter(img_f32, kernel_size, sig, lm, th, ps, gm, binary_threshold,
            white_percent);

        GS_LOG_TRACE_MSG(trace, "Initial Gabor filter white percent = " + std::to_string(white_percent));

        bool ratheting_threshold_down = (white_percent < kGaborMinWhitePercent);

        // Give it a second go if we're too white or too black and haven't already overridden the binary threshold
        if (prior_binary_threshold < 0 &&
            (white_percent < kGaborMinWhitePercent || white_percent >= kGaborMaxWhitePercent)) {

            while (white_percent < kGaborMinWhitePercent || white_percent >= kGaborMaxWhitePercent) {

                if (ratheting_threshold_down)
                {
                    if (kGaborMinWhitePercent - white_percent > 5) {
                        binary_threshold = binary_threshold - 1.0F;
                    }
                    else {
                        binary_threshold = binary_threshold - 0.5F;
                    }
                    GS_LOG_TRACE_MSG(trace, "Trying lower gabor binary_threshold setting of " + std::to_string(binary_threshold) + " for better balance.");
                }
                else {
                    if (white_percent - kGaborMaxWhitePercent > 5) {
                        binary_threshold = binary_threshold + 1.0F;
                    }
                    else {
                        binary_threshold = binary_threshold + 0.5F;
                    }
                    GS_LOG_TRACE_MSG(trace, "Trying higher gabor binary_threshold setting of " + std::to_string(binary_threshold) + " for better balance.");
                }

                dimpleImg = ApplyTestGaborFilter(img_f32, kernel_size, sig, lm, th, ps, gm, binary_threshold,
                    white_percent);
                GS_LOG_TRACE_MSG(trace, "Next, refined, Gabor white percent = " + std::to_string(white_percent));

                if (binary_threshold > 30 || binary_threshold < 2) {
                    GS_LOG_MSG(warning, "Binaary threshold for Gabor filter reached limit of " + std::to_string(binary_threshold));
                    break;
                }

            }

            calibrated_binary_threshold = binary_threshold;

            GS_LOG_TRACE_MSG(trace, "Final Gabor white percent = " + std::to_string(white_percent));
        }

        return dimpleImg;
    }

    cv::Mat SpinAnalyzer::ApplyTestGaborFilter(const cv::Mat& img_f32,
        const int kernel_size, double sig, double lm, double th, double ps, double gm, float binary_threshold,
        int &white_percent  ) {

        cv::Mat dest = cv::Mat::zeros(img_f32.rows, img_f32.cols, img_f32.type());
        cv::Mat accum = cv::Mat::zeros(img_f32.rows, img_f32.cols, img_f32.type());
        cv::Mat kernel;

        const double thetaIncrement = 11.25;
        for (double theta = 0; theta <= 360.0; theta += thetaIncrement) {
            kernel = CreateGaborKernel(kernel_size, sig, theta, lm, gm, ps);
            cv::filter2D(img_f32, dest, CV_32F, kernel);

            cv::max(accum, dest, accum);
        }

        cv::Mat accumGray;
        accum.convertTo(accumGray, CV_8U, 255, 0);

        cv::Mat dimpleEdges = cv::Mat::zeros(accum.rows, accum.cols, accum.type());

        const int edgeThresholdLow = (int)std::round(binary_threshold * 10.);
        const int edgeThresholdHigh = 255;
        cv::threshold(accumGray, dimpleEdges, edgeThresholdLow, edgeThresholdHigh, cv::THRESH_BINARY);

        white_percent = (int)std::round(((double)cv::countNonZero(dimpleEdges) * 100.) / ((double)dimpleEdges.rows * dimpleEdges.cols));

        return dimpleEdges;
    }

    // --- 3D projection structs and functions ---

    // This structure is used as a callback for the OpenCV forEach() call.
    struct projectionOp {
        static void setup(const GolfBall *currentBall,
                          cv::Mat& projectedImg,
                          const double& x_rotation_degreesAngleRad,
                          const double& y_rotation_degreesAngleRad,
                          const double& z_rotation_degreesAngleRad ) {
            currentBall_ = currentBall;
            projectedImg_ = projectedImg;
            projectedImg_.rows = projectedImg.rows;
            projectedImg_.cols = projectedImg.cols;
            x_rotation_degreesAngleRad_ = x_rotation_degreesAngleRad;
            y_rotation_degreesAngleRad_ = y_rotation_degreesAngleRad;
            z_rotation_degreesAngleRad_ = z_rotation_degreesAngleRad;

            sinX_ = sin(x_rotation_degreesAngleRad_);
            cosX_ = cos(x_rotation_degreesAngleRad_);
            sinY_ = sin(y_rotation_degreesAngleRad_);
            cosY_ = cos(y_rotation_degreesAngleRad_);
            sinZ_ = sin(z_rotation_degreesAngleRad_);
            cosZ_ = cos(z_rotation_degreesAngleRad_);

            rotatingOnX_ = (std::abs(x_rotation_degreesAngleRad_) > 0.001) ? true : false;
            rotatingOnY_ = (std::abs(y_rotation_degreesAngleRad_) > 0.001) ? true : false;
            rotatingOnZ_ = (std::abs(z_rotation_degreesAngleRad_) > 0.001) ? true : false;
        }

        static void getBallZ(const double imageX, const double imageY, double& imageXFromCenter, double& imageYFromCenter, double& ball3dZ) {
            double r = currentBall_->measured_radius_pixels_;
            double ballCenterX = currentBall_->x();
            double ballCenterY = currentBall_->y();

            imageXFromCenter = imageX - ballCenterX;
            imageYFromCenter = imageY - ballCenterY;

            if (std::abs(imageXFromCenter) > r || std::abs(imageYFromCenter) > r) {
                ball3dZ = 0;
                return;
            }
            double rSquared = pow(r, 2);
            double xSquarePlusYSquare = pow(imageXFromCenter, 2) + pow(imageYFromCenter, 2);
            double diff = rSquared - xSquarePlusYSquare;
            if (diff < 0.0) {
                ball3dZ = 0;
            }
            else
            {
                ball3dZ = sqrt(diff);
            }
        }

        void operator ()(uchar& pixelValue, const int* position) const {
            double imageX = position[0];
            double imageY = position[1];

            double imageXFromCenter;
            double imageYFromCenter;
            double ball3dZOfUnrotatedPoint = 0.0;
            getBallZ(imageX, imageY, imageXFromCenter, imageYFromCenter, ball3dZOfUnrotatedPoint);

            bool prerotatedPointNotValid = (ball3dZOfUnrotatedPoint <= 0.0001);

            if (prerotatedPointNotValid) {
                projectedImg_.at<cv::Vec2i>((int)imageX, (int)imageY)[0] = (int)ball3dZOfUnrotatedPoint;
                projectedImg_.at<cv::Vec2i>((int)imageX, (int)imageY)[1] = kPixelIgnoreValue;
            }

            double imageZ = ball3dZOfUnrotatedPoint;

            // X-axis rotation
            if (rotatingOnX_) {
                double tmpImageYFromCenter = imageYFromCenter;
                imageYFromCenter = (imageYFromCenter * cosX_) - (imageZ * sinX_);
                imageZ = (int)((tmpImageYFromCenter * sinX_) + (imageZ * cosX_));
            }

            // Y-axis rotation
            if (rotatingOnY_) {
                double tmpImageXFromCenter = imageXFromCenter;
                imageXFromCenter = (imageXFromCenter * cosY_) + (imageZ * sinY_);
                imageZ = (int)((imageZ * cosY_) - (tmpImageXFromCenter * sinY_));
            }

            // Z-axis rotation
            if (rotatingOnZ_) {
                double tmpImageXFromCenter = imageXFromCenter;
                imageXFromCenter = (imageXFromCenter * cosZ_) - (imageYFromCenter * sinZ_);
                imageYFromCenter = (tmpImageXFromCenter * sinZ_) + (imageYFromCenter * cosZ_);
            }

            // Shift back to coordinates with the origin in the top-left
            imageX = imageXFromCenter + projectionOp::currentBall_->x();
            imageY = imageYFromCenter + projectionOp::currentBall_->y();

            double ball3dZOfRotatedPoint = 0;
            double dummy_rotatedImageXFromCenter;
            double dummy_rotatedImageYFromCenter;

            getBallZ(imageX, imageY, dummy_rotatedImageXFromCenter, dummy_rotatedImageYFromCenter, ball3dZOfRotatedPoint);

            if (currentBall_->PointIsInsideBall(imageX, imageY) && ball3dZOfRotatedPoint < 0.001) {
                GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall Z-value pixel within ball at (" + std::to_string(imageX) +
                    ", " + std::to_string(imageY) + ").");
            }

            if (imageX >= 0 &&
                imageY >= 0 &&
                imageX < projectedImg_.cols &&
                imageY < projectedImg_.rows &&
                ball3dZOfRotatedPoint > 0.0) {

                    int roundedImageX = (int)(imageX + 0.5);
                    int roundedImageY = (int)(imageY + 0.5);

                    projectedImg_.at<cv::Vec2i>(roundedImageX, roundedImageY)[0] = (int)(ball3dZOfRotatedPoint);
                    projectedImg_.at<cv::Vec2i>(roundedImageX, roundedImageY)[1] = (prerotatedPointNotValid ? kPixelIgnoreValue : pixelValue);
            }
        }

        static const GolfBall* currentBall_;
        static cv::Mat projectedImg_;
        static double x_rotation_degreesAngleRad_;
        static double y_rotation_degreesAngleRad_;
        static double z_rotation_degreesAngleRad_;
        static double sinX_;
        static double cosX_;
        static double sinY_;
        static double cosY_;
        static double sinZ_;
        static double cosZ_;
        static bool rotatingOnX_;
        static bool rotatingOnY_;
        static bool rotatingOnZ_;
    };

    // Static storage for projectionOp struct
    const GolfBall* projectionOp::currentBall_ = NULL;
    cv::Mat projectionOp::projectedImg_;
    double projectionOp::x_rotation_degreesAngleRad_ = 0;
    double projectionOp::y_rotation_degreesAngleRad_ = 0;
    double projectionOp::z_rotation_degreesAngleRad_ = 0;
    double projectionOp::sinX_ = 0;
    double projectionOp::cosX_ = 0;
    double projectionOp::sinY_ = 0;
    double projectionOp::cosY_ = 0;
    double projectionOp::sinZ_ = 0;
    double projectionOp::cosZ_ = 0;
    bool projectionOp::rotatingOnX_ = true;
    bool projectionOp::rotatingOnY_ = true;
    bool projectionOp::rotatingOnZ_ = true;

    cv::Mat SpinAnalyzer::Project2dImageTo3dBall(const cv::Mat& image_gray, const GolfBall& ball, const cv::Vec3i& rotation_angles_degrees) {

        int sizes[2] = { image_gray.rows, image_gray.cols };
        cv::Mat projectedImg = cv::Mat(2, sizes, CV_32SC2, cv::Scalar(0, kPixelIgnoreValue));
        projectedImg.rows = image_gray.rows;
        projectedImg.cols = image_gray.cols;

        projectionOp::setup(&ball,
                            projectedImg,
                            -(float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[0]),
                            (float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[1]),
                            (float)CvUtils::DegreesToRadians((double)rotation_angles_degrees[2])  );

        if (kSerializeOpsForDebug) {
            for (int x = 0; x < image_gray.cols; x++) {
                for (int y = 0; y < image_gray.rows; y++) {
                    int position[]{ x, y };
                    uchar pixel = image_gray.at<uchar>(x, y);

                    if (ball.PointIsInsideBall(x, y) && pixel == kPixelIgnoreValue) {
                        GS_LOG_TRACE_MSG(trace, "Project2dImageTo3dBall found ignore pixel within ball at (" + std::to_string(x) + ", " + std::to_string(y) + ").");
                    }

                    projectionOp()(pixel, position);
                }
            }
        }
        else {
            image_gray.forEach<uchar>(projectionOp());
        }

        return projectedImg;
    }

    void SpinAnalyzer::Unproject3dBallTo2dImage(const cv::Mat& src3D, cv::Mat& destination_image_gray, const GolfBall& ball) {

        for (int x = 0; x < destination_image_gray.cols; x++) {
            for (int y = 0; y < destination_image_gray.rows; y++) {
                int position[]{ x, y };
                int maxValueZ = src3D.at<cv::Vec2i>(x, y)[0];
                int pixelValue = src3D.at<cv::Vec2i>(x, y)[1];

                int original_pixel_value = (int)destination_image_gray.at<uchar>(x, y);
                destination_image_gray.at<uchar>(x, y) = pixelValue;
            }
        }
    }

    // --- 3D rotation wrapper ---

    void SpinAnalyzer::GetRotatedImage(const cv::Mat& gray_2D_input_image, const GolfBall& ball, const cv::Vec3i rotation, cv::Mat& outputGrayImg) {
       BOOST_LOG_FUNCTION();

       cv::Mat ball3DImage = Project2dImageTo3dBall(gray_2D_input_image, ball, rotation);

       outputGrayImg = cv::Mat::zeros(gray_2D_input_image.rows, gray_2D_input_image.cols, gray_2D_input_image.type());
       Unproject3dBallTo2dImage(ball3DImage, outputGrayImg, ball);
   }

    // --- Image comparison callback ---

    struct ImgComparisonOp {
        static void setup(const cv::Mat* target_image,
                          const cv::Mat* candidate_elements_mat,
                          std::vector<RotationCandidate>* candidates,
                          std::vector<std::string>* comparisonData ) {
            ImgComparisonOp::comparisonData_ = comparisonData;
            ImgComparisonOp::target_image_ = target_image;
            ImgComparisonOp::candidate_elements_mat_ = candidate_elements_mat;
            ImgComparisonOp::candidates_ = candidates;
        }

        void operator ()(ushort& unusedValue, const int* position) const {
            int x = position[0];
            int y = position[1];
            int z = position[2];

            int elementIndex = (*candidate_elements_mat_).at<ushort>(x, y, z);
            RotationCandidate& c = (*candidates_)[elementIndex];

            cv::Vec2i results = SpinAnalyzer::CompareRotationImage(*target_image_, c.img, c.index);
            double scaledScore = (double)results[0] / (double)results[1];

            c.pixels_matching = results[0];
            c.pixels_examined = results[1];
            c.score = scaledScore;

            // CSV (Excel) File format
            std::string s = std::to_string(c.index) + "\t" + std::to_string(c.x_rotation_degrees) + "\t" + std::to_string(c.y_rotation_degrees) + "\t" + std::to_string(c.z_rotation_degrees) + "\t" + std::to_string(results[0]) + "\t" + std::to_string(results[1]) +
                "\t" + std::to_string(scaledScore) + "\n";

            (*comparisonData_)[c.index] = s;
        }

        static const cv::Mat* target_image_;
        static const cv::Mat* candidate_elements_mat_;
        static std::vector<std::string>* comparisonData_;
        static std::vector<RotationCandidate>* candidates_;
    };

    // Static storage for ImgComparisonOp struct
    std::vector<std::string>* ImgComparisonOp::comparisonData_ = nullptr;
    const cv::Mat* ImgComparisonOp::target_image_ = nullptr;
    const cv::Mat* ImgComparisonOp::candidate_elements_mat_ = nullptr;
    std::vector<RotationCandidate>* ImgComparisonOp::candidates_ = nullptr;

    // --- Candidate generation and comparison ---

    int SpinAnalyzer::CompareCandidateAngleImages(const cv::Mat* target_image,
                                                    const cv::Mat* candidate_elements_mat,
                                                    const cv::Vec3i* candidate_elements_mat_size,
                                                    std::vector<RotationCandidate>* candidates,
                                                    std::vector<std::string>& comparison_csv_data) {

        boost::timer::cpu_timer timer1;

        int xSize = (*candidate_elements_mat_size)[0];
        int ySize = (*candidate_elements_mat_size)[1];
        int zSize = (*candidate_elements_mat_size)[2];

        int numCandidates = xSize * ySize * zSize;
        std::vector<std::string> comparisonData(numCandidates);

        ImgComparisonOp::setup(target_image, candidate_elements_mat, candidates, &comparisonData);

        if (kSerializeOpsForDebug) {
            for (int x = 0; x < xSize; x++) {
                for (int y = 0; y < ySize; y++) {
                    for (int z = 0; z < zSize; z++) {
                        ushort unusedValue = 0;
                        int position[]{ x, y, z };
                        ImgComparisonOp()(unusedValue, position);
                    }
                }
            }
        }
        else {
            (*candidate_elements_mat).forEach<ushort>(ImgComparisonOp());
        }

        // Find the best candidate from the comparison results
        double maxScaledScore = -1.0;
        double maxPixelsExamined = -1.0;
        double maxPixelsMatching = -1.0;
        int maxPixelsExaminedIndex = -1;
        int maxPixelsMatchingIndex = -1;
        int maxScaledScoreIndex = -1;
        int bestScaledScoreRotX = 0;
        int bestScaledScoreRotY = 0;
        int bestScaledScoreRotZ = 0;
        int bestPixelsMatchingRotX = 0;
        int bestPixelsMatchingRotY = 0;
        int bestPixelsMatchingRotZ = 0;

        double kSpinLowCountPenaltyPower = 2.0;
        double kSpinLowCountPenaltyScalingFactor = 1000.0;
        double kSpinLowCountDifferenceWeightingFactor = 500.0;

        double low_count_penalty = 0.0;
        double final_scaled_score = 0.0;

        for (auto& element : *candidates)
        {
            RotationCandidate c = element;

            if (c.pixels_examined > maxPixelsExamined) {
                maxPixelsExamined = c.pixels_examined;
                maxPixelsExaminedIndex = c.index;
            }

            if (c.pixels_matching > maxPixelsMatching) {
                maxPixelsMatching = c.pixels_matching;
                maxPixelsMatchingIndex = c.index;
                bestPixelsMatchingRotX = c.x_rotation_degrees;
                bestPixelsMatchingRotY = c.y_rotation_degrees;
                bestPixelsMatchingRotZ = c.z_rotation_degrees;
            }
        }

        for (auto& element : *candidates)
        {
            RotationCandidate c = element;

            low_count_penalty = std::pow((maxPixelsExamined - (double)c.pixels_examined) / kSpinLowCountDifferenceWeightingFactor,
                                kSpinLowCountPenaltyPower) / kSpinLowCountPenaltyScalingFactor;
            final_scaled_score = (c.score * 10.) - low_count_penalty;

            if (final_scaled_score > maxScaledScore) {
                maxScaledScore = final_scaled_score;
                maxScaledScoreIndex = c.index;
                bestScaledScoreRotX = c.x_rotation_degrees;
                bestScaledScoreRotY = c.y_rotation_degrees;
                bestScaledScoreRotZ = c.z_rotation_degrees;
            }
        }

        std::string s = "Best Candidate based on number of matching pixels was #" + std::to_string(maxPixelsMatchingIndex) +
                            " - Rot: (" + std::to_string(bestPixelsMatchingRotX) + ", " +
                            std::to_string(bestPixelsMatchingRotY) + ", " + std::to_string(bestPixelsMatchingRotZ) + ") ";

        s = "Best Candidate based on its scaled score of (" + std::to_string(maxScaledScore) + ") was # " + std::to_string(maxScaledScoreIndex) +
                            " - Rot: (" + std::to_string(bestScaledScoreRotX) + ", " +
                            std::to_string(bestScaledScoreRotY) + ", " + std::to_string(bestScaledScoreRotZ) + ") ";
        GS_LOG_MSG(debug, s);

        comparison_csv_data = comparisonData;

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << "CompareCandidateAngleImages: ";
        std::cout << std::fixed << std::setprecision(8)
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        return maxScaledScoreIndex;
    }

    cv::Vec2i SpinAnalyzer::CompareRotationImage(const cv::Mat& img1, const cv::Mat& img2, const int index) {

        CV_Assert((img1.rows == img2.rows && img1.rows == img2.cols));

        cv::Mat testCorrespondenceImg = cv::Mat::zeros(img1.rows, img1.cols, img1.type());

        long score = 0;
        long totalPixelsExamined = 0;
        for (int x = 0; x < img1.cols; x++) {
            for (int y = 0; y < img1.rows; y++) {
                uchar p1 = img1.at<uchar>(x, y);
                uchar p2 = img2.at<cv::Vec2i>(x, y)[1];

                if (p1 != kPixelIgnoreValue && p2 != kPixelIgnoreValue) {
                    totalPixelsExamined++;

                    if (p1 == p2) {
                        score++;
                        testCorrespondenceImg.at<uchar>(x, y) = 255;
                    }
                }
                else
                {
                    testCorrespondenceImg.at<uchar>(x, y) = kPixelIgnoreValue;
                }
            }
        }

        cv::Vec2i result(score, totalPixelsExamined);
        return result;
    }

    // --- Candidate image generation ---

    bool SpinAnalyzer::ComputeCandidateAngleImages(const cv::Mat& base_dimple_image,
                                                    const RotationSearchSpace& search_space,
                                                    cv::Mat &outputCandidateElementsMat,
                                                    cv::Vec3i &output_candidate_elements_mat_size,
                                                    std::vector< RotationCandidate> &output_candidates,
                                                    const GolfBall& ball) {
        boost::timer::cpu_timer timer1;

        int anglex_rotation_degrees_increment = search_space.anglex_rotation_degrees_increment;
        int anglex_rotation_degrees_start = search_space.anglex_rotation_degrees_start;
        int anglex_rotation_degrees_end = search_space.anglex_rotation_degrees_end;
        int angley_rotation_degrees_increment = search_space.angley_rotation_degrees_increment;
        int angley_rotation_degrees_start = search_space.angley_rotation_degrees_start;
        int angley_rotation_degrees_end = search_space.angley_rotation_degrees_end;
        int anglez_rotation_degrees_increment = search_space.anglez_rotation_degrees_increment;
        int anglez_rotation_degrees_start = search_space.anglez_rotation_degrees_start;
        int anglez_rotation_degrees_end = search_space.anglez_rotation_degrees_end;

        int xAngleOffset = 0;
        int yAngleOffset = 0;

        int xSize = (int)std::ceil((anglex_rotation_degrees_end - anglex_rotation_degrees_start) / anglex_rotation_degrees_increment) + 1;
        int ySize = (int)std::ceil((angley_rotation_degrees_end - angley_rotation_degrees_start) / angley_rotation_degrees_increment) + 1;
        int zSize = (int)std::ceil((anglez_rotation_degrees_end - anglez_rotation_degrees_start) / anglez_rotation_degrees_increment) + 1;

        output_candidate_elements_mat_size = cv::Vec3i(xSize, ySize, zSize);

        GS_LOG_TRACE_MSG(trace, "ComputeCandidateAngleImages will compute " + std::to_string(xSize * ySize * zSize) + " images.");

        int sizes[3] = { xSize, ySize, zSize };
        outputCandidateElementsMat = cv::Mat(3, sizes, CV_16U, cv::Scalar(0));

        short vectorIndex = 0;

        int xIndex = 0;
        int yIndex = 0;
        int zIndex = 0;

        for (int x_rotation_degrees = anglex_rotation_degrees_start, xIndex = 0; x_rotation_degrees <= anglex_rotation_degrees_end; x_rotation_degrees += anglex_rotation_degrees_increment, xIndex++) {
            for (int y_rotation_degrees = angley_rotation_degrees_start, yIndex = 0; y_rotation_degrees <= angley_rotation_degrees_end; y_rotation_degrees += angley_rotation_degrees_increment, yIndex++) {
                for (int z_rotation_degrees = anglez_rotation_degrees_start, zIndex = 0; z_rotation_degrees <= anglez_rotation_degrees_end; z_rotation_degrees += anglez_rotation_degrees_increment, zIndex++) {

                    cv::Mat ball2DImage;
                    cv::Mat ball13DImage = Project2dImageTo3dBall(base_dimple_image, ball, cv::Vec3i(x_rotation_degrees, y_rotation_degrees, z_rotation_degrees));

                    RotationCandidate c;
                    c.index = vectorIndex;
                    c.img = ball13DImage;
                    c.x_rotation_degrees = x_rotation_degrees - xAngleOffset;
                    c.y_rotation_degrees = y_rotation_degrees - yAngleOffset;
                    c.z_rotation_degrees = z_rotation_degrees;
                    c.score = 0.0;

                    output_candidates.push_back(c);
                    outputCandidateElementsMat.at<ushort>(xIndex, yIndex, zIndex) = vectorIndex;

                    vectorIndex++;
                }
            }
        }

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << "ComputeCandidateAngleImages Time: " << std::fixed << std::setprecision(8)
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        return true;
    }

    // --- Main spin analysis entry point ---

    cv::Vec3d SpinAnalyzer::GetBallRotation(const cv::Mat& full_gray_image1,
                                             const GolfBall& ball1,
                                             const cv::Mat& full_gray_image2,
                                             const GolfBall& ball2) {
        BOOST_LOG_FUNCTION();
        auto spin_detection_start = std::chrono::high_resolution_clock::now();

        GS_LOG_TRACE_MSG(trace, "GetBallRotation called with ball1 = " + ball1.Format() + ",\nball2 = " + ball2.Format());
        LoggingTools::DebugShowImage("full_gray_image1", full_gray_image1);
        LoggingTools::DebugShowImage("full_gray_image2", full_gray_image2);

        GolfBall local_ball1 = ball1;
        GolfBall local_ball2 = ball2;

        cv::Mat ball_image1 = IsolateBall(full_gray_image1, local_ball1);
        cv::Mat ball_image2 = IsolateBall(full_gray_image2, local_ball2);

        LoggingTools::DebugShowImage("ISOLATED full_gray_image1", ball_image1);
        LoggingTools::DebugShowImage("ISOLATED full_gray_image2", ball_image2);

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts && kLogIntermediateSpinImagesToFile) {
            LoggingTools::LogImage("", ball_image1, std::vector < cv::Point >{}, true, "log_view_ISOLATED_full_gray_image1.png");
            LoggingTools::LogImage("", ball_image2, std::vector < cv::Point >{}, true, "log_view_ISOLATED_full_gray_image2.png");
        }

        double ball1RadiusMultiplier = 1.0;
        double ball2RadiusMultiplier = 1.0;

        if (ball_image1.rows > ball_image2.rows || ball_image1.cols > ball_image2.cols) {
            ball2RadiusMultiplier = (double)ball_image1.rows / (double)ball_image2.rows;
            int upWidth = ball_image1.cols;
            int upHeight = ball_image1.rows;
            cv::resize(ball_image2, ball_image2, cv::Size(upWidth, upHeight), cv::INTER_LINEAR);
        }
        else if (ball_image2.rows > ball_image1.rows || ball_image2.cols > ball_image1.cols) {
            ball1RadiusMultiplier = (double)ball_image2.rows / (double)ball_image1.rows;
            int upWidth = ball_image2.cols;
            int upHeight = ball_image2.rows;
            cv::resize(ball_image1, ball_image1, cv::Size(upWidth, upHeight), cv::INTER_LINEAR);
        }

        cv::Mat originalBallImg1 = ball_image1.clone();
        cv::Mat originalBallImg2 = ball_image2.clone();

        local_ball1.measured_radius_pixels_ = local_ball1.measured_radius_pixels_ * ball1RadiusMultiplier;
        local_ball1.ball_circle_[2] = local_ball1.ball_circle_[2] * (float)ball1RadiusMultiplier;
        local_ball1.set_x( (float)((double)local_ball1.x() * ball1RadiusMultiplier));
        local_ball1.set_y( (float)((double)local_ball1.y() * ball1RadiusMultiplier));
        local_ball2.measured_radius_pixels_ = local_ball2.measured_radius_pixels_ * ball2RadiusMultiplier;
        local_ball2.ball_circle_[2] = local_ball2.ball_circle_[2] * (float)ball2RadiusMultiplier;
        local_ball2.set_x( (float)((double)local_ball2.x() * ball2RadiusMultiplier));
        local_ball2.set_y( (float)((double)local_ball2.y() * ball2RadiusMultiplier));

        std::vector < cv::Point > center1 = { cv::Point{(int)local_ball1.x(), (int)local_ball1.y()} };
        LoggingTools::DebugShowImage("Ball1 Image", ball_image1, center1);
        GS_LOG_TRACE_MSG(trace, "Updated (local) ball1 data: " + local_ball1.Format());
        std::vector < cv::Point > center2 = { cv::Point{(int)local_ball2.x(), (int)local_ball2.y()} };
        LoggingTools::DebugShowImage("Ball2 Image", ball_image2, center2);
        GS_LOG_TRACE_MSG(trace, "Updated (local) ball2 data: " + local_ball2.Format());

        float calibrated_binary_threshold = 0;
        cv::Mat ball_image1DimpleEdges = ApplyGaborFilterToBall(ball_image1, local_ball1, calibrated_binary_threshold);
        cv::Mat ball_image2DimpleEdges = ApplyGaborFilterToBall(ball_image2, local_ball2, calibrated_binary_threshold, calibrated_binary_threshold);

        cv::Mat area_mask_image_;
        RemoveReflections(ball_image1, ball_image1DimpleEdges, area_mask_image_);
        RemoveReflections(ball_image2, ball_image2DimpleEdges, area_mask_image_);

        const float finalBallMaskReductionFactor = 0.92f;
        cv::Scalar ignoreColor = cv::Scalar(kPixelIgnoreValue, kPixelIgnoreValue, kPixelIgnoreValue);
        ball_image1DimpleEdges = MaskAreaOutsideBall(ball_image1DimpleEdges, local_ball1, finalBallMaskReductionFactor, ignoreColor);
        ball_image2DimpleEdges = MaskAreaOutsideBall(ball_image2DimpleEdges, local_ball2, finalBallMaskReductionFactor, ignoreColor);
        LoggingTools::DebugShowImage("Final ball_image1DimpleEdges after masking outside", ball_image1DimpleEdges);
        LoggingTools::DebugShowImage("Final ball_image2DimpleEdges after masking outside", ball_image2DimpleEdges);

        cv::Vec3d ball2Distances;

        cv::Vec3f angleOffset1 = cv::Vec3f((float)ball1.angles_camera_ortho_perspective_[0], (float)ball1.angles_camera_ortho_perspective_[1], 0);
        cv::Vec3f angleOffset2 = cv::Vec3f((float)ball2.angles_camera_ortho_perspective_[0], (float)ball2.angles_camera_ortho_perspective_[1], 0);

        cv::Vec3f angleOffsetDeltas1Float = (angleOffset2 - angleOffset1) / 2.0;

        if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kLeftHanded) {
            angleOffsetDeltas1Float[1] = -angleOffsetDeltas1Float[1];
        }
        cv::Vec3i angleOffsetDeltas1 = CvUtils::Round(angleOffsetDeltas1Float);

        cv::Mat unrotatedBallImg1DimpleEdges = ball_image1DimpleEdges.clone();
        GetRotatedImage(unrotatedBallImg1DimpleEdges, local_ball1, angleOffsetDeltas1, ball_image1DimpleEdges);

        GS_LOG_TRACE_MSG(trace, "Adjusting rotation for camera view of ball 1 to offset (x,y,z)=" + std::to_string(angleOffsetDeltas1[0]) + "," + std::to_string(angleOffsetDeltas1[1]) + "," + std::to_string(angleOffsetDeltas1[2]));
        LoggingTools::DebugShowImage("Final perspective-de-rotated filtered ball_image1DimpleEdges: ", ball_image1DimpleEdges, center1);

        cv::Vec3i angleOffsetDeltas2 = CvUtils::Round(  -(( angleOffset2 - angleOffset1) - angleOffsetDeltas1Float) );
        if (GolfSimOptions::GetCommandLineOptions().golfer_orientation_ == GolferOrientation::kLeftHanded) {
            angleOffsetDeltas2[1] = (int)std::round( - ((angleOffset1[1] - angleOffset2[1]) - angleOffsetDeltas1Float[1]) );
        }

        cv::Mat unrotatedBallImg2DimpleEdges = ball_image2DimpleEdges.clone();
        GetRotatedImage(unrotatedBallImg2DimpleEdges, local_ball2, angleOffsetDeltas2, ball_image2DimpleEdges);
        GS_LOG_TRACE_MSG(trace, "Adjusting rotation for camera view of ball 2 to offset (x,y,z)=" + std::to_string(angleOffsetDeltas2[0]) + "," + std::to_string(angleOffsetDeltas2[1]) + "," + std::to_string(angleOffsetDeltas2[2]));
        LoggingTools::DebugShowImage("Final perspective-de-rotated filtered ball_image2DimpleEdges: ", ball_image2DimpleEdges, center1);

        cv::Mat normalizedOriginalBallImg1 = originalBallImg1.clone();
        GetRotatedImage(originalBallImg1, local_ball1, angleOffsetDeltas1, normalizedOriginalBallImg1);
        LoggingTools::DebugShowImage("Final rotated originalBall1: ", normalizedOriginalBallImg1, center1);
        cv::Mat normalizedOriginalBallImg2 = originalBallImg2.clone();
        GetRotatedImage(originalBallImg2, local_ball2, angleOffsetDeltas2, normalizedOriginalBallImg2);
        LoggingTools::DebugShowImage("Final rotated originalBall2: ", normalizedOriginalBallImg2, center2);

#ifdef __unix__
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultSpinBall1Image, normalizedOriginalBallImg1);
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultSpinBall2Image, normalizedOriginalBallImg2);
#endif

        RotationSearchSpace initialSearchSpace;

        initialSearchSpace.anglex_rotation_degrees_increment = kCoarseXRotationDegreesIncrement;
        initialSearchSpace.anglex_rotation_degrees_start = kCoarseXRotationDegreesStart;
        initialSearchSpace.anglex_rotation_degrees_end = kCoarseXRotationDegreesEnd;
        initialSearchSpace.angley_rotation_degrees_increment = kCoarseYRotationDegreesIncrement;
        initialSearchSpace.angley_rotation_degrees_start = kCoarseYRotationDegreesStart;
        initialSearchSpace.angley_rotation_degrees_end = kCoarseYRotationDegreesEnd;
        initialSearchSpace.anglez_rotation_degrees_increment = kCoarseZRotationDegreesIncrement;
        initialSearchSpace.anglez_rotation_degrees_start = kCoarseZRotationDegreesStart;
        initialSearchSpace.anglez_rotation_degrees_end = kCoarseZRotationDegreesEnd;

        cv::Mat outputCandidateElementsMat;
        std::vector< RotationCandidate> candidates;
        cv::Vec3i output_candidate_elements_mat_size;

        ComputeCandidateAngleImages(ball_image1DimpleEdges, initialSearchSpace, outputCandidateElementsMat, output_candidate_elements_mat_size, candidates, local_ball1);

        std::vector<std::string> comparison_csv_data;
        int best_candidate_index = CompareCandidateAngleImages(&ball_image2DimpleEdges, &outputCandidateElementsMat, &output_candidate_elements_mat_size, &candidates, comparison_csv_data);

        cv::Vec3f rotationResult;

        if (best_candidate_index < 0) {
            LoggingTools::Warning("No best candidate found.");
            return rotationResult;
        }

        bool write_spin_analysis_CSV_files = false;

        GolfSimConfiguration::SetConstant("gs_config.spin_analysis.kWriteSpinAnalysisCsvFiles", write_spin_analysis_CSV_files);

        if (write_spin_analysis_CSV_files) {
            std::string csv_fname_coarse = "spin_analysis_coarse.csv";
            ofstream csv_file_coarse(csv_fname_coarse);
            GS_LOG_TRACE_MSG(trace, "Writing CSV spin data to: " + csv_fname_coarse);
            for (auto& element : comparison_csv_data)
            {
                csv_file_coarse << element;
            }
            csv_file_coarse.close();
        }

        RotationCandidate c = candidates[best_candidate_index];

        std::string s = "Best Coarse Initial Rotation Candidate was #" + std::to_string(best_candidate_index) + " - Rot: (" + std::to_string(c.x_rotation_degrees) + ", " + std::to_string(c.y_rotation_degrees) + ", " + std::to_string(c.z_rotation_degrees) + ") ";
        GS_LOG_MSG(debug, s);

        RotationSearchSpace finalSearchSpace;

        int anglex_window_width = (int)std::round(ceil(initialSearchSpace.anglex_rotation_degrees_increment / 2.));
        int angley_window_width = (int)std::round(ceil(initialSearchSpace.angley_rotation_degrees_increment / 2.));
        int anglez_window_width = (int)std::round(ceil(initialSearchSpace.anglez_rotation_degrees_increment / 2.));

        finalSearchSpace.anglex_rotation_degrees_increment = 1;
        finalSearchSpace.anglex_rotation_degrees_start = c.x_rotation_degrees - anglex_window_width;
        finalSearchSpace.anglex_rotation_degrees_end = c.x_rotation_degrees + anglex_window_width;
        finalSearchSpace.angley_rotation_degrees_increment = (int) std::round(kCoarseYRotationDegreesIncrement / 2.);
        finalSearchSpace.angley_rotation_degrees_start = c.y_rotation_degrees - angley_window_width;
        finalSearchSpace.angley_rotation_degrees_end = c.y_rotation_degrees + angley_window_width;
        finalSearchSpace.anglez_rotation_degrees_increment = 1;
        finalSearchSpace.anglez_rotation_degrees_start = c.z_rotation_degrees - anglez_window_width;
        finalSearchSpace.anglez_rotation_degrees_end = c.z_rotation_degrees + anglez_window_width;

        cv::Mat finalOutputCandidateElementsMat;
        cv::Vec3i finalOutputCandidateElementsMatSize;
        std::vector< RotationCandidate> finalCandidates;

        ComputeCandidateAngleImages(ball_image1DimpleEdges, finalSearchSpace, finalOutputCandidateElementsMat, finalOutputCandidateElementsMatSize, finalCandidates, local_ball1);

        best_candidate_index = CompareCandidateAngleImages(&ball_image2DimpleEdges, &finalOutputCandidateElementsMat, &finalOutputCandidateElementsMatSize, &finalCandidates, comparison_csv_data);

        if (write_spin_analysis_CSV_files) {
            std::string csv_fname_fine = "spin_analysis_fine.csv";
            ofstream csv_file_fine(csv_fname_fine);
            GS_LOG_TRACE_MSG(trace, "Writing CSV spin data to: " + csv_fname_fine);
            for (auto& element : comparison_csv_data)
            {
                csv_file_fine << element;
            }
            csv_file_fine.close();
        }

        int best_rot_x = 0;
        int best_rot_y = 0;
        int best_rot_z = 0;

        if (best_candidate_index >= 0) {
            RotationCandidate finalC = finalCandidates[best_candidate_index];
            best_rot_x = finalC.x_rotation_degrees;
            best_rot_y = finalC.y_rotation_degrees;
            best_rot_z = finalC.z_rotation_degrees;

            std::string s = "Best Raw Fine (and final) Rotation Candidate was #" + std::to_string(best_candidate_index) + " - Rot: (" + std::to_string(best_rot_x) + ", " + std::to_string(best_rot_y) + ", " + std::to_string(best_rot_z) + ") ";
            GS_LOG_MSG(debug, s);

            cv::Mat bestImg3D = finalCandidates[best_candidate_index].img;
            cv::Mat bestImg2D = cv::Mat::zeros(ball_image1DimpleEdges.rows, ball_image1DimpleEdges.cols, ball_image1DimpleEdges.type());
            Unproject3dBallTo2dImage(bestImg3D, bestImg2D, ball2);
            LoggingTools::DebugShowImage("Best Final Rotation Candidate Image", bestImg2D);
        }
        else {
            LoggingTools::Warning("No best final candidate found.  Returning 0,0,0 spin results.");
            rotationResult = cv::Vec3d(0, 0, 0);
        }

        cv::Vec3f spin_offset_angle;
        spin_offset_angle[0] = angleOffset1[0] + angleOffsetDeltas1Float[0];
        spin_offset_angle[1] = angleOffset1[1] - angleOffsetDeltas1Float[1];

        GS_LOG_TRACE_MSG(trace, "Now normalizing for spin_offset_angle = (" + std::to_string(spin_offset_angle[0]) + ", " +
                                    std::to_string(spin_offset_angle[1]) + ", " + std::to_string(spin_offset_angle[2]) + ").");

        double spin_offset_angle_radians_X = CvUtils::DegreesToRadians(spin_offset_angle[0]);
        double spin_offset_angle_radians_Y = CvUtils::DegreesToRadians(spin_offset_angle[1]);
        double spin_offset_angle_radians_Z = CvUtils::DegreesToRadians(spin_offset_angle[2]);

        int normalized_rot_x = (int)round( (double)best_rot_x * cos(spin_offset_angle_radians_Y) + (double)best_rot_z * sin(spin_offset_angle_radians_Y) );
        int normalized_rot_y = (int)round( (double)best_rot_y * cos(spin_offset_angle_radians_X) - (double)best_rot_z * sin(spin_offset_angle_radians_X) );

        int normalized_rot_z = (int)round((double)best_rot_z * cos(spin_offset_angle_radians_X) * cos(spin_offset_angle_radians_Y));
        normalized_rot_z -= (int)round((double)best_rot_y * sin(spin_offset_angle_radians_X));
        normalized_rot_z -= (int)round((double)best_rot_x * sin(spin_offset_angle_radians_Y));

        rotationResult = cv::Vec3d(normalized_rot_x, normalized_rot_y, normalized_rot_z);

        GS_LOG_TRACE_MSG(trace, "Normalized spin angles (X,Y,Z) = (" + std::to_string(normalized_rot_x) + ", " + std::to_string(normalized_rot_y) + ", " + std::to_string(normalized_rot_z) + ").");

        cv::Mat resultBball2DImage;

        GetRotatedImage(ball_image1DimpleEdges, local_ball1, cv::Vec3i(best_rot_x, best_rot_y, best_rot_z), resultBball2DImage);

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts && kLogIntermediateSpinImagesToFile) {
            LoggingTools::LogImage("", resultBball2DImage, std::vector < cv::Point >{}, true, "Filtered Ball1_Rotated_By_Best_Angles.png");
        }

        cv::Mat test_ball1_image = normalizedOriginalBallImg1.clone();
        GetRotatedImage(normalizedOriginalBallImg1, local_ball1, cv::Vec3i(best_rot_x, best_rot_y, best_rot_z), test_ball1_image);

        cv::Scalar color{ 0, 0, 0 };
        const GsCircle& circle = local_ball1.ball_circle_;
        cv::circle(test_ball1_image, cv::Point((int)local_ball1.x(), (int)local_ball1.y()), (int)circle[2], color, 2 /*thickness*/);
        LoggingTools::DebugShowImage("Final rotated-by-best-angle originalBall1: ", test_ball1_image, center1);

#ifdef __unix__
        GsUISystem::SaveWebserverImage(GsUISystem::kWebServerResultBallRotatedByBestAngles, test_ball1_image);
#endif

        // Golf convention: X (side) spin positive = surface going right to left
        rotationResult[0] = -rotationResult[0];

        auto spin_detection_end = std::chrono::high_resolution_clock::now();
        auto spin_duration = std::chrono::duration_cast<std::chrono::milliseconds>(spin_detection_end - spin_detection_start);
        GS_LOG_MSG(info, "Spin detection completed in " + std::to_string(spin_duration.count()) + "ms");

        return rotationResult;
    }

}
