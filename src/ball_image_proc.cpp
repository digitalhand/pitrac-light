/*****************************************************************//**
 * \file   ball_image_proc.cpp
 * \brief  Handles most of the image processing related to ball-identification 
 * 
 * \author PiTrac
 * \date   February 2025
 *********************************************************************/
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */


#include <ranges>
#include <algorithm>
#include <vector>
#include <chrono>
#include <fstream>
#include "gs_format_lib.h"

#include <boost/timer/timer.hpp>
#include <boost/math/special_functions/erf.hpp>
#include <boost/circular_buffer.hpp>
#include <opencv2/photo.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/core/cvdef.h>

#include "ball_image_proc.h"
#include "utils/logging_tools.h"
#include "utils/cv_utils.h"
#include "gs_config.h"
#include "gs_options.h"
#include "gs_ui_system.h"
#include "EllipseDetectorCommon.h"
#include "EllipseDetectorYaed.h"

// Edge detection
#include "ED.h"
#include "EDPF.h"
#include "EDColor.h"

namespace golf_sim {

    // Currently, equalizing the brightness of the input images appears to help the results
#define GS_USING_IMAGE_EQ
#define DONT__PERFORM_FINAL_TARGETTED_BALL_ID  // Remove DONT to perform a final, targetted refinement of the ball circle identification
#define DONT__USE_ELLIPSES_FOR_FINAL_ID    

    const int MIN_BALL_CANDIDATE_RADIUS = 10;

    // Balls with an average color that is too far from the searched-for color will not be considered
    // good candidates.The tolerance is based on a Euclidian distance. See differenceRGB in CvUtils module.
    // The tolerance is relative to the closest - in - RGB - value candidate.So if the "best" candidate ball is,
    // for example, 100 away from the expected color, than any balls with a RGB difference of greater than
    // 100 + CANDIDATE_BALL_COLOR_TOLERANCE will be excluded.
    const int CANDIDATE_BALL_COLOR_TOLERANCE = 50;

    const bool PREBLUR_IMAGE = false;
    const bool IS_COLOR_MASKING = false;   // Probably not effective on IR pictures

    // May be necessary in brighter environments - TBD
    const bool FINAL_BLUR = true;

    const int MAX_FINAL_CANDIDATE_BALLS_TO_SHOW = 4;


    // See places of use for explanation of these constants
    // kColorMaskWideningAmount moved to ColorFilter
    static const double kEllipseColorMaskWideningAmount = 35;
    static const bool kSerializeOpsForDebug = false;

    // Spin analysis constants moved to SpinAnalyzer

    double BallImageProc::kPlacedBallCannyLower;
    double BallImageProc::kPlacedBallCannyUpper;
    double BallImageProc::kPlacedBallStartingParam2 = 40;
    double BallImageProc::kPlacedBallMinParam2 = 30;
    double BallImageProc::kPlacedBallMaxParam2 = 60;
    double BallImageProc::kPlacedBallCurrentParam1 = 120.0;
    double BallImageProc::kPlacedBallParam2Increment = 4;

    int BallImageProc::kPlacedMinHoughReturnCircles = 1;
    int BallImageProc::kPlacedMaxHoughReturnCircles = 4;
    double BallImageProc::kStrobedBallsCannyLower = 50;
    double BallImageProc::kStrobedBallsCannyUpper = 110;


    int BallImageProc::kStrobedBallsMaxHoughReturnCircles = 12;
    int BallImageProc::kStrobedBallsMinHoughReturnCircles = 1;

    int BallImageProc::kStrobedBallsPreCannyBlurSize = 5;
    int BallImageProc::kStrobedBallsPreHoughBlurSize = 13;
    double BallImageProc::kStrobedBallsStartingParam2 = 40;
    double BallImageProc::kStrobedBallsMinParam2 = 30;
    double BallImageProc::kStrobedBallsMaxParam2 = 60;
    double BallImageProc::kStrobedBallsCurrentParam1 = 120.0;
    double BallImageProc::kStrobedBallsHoughDpParam1 = 1.5;
    double BallImageProc::kStrobedBallsParam2Increment = 4;

    bool  BallImageProc::kStrobedBallsUseAltHoughAlgorithm = true;
    double BallImageProc::kStrobedBallsAltCannyLower = 35;
    double BallImageProc::kStrobedBallsAltCannyUpper = 70;
    int BallImageProc::kStrobedBallsAltPreCannyBlurSize = 11;
    int BallImageProc::kStrobedBallsAltPreHoughBlurSize = 16;
    double BallImageProc::kStrobedBallsAltStartingParam2 = 0.95;
    double BallImageProc::kStrobedBallsAltMinParam2 = 0.6;
    double BallImageProc::kStrobedBallsAltMaxParam2 = 1.0;
    double BallImageProc::kStrobedBallsAltCurrentParam1 = 130.0;
    double BallImageProc::kStrobedBallsAltHoughDpParam1 = 1.5;
    double BallImageProc::kStrobedBallsAltParam2Increment = 0.05;

    bool BallImageProc::kUseCLAHEProcessing;
    int BallImageProc::kCLAHEClipLimit;
    int BallImageProc::kCLAHETilesGridSize;

    double BallImageProc::kPuttingBallStartingParam2 = 40;
    double BallImageProc::kPuttingBallMinParam2 = 30;
    double BallImageProc::kPuttingBallMaxParam2 = 60;
    double BallImageProc::kPuttingBallCurrentParam1 = 120.0;
    double BallImageProc::kPuttingBallParam2Increment = 4;
    int BallImageProc::kPuttingMaxHoughReturnCircles = 12;
    int BallImageProc::kPuttingMinHoughReturnCircles = 1;
    double BallImageProc::kPuttingHoughDpParam1 = 1.5;

    double BallImageProc::kExternallyStrobedEnvCannyLower = 35;
    double BallImageProc::kExternallyStrobedEnvCannyUpper = 80;

    double BallImageProc::kExternallyStrobedEnvCurrentParam1 = 130.0;
    double BallImageProc::kExternallyStrobedEnvMinParam2 = 28;
    double BallImageProc::kExternallyStrobedEnvMaxParam2 = 100;
    double BallImageProc::kExternallyStrobedEnvStartingParam2 = 65;
    double BallImageProc::kExternallyStrobedEnvNarrowingParam2 = 0.6;
    double BallImageProc::kExternallyStrobedEnvNarrowingDpParam = 1.1;
    double BallImageProc::kExternallyStrobedEnvParam2Increment = 4;
    int BallImageProc::kExternallyStrobedEnvMinHoughReturnCircles = 3;
    int BallImageProc::kExternallyStrobedEnvMaxHoughReturnCircles = 20;
    int BallImageProc::kExternallyStrobedEnvPreHoughBlurSize = 11;
    int BallImageProc::kExternallyStrobedEnvPreCannyBlurSize = 3;
    double BallImageProc::kExternallyStrobedEnvHoughDpParam1 = 1.0;
    int BallImageProc::kExternallyStrobedEnvNarrowingPreCannyBlurSize = 3;
    int BallImageProc::kExternallyStrobedEnvNarrowingPreHoughBlurSize = 9;
    int BallImageProc::kExternallyStrobedEnvMinimumSearchRadius = 60;
    int BallImageProc::kExternallyStrobedEnvMaximumSearchRadius = 80;

    bool BallImageProc::kUseDynamicRadiiAdjustment = true;
    int BallImageProc::kNumberRadiiToAverageForDynamicAdjustment = 3;
    double BallImageProc::kStrobedNarrowingRadiiMinRatio = 0.8;
    double BallImageProc::kStrobedNarrowingRadiiMaxRatio = 1.2;
    double BallImageProc::kStrobedNarrowingRadiiDpParam = 1.8;
    double BallImageProc::kStrobedNarrowingRadiiParam2 = 100.0;


    double BallImageProc::kPlacedNarrowingRadiiMinRatio = 0.9;
    double BallImageProc::kPlacedNarrowingRadiiMaxRatio = 1.1;
    double BallImageProc::kPlacedNarrowingStartingParam2 = 80.0;
    double BallImageProc::kPlacedNarrowingRadiiDpParam = 2.0;
    double BallImageProc::kPlacedNarrowingParam1 = 130.0;

    int BallImageProc::kPlacedPreCannyBlurSize = 5;
    int BallImageProc::kPlacedPreHoughBlurSize = 11;
    int BallImageProc::kPuttingPreHoughBlurSize = 9;


    // kLogIntermediateSpinImagesToFile moved to SpinAnalyzer
    double BallImageProc::kPlacedBallHoughDpParam1 = 1.5;

    bool BallImageProc::kUseBestCircleRefinement = false;
    bool BallImageProc::kUseBestCircleLargestCircle = false;

    double BallImageProc::kBestCircleCannyLower = 55;
    double BallImageProc::kBestCircleCannyUpper = 110;
    int BallImageProc::kBestCirclePreCannyBlurSize = 5;
    int BallImageProc::kBestCirclePreHoughBlurSize = 13;
    double BallImageProc::kBestCircleParam1 = 120.;
    double BallImageProc::kBestCircleParam2 = 35.;
    double BallImageProc::kBestCircleHoughDpParam1 = 1.5;

    double BallImageProc::kExternallyStrobedBestCircleCannyLower = 55;
    double BallImageProc::kExternallyStrobedBestCircleCannyUpper = 110;
    int BallImageProc::kExternallyStrobedBestCirclePreCannyBlurSize = 5;
    int BallImageProc::kExternallyStrobedBestCirclePreHoughBlurSize = 13;
    double BallImageProc::kExternallyStrobedBestCircleParam1 = 120.;
    double BallImageProc::kExternallyStrobedBestCircleParam2 = 35.;
    double BallImageProc::kExternallyStrobedBestCircleHoughDpParam1 = 1.5;

    bool BallImageProc::kExternallyStrobedUseCLAHEProcessing = true;
    int BallImageProc::kExternallyStrobedCLAHEClipLimit = 6;
    int BallImageProc::kExternallyStrobedCLAHETilesGridSize = 6;

    double BallImageProc::kBestCircleIdentificationMinRadiusRatio = 0.85;
    double BallImageProc::kBestCircleIdentificationMaxRadiusRatio = 1.10;

    // Gabor constants moved to SpinAnalyzer

    // ONNX Detection Configuration
    // TODO: Fix defaults or remove these entirely
    std::string BallImageProc::kDetectionMethod = "legacy";
    std::string BallImageProc::kBallPlacementDetectionMethod = "legacy";
    // Default ONNX model path - relative to PITRAC_ROOT, resolved at runtime
    std::string BallImageProc::kONNXModelPath = "assets/models/best.onnx";
    float BallImageProc::kONNXConfidenceThreshold = 0.5f;
    float BallImageProc::kONNXNMSThreshold = 0.4f;
    int BallImageProc::kONNXInputSize = 640;
    int BallImageProc::kSAHISliceHeight = 320;
    int BallImageProc::kSAHISliceWidth = 320;
    float BallImageProc::kSAHIOverlapRatio = 0.2f;
    std::string BallImageProc::kONNXDeviceType = "CPU";

    // Dual-Backend Configuration
    std::string BallImageProc::kONNXBackend = "onnxruntime";  // Default to ONNX Runtime
    bool BallImageProc::kONNXRuntimeAutoFallback = true;     // Enable automatic fallback
    int BallImageProc::kONNXRuntimeThreads = 4;              // ARM64 optimized default

    // ONNX Runtime detector instance - replaces all static ONNX members
    std::unique_ptr<ONNXRuntimeDetector> BallImageProc::onnx_detector_;
    std::atomic<bool> BallImageProc::onnx_detector_initialized_{false};
    std::mutex BallImageProc::onnx_detector_mutex_;

    cv::dnn::Net BallImageProc::yolo_model_;
    bool BallImageProc::yolo_model_loaded_ = false;
    std::mutex BallImageProc::yolo_model_mutex_;

    // Pre-allocated buffers - static members
    cv::Mat BallImageProc::yolo_input_buffer_;
    cv::Mat BallImageProc::yolo_letterbox_buffer_;
    cv::Mat BallImageProc::yolo_resized_buffer_;
    cv::Mat BallImageProc::yolo_blob_buffer_;
    std::vector<cv::Rect> BallImageProc::yolo_detection_boxes_;
    std::vector<float> BallImageProc::yolo_detection_confidences_;
    std::vector<cv::Mat> BallImageProc::yolo_outputs_;

    BallImageProc* BallImageProc::get_ball_image_processor() {
        static BallImageProc* ip = nullptr;

        if (ip == nullptr) {
            ip = new BallImageProc;
        }

        return ip;
    }


    BallImageProc::BallImageProc() {
        min_ball_radius_ = -1;
        max_ball_radius_ = -1;

        // Spin analysis configuration is now loaded via SpinAnalyzer::LoadConfigurationValues()
        SpinAnalyzer::LoadConfigurationValues();

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCannyLower", kPlacedBallCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCannyUpper", kPlacedBallCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallStartingParam2", kPlacedBallStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallMinParam2", kPlacedBallMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallMaxParam2", kPlacedBallMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallCurrentParam1", kPlacedBallCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallParam2Increment", kPlacedBallParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedMinHoughReturnCircles", kPlacedMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedMaxHoughReturnCircles", kPlacedMaxHoughReturnCircles);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCannyLower", kStrobedBallsCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCannyUpper", kStrobedBallsCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreCannyBlurSize", kStrobedBallsPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreHoughBlurSize", kStrobedBallsPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsStartingParam2", kStrobedBallsStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMinParam2", kStrobedBallsMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMaxParam2", kStrobedBallsMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsCurrentParam1", kStrobedBallsCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsParam2Increment", kStrobedBallsParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMinHoughReturnCircles", kStrobedBallsMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsMaxHoughReturnCircles", kStrobedBallsMaxHoughReturnCircles);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsUseAltHoughAlgorithm", kStrobedBallsUseAltHoughAlgorithm);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCannyLower", kStrobedBallsAltCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCannyUpper", kStrobedBallsAltCannyUpper);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltPreCannyBlurSize", kStrobedBallsAltPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltPreHoughBlurSize", kStrobedBallsAltPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltStartingParam2", kStrobedBallsAltStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltMinParam2", kStrobedBallsAltMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltMaxParam2", kStrobedBallsAltMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltCurrentParam1", kStrobedBallsAltCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltHoughDpParam1", kStrobedBallsAltHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsAltParam2Increment", kStrobedBallsAltParam2Increment);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseCLAHEProcessing", kUseCLAHEProcessing);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kCLAHEClipLimit", kCLAHEClipLimit);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kCLAHETilesGridSize", kCLAHETilesGridSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallStartingParam2", kPuttingBallStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallMinParam2", kPuttingBallMinParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallMaxParam2", kPuttingBallMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallCurrentParam1", kPuttingBallCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingBallParam2Increment", kPuttingBallParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingMinHoughReturnCircles", kPuttingMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingMaxHoughReturnCircles", kPuttingMaxHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingHoughDpParam1", kPuttingHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvCurrentParam1", kExternallyStrobedEnvCurrentParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaxParam2", kExternallyStrobedEnvMaxParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvStartingParam2", kExternallyStrobedEnvStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingParam2", kExternallyStrobedEnvNarrowingParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingDpParam", kExternallyStrobedEnvNarrowingDpParam);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingPreCannyBlurSize", kExternallyStrobedEnvNarrowingPreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvNarrowingPreHoughBlurSize", kExternallyStrobedEnvNarrowingPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvParam2Increment", kExternallyStrobedEnvParam2Increment);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMinHoughReturnCircles", kExternallyStrobedEnvMinHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaxHoughReturnCircles", kExternallyStrobedEnvMaxHoughReturnCircles);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreHoughBlurSize", kExternallyStrobedEnvPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvPreCannyBlurSize", kExternallyStrobedEnvPreCannyBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleCannyLower", kExternallyStrobedBestCircleCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleCannyUpper", kExternallyStrobedBestCircleCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCirclePreCannyBlurSize", kExternallyStrobedBestCirclePreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCirclePreHoughBlurSize", kExternallyStrobedBestCirclePreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleParam1", kExternallyStrobedBestCircleParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleParam2", kExternallyStrobedBestCircleParam2);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedBestCircleHoughDpParam1", kExternallyStrobedBestCircleHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedUseCLAHEProcessing", kExternallyStrobedUseCLAHEProcessing);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedCLAHEClipLimit", kExternallyStrobedCLAHEClipLimit);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedCLAHETilesGridSize", kExternallyStrobedCLAHETilesGridSize);

        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvHoughDpParam1", kExternallyStrobedEnvHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMaximumSearchRadius", kExternallyStrobedEnvMaximumSearchRadius);
        GolfSimConfiguration::SetConstant("gs_config.testing.kExternallyStrobedEnvMinimumSearchRadius", kExternallyStrobedEnvMinimumSearchRadius);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedPreHoughBlurSize", kPlacedPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedPreCannyBlurSize", kPlacedPreCannyBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsPreHoughBlurSize", kStrobedBallsPreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPuttingPreHoughBlurSize", kPuttingPreHoughBlurSize);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedBallHoughDpParam1", kPlacedBallHoughDpParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedBallsHoughDpParam1", kStrobedBallsHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseBestCircleRefinement", kUseBestCircleRefinement);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseBestCircleLargestCircle", kUseBestCircleLargestCircle);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleCannyLower", kBestCircleCannyLower);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleCannyUpper", kBestCircleCannyUpper);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCirclePreCannyBlurSize", kBestCirclePreCannyBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCirclePreHoughBlurSize", kBestCirclePreHoughBlurSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleParam1", kBestCircleParam1);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleParam2", kBestCircleParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleHoughDpParam1", kBestCircleHoughDpParam1);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleIdentificationMinRadiusRatio", kBestCircleIdentificationMinRadiusRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBestCircleIdentificationMaxRadiusRatio", kBestCircleIdentificationMaxRadiusRatio);


        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kUseDynamicRadiiAdjustment", kUseDynamicRadiiAdjustment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kNumberRadiiToAverageForDynamicAdjustment", kNumberRadiiToAverageForDynamicAdjustment);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiMinRatio", kStrobedNarrowingRadiiMinRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiMaxRatio", kStrobedNarrowingRadiiMaxRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiDpParam", kStrobedNarrowingRadiiDpParam);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kStrobedNarrowingRadiiParam2", kStrobedNarrowingRadiiParam2);

        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiMinRatio", kPlacedNarrowingRadiiMinRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiMaxRatio", kPlacedNarrowingRadiiMaxRatio);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingStartingParam2", kPlacedNarrowingStartingParam2);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kPlacedNarrowingRadiiDpParam", kPlacedNarrowingRadiiDpParam);

        // ONNX Detection Configuration values will be loaded later via LoadConfigurationValues()
        // which is called after the JSON config file has been loaded in main()

        // kLogIntermediateSpinImagesToFile is now loaded by SpinAnalyzer::LoadConfigurationValues()
        
        // Preload model at startup if using experimental detection for either ball placement or flight
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi" ||
            kBallPlacementDetectionMethod == "experimental") {
            GS_LOG_MSG(info, "Detection method is '" + kDetectionMethod + "' / Placement method is '" + kBallPlacementDetectionMethod + "', preloading YOLO model at startup...");

            // Try ONNX Runtime first if configured
            if (kONNXBackend == "onnxruntime") {
                if (PreloadONNXRuntimeModel()) {
                    GS_LOG_MSG(info, "ONNX Runtime model preloaded successfully - first detection will be fast!");
                } else {
                    GS_LOG_MSG(warning, "Failed to preload ONNX Runtime model");
                    if (kONNXRuntimeAutoFallback) {
                        GS_LOG_MSG(info, "Auto-fallback enabled, attempting to preload OpenCV DNN model...");
                        if (PreloadYOLOModel()) {
                            GS_LOG_MSG(info, "OpenCV DNN fallback model preloaded successfully!");
                        } else {
                            GS_LOG_MSG(warning, "Failed to preload both ONNX Runtime and OpenCV DNN models");
                        }
                    }
                }
            } else {
                // Use OpenCV DNN backend
                if (PreloadYOLOModel()) {
                    GS_LOG_MSG(info, "OpenCV DNN model preloaded successfully - first detection will be fast!");
                } else {
                    GS_LOG_MSG(warning, "Failed to preload OpenCV DNN model - will load on first detection");
                }
            }
        }
    }

    BallImageProc::~BallImageProc() {
        // Cleanup is handled by static cleanup, since model is shared across instances
        // Call CleanupONNXRuntime() only on program exit, not per-instance destruction
    }

    /**
     * Given a gray-scale image and a ball search mode (e.g., kStrobed), this function applies
     * CLAHE processing to improve the contrast and edge definition of the balls.  It then
     * applies a Guassian blur and edge detection to the image.
     * 
     * \param search_image  The image to be processed.
     * \param search_mode  Currently can be only kStrobed or kExternallyStrobed
     * \return True on success.
     */
    // PreProcessStrobedImage - Delegated to HoughDetector
    bool BallImageProc::PreProcessStrobedImage( cv::Mat& search_image,
                                                BallSearchMode search_mode) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::PreProcessStrobedImage - Delegating to HoughDetector");

        // Convert mode and delegate
        HoughDetector::BallSearchMode hough_mode;
        switch (search_mode) {
            case kStrobed:
                hough_mode = HoughDetector::kStrobed;
                break;
            case kExternallyStrobed:
                hough_mode = HoughDetector::kExternallyStrobed;
                break;
            default:
                GS_LOG_MSG(error, "PreProcessStrobedImage called with invalid search_mode");
                return false;
        }

        return HoughDetector::PreProcessStrobedImage(search_image, hough_mode);
    }

    // OLD IMPLEMENTATION (preserved for reference)
    /*
    bool BallImageProc::PreProcessStrobedImage_OLD( cv::Mat& search_image,
                                                BallSearchMode search_mode) {

        GS_LOG_TRACE_MSG(trace, "PreProcessStrobedImage");

        if (search_image.empty()) {
            GS_LOG_MSG(error, "PreProcessStrobedImage called with no image to work with (search_image)");
            return false;
        }

        // setup CLAHE processing dependent on PiTrac-only strobing or externally-strobed
        
        bool use_clahe_processing = true;
        int clahe_tiles_grid_size = -1;
        int clahe_clip_limit = -1;

        if (search_mode == kStrobed) {
            use_clahe_processing = kUseCLAHEProcessing;
            clahe_tiles_grid_size = kCLAHETilesGridSize;
            clahe_clip_limit = kCLAHEClipLimit;
        }
        else if (search_mode == kExternallyStrobed) {
            use_clahe_processing = kExternallyStrobedUseCLAHEProcessing;
            clahe_tiles_grid_size = kExternallyStrobedCLAHETilesGridSize;
            clahe_clip_limit = kExternallyStrobedCLAHEClipLimit;
        }
        else {
            GS_LOG_MSG(error, "PreProcessStrobedImage called with invalid search_mode)");
            return false;
        }

        // Create a CLAHE object

        if (use_clahe_processing) {
            cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();

            // Set CLAHE parameters

            if (clahe_tiles_grid_size < 1) {
                clahe_tiles_grid_size = 1;
                GS_LOG_MSG(warning, "clahe_tiles_grid_size was < 1 - Resetting to 1.");
            }
            if (clahe_clip_limit < 1) {
                clahe_clip_limit = 1;
                GS_LOG_MSG(warning, "kCLAHEClipLimit was < 1 - Resetting to 1.");
            }

            GS_LOG_TRACE_MSG(trace, "Using CLAHE Pre-processing with GridSize = " + std::to_string(clahe_tiles_grid_size) +
                ", ClipLimit = " + std::to_string(clahe_clip_limit));

            clahe->setClipLimit(clahe_clip_limit);
            clahe->setTilesGridSize(cv::Size(clahe_tiles_grid_size, clahe_tiles_grid_size));

            // Apply CLAHE
            cv::Mat equalizedImage;
            clahe->apply(search_image, search_image);

            LoggingTools::DebugShowImage(image_name_ + "  Strobed Ball Image - After CLAHE equalization", search_image);
        }

        double canny_lower = 0.0;
        double canny_upper = 0.0;
        int pre_canny_blur_size = 0;
        int pre_hough_blur_size = 0;

        if (search_mode == kStrobed) {
            if (kStrobedBallsUseAltHoughAlgorithm) {
                canny_lower = kStrobedBallsAltCannyLower;
                canny_upper = kStrobedBallsAltCannyUpper;
                pre_canny_blur_size = kStrobedBallsAltPreCannyBlurSize;
                pre_hough_blur_size = kStrobedBallsAltPreHoughBlurSize;
            }
            else {
                canny_lower = kStrobedBallsCannyLower;
                canny_upper = kStrobedBallsCannyUpper;
                pre_canny_blur_size = kStrobedBallsPreCannyBlurSize;
                pre_hough_blur_size = kStrobedBallsPreHoughBlurSize;
            }
        }
        else if (search_mode == kExternallyStrobed) {
            canny_lower = kExternallyStrobedEnvCannyLower;
            canny_upper = kExternallyStrobedEnvCannyUpper;
            pre_canny_blur_size = kExternallyStrobedEnvPreCannyBlurSize;
            pre_hough_blur_size = kExternallyStrobedEnvPreHoughBlurSize;
        }

        // The size for the blur must be odd - force it up in value by 1 if necessary
        if (pre_canny_blur_size > 0) {
            if (pre_canny_blur_size % 2 != 1) {
                pre_canny_blur_size++;
            }
        }

        if (pre_hough_blur_size > 0) {
            if (pre_hough_blur_size % 2 != 1) {
                pre_hough_blur_size++;
            }
        }


        GS_LOG_MSG(trace, "Main HoughCircle Image Prep - Performing Pre-Hough Blur and Canny for kStrobed mode.");
        GS_LOG_MSG(trace, "  Blur Parameters are: pre_canny_blur_size = " + std::to_string(pre_canny_blur_size) +
            ", pre_hough_blur_size " + std::to_string(pre_hough_blur_size));
        GS_LOG_MSG(trace, "  Canny Parameters are: canny_lower = " + std::to_string(canny_lower) +
            ", canny_upper " + std::to_string(canny_upper));


        if (pre_canny_blur_size > 0) {
            cv::GaussianBlur(search_image, search_image, cv::Size(pre_canny_blur_size, pre_canny_blur_size), 0);
        }
        else {
            GS_LOG_TRACE_MSG(trace, "Skipping pre-Canny Blur");
        }

        // TBD - REMOVED THIS FOR NOW - IT DOESN'T SEEM TO HELP
        for (int i = 0; i < 0; i++) {
            cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
            cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
        }

        LoggingTools::DebugShowImage(image_name_ + "  Strobed Ball Image - Ready for Edge Detection", search_image);

        cv::Mat cannyOutput_for_balls;
        if (search_mode == kExternallyStrobed && pre_canny_blur_size == 0) {
            // Don't do the Canny at all if the blur size is zero and we're in comparison mode
            cannyOutput_for_balls = search_image.clone();
        }
        else {
            cv::Canny(search_image, cannyOutput_for_balls, canny_lower, canny_upper);
        }

        LoggingTools::DebugShowImage(image_name_ + "  cannyOutput_for_balls", cannyOutput_for_balls);

        // Blur the lines-only image back to the search_image that the code below uses
        cv::GaussianBlur(cannyOutput_for_balls, search_image, cv::Size(pre_hough_blur_size, pre_hough_blur_size), 0);   // Nominal is 7x7

        return true;
    }

    // Given a picture, see if we can find the golf ball somewhere in that picture.
    // Should be much more successful if called with a calibrated golf ball so that the code has
    // some hints about where to look.
    // Returns a new GolfBall object iff success. 
    // Helper: Convert BallImageProc::BallSearchMode to SearchStrategy::Mode
    static SearchStrategy::Mode ConvertSearchMode(BallImageProc::BallSearchMode mode) {
        switch (mode) {
            case BallImageProc::kFindPlacedBall:
                return SearchStrategy::kFindPlacedBall;
            case BallImageProc::kStrobed:
                return SearchStrategy::kStrobed;
            case BallImageProc::kExternallyStrobed:
                return SearchStrategy::kExternallyStrobed;
            case BallImageProc::kPutting:
                return SearchStrategy::kPutting;
            case BallImageProc::kUnknown:
            default:
                return SearchStrategy::kUnknown;
        }
    }

    // --- NEW IMPLEMENTATION: Delegates to BallDetectorFacade ---

    bool BallImageProc::GetBall(const cv::Mat& rgbImg,
                                const GolfBall& baseBallWithSearchParams,
                                std::vector<GolfBall> &return_balls,
                                cv::Rect& expectedBallArea,
                                BallSearchMode search_mode,
                                bool chooseLargestFinalBall,
                                bool report_find_failures) {

        auto getball_start = std::chrono::high_resolution_clock::now();
        GS_LOG_TRACE_MSG(trace, "BallImageProc::GetBall - Delegating to BallDetectorFacade (search_mode = " +
                        std::to_string(search_mode) + ")");

        if (rgbImg.empty()) {
            GS_LOG_MSG(error, "GetBall called with no image to work with (rgbImg)");
            return false;
        }

        // Convert BallSearchMode to SearchStrategy::Mode
        SearchStrategy::Mode facade_mode = ConvertSearchMode(search_mode);

        // Delegate to BallDetectorFacade
        bool result = BallDetectorFacade::GetBall(
            rgbImg,
            baseBallWithSearchParams,
            return_balls,
            expectedBallArea,
            facade_mode,
            chooseLargestFinalBall,
            report_find_failures
        );

        auto getball_end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(getball_end - getball_start);
        GS_LOG_TRACE_MSG(trace, "GetBall completed in " + std::to_string(duration.count()) + " ms");

        return result;
    }

    // --- OLD IMPLEMENTATION (preserved for rollback if needed) ---
    /*
    bool BallImageProc::GetBall_OLD(const cv::Mat& rgbImg,
                                const GolfBall& baseBallWithSearchParams,
                                std::vector<GolfBall> &return_balls,
                                cv::Rect& expectedBallArea,
                                BallSearchMode search_mode,
                                bool chooseLargestFinalBall,
                                bool report_find_failures) {

        auto getball_start = std::chrono::high_resolution_clock::now();
        GS_LOG_TRACE_MSG(trace, "GetBall called with PREBLUR_IMAGE = " + std::to_string(PREBLUR_IMAGE) + " IS_COLOR_MASKING = " +
                    std::to_string(IS_COLOR_MASKING) + " FINAL_BLUR = " + std::to_string(FINAL_BLUR) + " search_mode = " + std::to_string(search_mode));

        if (rgbImg.empty()) {
            GS_LOG_MSG(error, "GetBall called with no image to work with (rgbImg)");
            return false;
        }

        // *** ONNX DETECTION INTEGRATION - Process through full trajectory analysis pipeline ***
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            std::vector<GsCircle> onnx_circles;
            if (DetectBallsONNX(rgbImg, search_mode, onnx_circles)) {
                // Convert GsCircle results to GolfBall objects for trajectory analysis
                return_balls.clear();
                for (size_t i = 0; i < onnx_circles.size(); ++i) {
                    GolfBall ball;
                    ball.quality_ranking = static_cast<int>(i); // ONNX confidence-based ranking
                    ball.set_circle(onnx_circles[i]);
                    ball.ball_color_ = GolfBall::BallColor::kONNXDetected; // Mark as ONNX-detected
                    ball.measured_radius_pixels_ = onnx_circles[i][2];
                    ball.radius_at_calibration_pixels_ = baseBallWithSearchParams.radius_at_calibration_pixels_;

                    // Set color info - ONNX doesn't analyze color but we need placeholders
                    ball.average_color_ = baseBallWithSearchParams.average_color_;
                    ball.median_color_ = baseBallWithSearchParams.average_color_;
                    ball.std_color_ = GsColorTriplet(0, 0, 0); // Zero std indicates no color analysis

                    return_balls.push_back(ball);
                }

                return !return_balls.empty();
            } else {
                GS_LOG_MSG(warning, "ONNX detection failed - no balls found");
                return false;
            }
        }

        GS_LOG_TRACE_MSG(trace, "Using legacy HoughCircles detection");
        GS_LOG_TRACE_MSG(trace, "Looking for a ball with color{ " + LoggingTools::FormatGsColorTriplet(baseBallWithSearchParams.average_color_));
        LoggingTools::DebugShowImage(image_name_ + "  rgbImg", rgbImg);

        // Blur the image to reduce noise - TBD - Would medianBlur be better ?
        // img_blur = cv::medianBlur(grayImage, 5)
        // Blur the image before trying to identify circles (if desired)
        cv::Mat blurImg = area_mask_image_.clone();

        // This seems touchy, too.  Nominal is 7 right now.
        if (PREBLUR_IMAGE) {
            cv::GaussianBlur(rgbImg, blurImg, cv::Size(7, 7), 0);  // nominal was 11x11
            LoggingTools::DebugShowImage(image_name_ + "  Pre-blurred image", blurImg);
        }
        else {
            blurImg = rgbImg.clone();
        }


        // construct a colorMask for the expected ball color range
        // Note - We want to UNDER-colorMask if anything.Just get rid of stuff that is
        // pretty certainly NOT the golf ball
        // Need an HSV image to work with the HSV-based masking function
        int stype = blurImg.type();

        if (stype == CV_8U) {
            GS_LOG_MSG(error, "GetBall called with a 1-channel (grayscale?) image.  Expecting 3 channel RGB");
            return false;
        }


        // We will create our own colorMask if we don't have one already
        // We will not do anything with the areaMask(other than to apply it further below if it exists)
        if (color_mask_image_.empty()) {

            cv::Mat hsvImage;
            cv::cvtColor(blurImg, hsvImage, cv::COLOR_BGR2HSV);

            // Save the colorMask for later debugging as well as for use below
            color_mask_image_ = GetColorMaskImage(hsvImage, baseBallWithSearchParams);
        }

        // LoggingTools::DebugShowImage(image_name_ + "  cv::GaussianBlur(...) hsvImage", hsvImage);
        // LoggingTools::DebugShowImage(image_name_ + "  color_mask_image_", color_mask_image_);

        // Perform a Hough conversion to identify circles or near-circles

        // Convert the blurred version of the original image to required gray-scale for Hough Transform circle detection
        cv::Mat grayImage;
        cv::cvtColor(blurImg, grayImage, cv::COLOR_BGR2GRAY);

        // LoggingTools::DebugShowImage(image_name_ + "  gray image (as well as the result if no colorMasking)", grayImage);

        cv::Mat search_image = cv::Mat::zeros(grayImage.size(), grayImage.type());

        // Bitwise-AND the colorMask and original image
        // NOTE - THIS COLOR MASKING MAY ACTUALLY BE HURTING US!!!
        if (IS_COLOR_MASKING) {
            cv::bitwise_and(grayImage, color_mask_image_, search_image);
            LoggingTools::DebugShowImage(image_name_ + "  colorMasked image (search_image)", search_image);
        }
        else {
            search_image = grayImage;
        }

        // Apply any area mask
        if (false && !area_mask_image_.empty()) {
            cv::bitwise_and(search_image, area_mask_image_, search_image);
        }

        LoggingTools::DebugShowImage(image_name_ + "  Final color AND area-masked image (search_image)", search_image);

        switch (search_mode) {
            case kFindPlacedBall: {

               cv::GaussianBlur(search_image, search_image, cv::Size(kPlacedPreCannyBlurSize, kPlacedPreCannyBlurSize), 0);

                 // TBD - REMOVED THIS FOR NOW
                 for (int i = 0; i < 0; i++) {
                     cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                     cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                 }

                 LoggingTools::DebugShowImage(image_name_ + "  Placed Ball Image - Ready for Edge Detection", search_image);

                 /*
                 EDPF testEDPF = EDPF(search_image);
                 Mat edgePFImage = testEDPF.getEdgeImage();
                 edgePFImage = edgePFImage * -1 + 255;
                 search_image = edgePFImage;
                 */
                 cv::Mat cannyOutput_for_balls;
                 cv::Canny(search_image, cannyOutput_for_balls, kPlacedBallCannyLower, kPlacedBallCannyUpper);

                 LoggingTools::DebugShowImage(image_name_ + "  cannyOutput_for_balls", cannyOutput_for_balls);

                 // Blur the lines-only image back to the search_image that the code below uses
                 cv::GaussianBlur(cannyOutput_for_balls, search_image, cv::Size(kPlacedPreHoughBlurSize, kPlacedPreHoughBlurSize), 0);   // Nominal is 7x7


                 break;
            }

            case kStrobed: {

                if (!PreProcessStrobedImage(search_image, kStrobed)) {
                    GS_LOG_MSG(error, "Failed to PreProcessStrobedImage");
                    return false;
                }

                break;
            }


            case kExternallyStrobed: {

                // Attempt to remove the lines of the golf-shaft in an externally-strobed environment.  
                // Sadly, this is better accomplished by simply putting IR-black felt over the shaft.
                std::vector<cv::Vec4i> lines;

                if (GolfSimCamera::kExternallyStrobedEnvFilterImage) {
                    if (!GolfSimCamera::CleanExternalStrobeArtifacts(rgbImg, search_image, lines)) {
                        GS_LOG_MSG(warning, "ProcessReceivedCam2Image - failed to CleanExternalStrobeArtifacts.");
                    }

                    LoggingTools::DebugShowImage(image_name_ + "After CleanExternalStrobeArtifacts", search_image);
                }

                if (!PreProcessStrobedImage(search_image, kExternallyStrobed)) {
                    GS_LOG_MSG(error, "Failed to PreProcessStrobedImage");
                    return false;
                }

                break;
            }

            case kPutting: {

                cv::medianBlur(search_image, search_image, kPuttingPreHoughBlurSize);

                for (int i = 0; i < 0; i++) {
                    cv::erode(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                    cv::dilate(search_image, search_image, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 3);
                }

                LoggingTools::DebugShowImage(image_name_ + "  Putting Image - Ready for Edge Detection", search_image);

                EDPF testEDPF = EDPF(search_image);
                Mat edgePFImage = testEDPF.getEdgeImage();
                edgePFImage = edgePFImage * -1 + 255;
                search_image = edgePFImage;

                cv::GaussianBlur(search_image, search_image, cv::Size(5, 5), 0);   // Nominal is 7x7

                break;
            }

            case kUnknown: {
            default:
                 GS_LOG_MSG(error, "BallImageProc::GetBall called with invalid search_mode");
                 return false;
                 break;
            }
        }


        LoggingTools::DebugShowImage(image_name_ + "  FINAL blurred/eroded/dilated Putting search_image for Hough Transform{ ", search_image);

        if (GolfSimOptions::GetCommandLineOptions().artifact_save_level_ != ArtifactSaveLevel::kNoArtifacts) {
            // TBD - REMOVE - Not really  useful any more
            // LoggingTools::LogImage("", search_image, std::vector < cv::Point >{}, true, "log_view_final_search_image_for_Hough.png");
        }

        // Apply hough transform on the image - NOTE - param2 is critical to balance between over - and under - identification
        // works            circles = cv::HoughCircles(img_blur, cv::HOUGH_GRADIENT, 1, minDist = 50, param1 = 200, param2 = 25, minRadius = 10, maxRadius = 50)
        // should be image_blur, not search_image ?

        // Param 1 will set the sensitivity; how strong the edges of the circles need to be.Too high and it won't detect anything, too low and it 
        // will find too much clutter.Param 2 will set how many edge points it needs to find to declare that it's found a circle. Again, too high 
        // will detect nothing, too low will declare anything to be a circle.
        // (See https{//dsp.stackexchange.com/questions/22648/in-opecv-function-hough-circles-how-does-parameter-1-and-2-affect-circle-detecti)

        // We will start with a best-guess transform parameter.If that results in one circle, great.And we're done.
        // If we get more than one circle, tighten the parameter so see if we can get just one.If not, we'll sort through the
        // circles further below.But if we don't get any circles with the starting point, loosen the parameter up to see if we 
        // can get at least one.

        bool done = false;
        std::vector<GsCircle> circles;
        double starting_param2;
        double min_param2;
        double max_param2;

        // Min number of circles will override max if necessary
        int min_circles_to_return_from_hough;
        // This is only small for when we are REALLY sure of where the ball is, like during calibration
        int max_circles_to_return_from_hough;

        int minimum_search_radius = 0;

        // Determine reasonable min / max radii if we don't know it
        if (min_ball_radius_ < 0.0) {
            minimum_search_radius = int(CvUtils::CvHeight(search_image) / 15);
        }
        else {
            minimum_search_radius = min_ball_radius_;
        }

        int maximum_search_radius = 0;

        if (max_ball_radius_ < 0.0) {
            maximum_search_radius = int(CvUtils::CvHeight(search_image) / 6);
        }
        else {
            maximum_search_radius = max_ball_radius_;
        }

        // If we are in strobed mode, allow for circles that are overlapping and of lower quality, etc.

        double minimum_distance;

        double currentParam1;    // nominal is 200.  Touchy - higher values sometimes do not work - CURRENT = 100
        double param2_increment;
        double currentDp;

        // Otherwise,if highly-certain we will find just one ball, crank the requirements to prevent false positives, otherwise, relax them
        switch (search_mode) {
            case kFindPlacedBall:
            {
                starting_param2 = kPlacedBallStartingParam2; // Nominal{  25
                min_param2 = kPlacedBallMinParam2;  // Nominal { 15
                max_param2 = kPlacedBallMaxParam2;

                currentParam1 = kPlacedBallCurrentParam1;    // nominal is 200.  Touchy - higher values sometimes do not work - CURRENT = 100
                param2_increment = kPlacedBallParam2Increment;

                min_circles_to_return_from_hough = kPlacedMinHoughReturnCircles;
                max_circles_to_return_from_hough = kPlacedMaxHoughReturnCircles;

                // In the expected image, there should be only one candidate anywhere near the ball
                minimum_distance = (double)minimum_search_radius * 0.5;

                currentDp = kPlacedBallHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kStrobed:
            {
                bool use_alt = kStrobedBallsUseAltHoughAlgorithm;

                starting_param2 = use_alt ? kStrobedBallsAltStartingParam2 : kStrobedBallsStartingParam2;
                min_param2 = use_alt ? kStrobedBallsAltMinParam2 : kStrobedBallsMinParam2;
                max_param2 = use_alt ? kStrobedBallsAltMaxParam2 : kStrobedBallsMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = use_alt ? kStrobedBallsAltCurrentParam1 : kStrobedBallsCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = use_alt ? kStrobedBallsAltParam2Increment : kStrobedBallsParam2Increment;

                minimum_distance = minimum_search_radius * .3; // 0.18f;  // TBD - Parameterize this!

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kStrobedBallsMinHoughReturnCircles;
                max_circles_to_return_from_hough = kStrobedBallsMaxHoughReturnCircles;

                currentDp = use_alt ? kStrobedBallsAltHoughDpParam1 : kStrobedBallsHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kExternallyStrobed:
            {
                starting_param2 = kExternallyStrobedEnvStartingParam2;
                min_param2 = kExternallyStrobedEnvMinParam2;
                max_param2 = kExternallyStrobedEnvMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = kExternallyStrobedEnvCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = kExternallyStrobedEnvParam2Increment;

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kExternallyStrobedEnvMinHoughReturnCircles;
                max_circles_to_return_from_hough = kExternallyStrobedEnvMaxHoughReturnCircles;

                currentDp = kExternallyStrobedEnvHoughDpParam1;

                /** TBD - REMOVE?  Any reason to have this mode's own radius parameters?
                minimum_search_radius = kExternallyStrobedEnvMinimumSearchRadius;
                maximum_search_radius = kExternallyStrobedEnvMaximumSearchRadius;
                ***/

                minimum_distance = (double)minimum_search_radius * 0.2;

                break;
            }

            case kPutting:
            {
                starting_param2 = kPuttingBallStartingParam2;
                min_param2 = kPuttingBallMinParam2;
                max_param2 = kPuttingBallMaxParam2;
                // In the strobed image, there may be overlapping balls. so the search distance should be small

                // The lower the value, the sloppier the found circles can be.  But crank it up too far and 
                // we don't pick up overlapped circles.
                currentParam1 = kPuttingBallCurrentParam1;
                // Don't want to get too crazy loose too fast in order to find more balls
                param2_increment = kPuttingBallParam2Increment;

                minimum_distance = minimum_search_radius * 0.5;

                // We have to have at least two candidate balls to do spin analysis
                // Try for more tp make sure we get all the overlapped balls.
                min_circles_to_return_from_hough = kPuttingMinHoughReturnCircles;
                max_circles_to_return_from_hough = kPuttingMaxHoughReturnCircles;

                currentDp = kPuttingHoughDpParam1;         // Must be between 0 and 2 (double).Nominal is 2, CURRENT = 1.2
                break;
            }
            case kUnknown:
            default:
                GS_LOG_MSG(error, "BallImageProc::GetBall called with invalid search_mode");
                return false;
                break;
        }


        double currentParam2 = starting_param2;

        int priorNumCircles = 0;
        int finalNumberOfFoundCircles = 0;

        bool currentlyLooseningSearch = false;

        cv::Mat final_search_image;

        // HoughCircles() is expensive - use it only in the region of interest if we have an ROI
        cv::Point offset_sub_to_full;
        cv::Point offset_full_to_sub;
        if (expectedBallArea.tl().x != 0 || expectedBallArea.tl().y != 0 ||
            expectedBallArea.br().x != 0 || expectedBallArea.br().y != 0) {

            // Note - if the expectedBallArea ROI is invalid, it will be corrected.
            final_search_image = CvUtils::GetSubImage(search_image, expectedBallArea, offset_sub_to_full, offset_full_to_sub);
        }
        else {
            // Do nothing if we don't have a sub-image.  Any later offsets will be 0, so will do nothing
            final_search_image = search_image;
        }

        // LoggingTools::DebugShowImage(image_name_ + "  Final sub-image search_image for Hough Transform{ ", final_search_image);
        // LoggingTools::LogImage("", final_search_image, std::vector < cv::Point >{}, true, "log_view_final_sub_image_for_Hough.png");

        cv::HoughModes hough_mode = cv::HOUGH_GRADIENT_ALT;

        if (search_mode != kFindPlacedBall) {
            if (kStrobedBallsUseAltHoughAlgorithm) {
                GS_LOG_TRACE_MSG(trace, "Using HOUGH_GRADIENT_ALT.");
                hough_mode = cv::HOUGH_GRADIENT_ALT;
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Using HOUGH_GRADIENT.");
                hough_mode = cv::HOUGH_GRADIENT;
            }
        }

        if (search_mode == kStrobed || search_mode == kExternallyStrobed || search_mode == kFindPlacedBall) {

            if (kUseDynamicRadiiAdjustment && search_mode != kFindPlacedBall) {

                double min_ratio;
                double max_ratio;
                double narrowing_radii_param2;
                double narrowing_dp_param;

                if (search_mode == kFindPlacedBall) {
                    min_ratio = kPlacedNarrowingRadiiMinRatio;
                    max_ratio = kPlacedNarrowingRadiiMaxRatio;
                    minimum_distance = minimum_search_radius * 0.7;
                    narrowing_radii_param2 = kPlacedNarrowingStartingParam2;
                    narrowing_dp_param = kPlacedNarrowingRadiiDpParam;
                }
                else {
                    min_ratio = kStrobedNarrowingRadiiMinRatio;
                    max_ratio = kStrobedNarrowingRadiiMaxRatio;
                    minimum_distance = minimum_search_radius * 0.8;
                    narrowing_radii_param2 = kStrobedNarrowingRadiiParam2;
                    narrowing_dp_param = kStrobedNarrowingRadiiDpParam;
                }

                // Externally-strobed environments need a looser Param2
                if (search_mode == kExternallyStrobed) {
                    narrowing_radii_param2 = kExternallyStrobedEnvNarrowingParam2;
                    narrowing_dp_param = kExternallyStrobedEnvNarrowingDpParam;
                }
                    
                // For some reason, odd maximum_search_radius values were resulting in bad circle identification
                // These are the wider-ranging radii to make sure we find the ball, however near/far it may be
                minimum_search_radius = CvUtils::RoundAndMakeEven(minimum_search_radius);
                maximum_search_radius = CvUtils::RoundAndMakeEven(maximum_search_radius);

                GS_LOG_TRACE_MSG(trace, "Executing INITIAL houghCircles (to determine narrowed ball diameters) with currentDP = " + std::to_string(narrowing_dp_param) +
                    ", minDist = " + std::to_string(minimum_distance) + ", param1 = " + std::to_string(currentParam1) +
                    ", param2 = " + std::to_string(narrowing_radii_param2) + ", minRadius = " + std::to_string(int(minimum_search_radius)) +
                    ", maxRadius = " + std::to_string(int(maximum_search_radius)));

                // TBD - May want to adjust min / max radius
                // NOTE - Param 1 may be sensitive as well - needs to be 100 for large pictures ?
                // TBD - Need to set minDist to rows / 8, roughly ?
                // The _ALT mode seems to work best for this purpose
                std::vector<GsCircle> test_circles;
                
                    cv::HoughCircles(final_search_image,
                    test_circles,
                    cv::HOUGH_GRADIENT_ALT,
                    narrowing_dp_param,
                    minimum_distance, 
                    kPlacedNarrowingParam1,
                    narrowing_radii_param2,
                    (int)minimum_search_radius,
                    (int)maximum_search_radius );
                

                {
                    int MAX_CIRCLES_TO_EVALUATE = 100;
                    int kMaxCirclesToEmphasize = 8;
                    int i = 0;
                    cv::Mat test_hough_output = final_search_image.clone();

                    if (test_circles.size() == 0) {
                        if (report_find_failures) {
                            GS_LOG_TRACE_MSG(warning, "Initial (narrowing) Hough Transform found 0 balls.");
                        }
                        return false;
                    }

                    if (!RemoveSmallestConcentricCircles(test_circles)) {
                        GS_LOG_TRACE_MSG(warning, "Failed to RemoveSmallestConcentricCircles.");
                        return false;
                    }

                    for (auto& c : test_circles) {

                        i += 1;

                        if (i > MAX_CIRCLES_TO_EVALUATE) {
                            break;
                        }

                        int found_radius = (int)std::round(c[2]);

                        LoggingTools::DrawCircleOutlineAndCenter(test_hough_output, c, std::to_string(i), i, (i > kMaxCirclesToEmphasize));

                    }
                    LoggingTools::DebugShowImage("Initial (for narrowing) Hough-identified Circles", test_hough_output);
                    GS_LOG_TRACE_MSG(trace, "Narrowing Hough found the following circles: {     " + LoggingTools::FormatCircleList(test_circles));
                }

                const int number_balls_to_average = std::min(kNumberRadiiToAverageForDynamicAdjustment, (int)test_circles.size());
                double average = 0.0;

                for (int i = 0; i < number_balls_to_average; i++) {
                    average += test_circles[i][2] / number_balls_to_average;
                }

                minimum_search_radius = CvUtils::RoundAndMakeEven(average * min_ratio);
                maximum_search_radius = CvUtils::RoundAndMakeEven(average * max_ratio);

                minimum_distance = minimum_search_radius * 0.6;

                /* TBD - REMOVE - Not necessary for GRADIENT_ALT now
                if (kUseDynamicRadiiAdjustment && (search_mode == kFindPlacedBall)) {
                    // If we're using dynamic radii adjustment, we'd like to look at potentially several circles in a tight area
                    minimum_distance = 1;
                }
                */

                GS_LOG_TRACE_MSG(trace, "Dynamically narrowing search radii to { " + std::to_string(minimum_search_radius) +
                    ", " + std::to_string(maximum_search_radius) + " } pixels.");
            }

        }

        // NEW: ONNX detection bypass - skip adaptive parameter tuning for ONNX
        if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            GS_LOG_TRACE_MSG(trace, "Using ONNX detection - bypassing adaptive parameter tuning");
            
            std::vector<GsCircle> test_circles;
            if (DetectBalls(final_search_image, search_mode, test_circles)) {

                GS_LOG_MSG(trace, "DetectBalls succeeded - initially found " + std::to_string(test_circles.size()) + " circles.");

                // Apply radius filtering to ONNX results
                auto it = test_circles.begin();
                while (it != test_circles.end()) {
                    if ((*it)[2] < minimum_search_radius || (*it)[2] > maximum_search_radius) {
                        it = test_circles.erase(it);
                    } else {
                        ++it;
                    }
                }
                
                if (!test_circles.empty()) {
                    circles.assign(test_circles.begin(), test_circles.end());

                    // ⚠️  WARNING: This old ONNX coordinate offset logic should NEVER execute
                    // with the new early bypass. If you see this, the early bypass failed!
                    GS_LOG_MSG(error, "OLD ONNX path executed - this indicates early bypass failure!");

                    // Apply coordinate transformation if using sub-image
                    for (auto& c : circles) {
                        c[0] += offset_sub_to_full.x;
                        c[1] += offset_sub_to_full.y;
                    }

                    finalNumberOfFoundCircles = (int)circles.size();
                } else {
                    if (report_find_failures) {
                        GS_LOG_MSG(warning, "ONNX detection found no balls within radius constraints");
                    }
                    return false;
                }
            } else {
                if (report_find_failures) {
                    GS_LOG_MSG(warning, "ONNX detection failed to find any balls");
                }
                return false;
            }
            
            // Skip to post-processing (jump past the HoughCircles adaptive loop)
            goto post_detection_processing;
        }

        // Adaptive algorithm to dynamically adjust the (very touchy) Hough circle parameters depending on how things are going
        while (!done) {

            minimum_search_radius = CvUtils::RoundAndMakeEven(minimum_search_radius);
            maximum_search_radius = CvUtils::RoundAndMakeEven(maximum_search_radius);

            GS_LOG_TRACE_MSG(trace, "Executing houghCircles with currentDP = " + std::to_string(currentDp) +
                ", minDist = " + std::to_string(minimum_distance) + ", param1 = " + std::to_string(currentParam1) +
                ", param2 = " + std::to_string(currentParam2) + ", minRadius = " + std::to_string(int(minimum_search_radius)) +
                ", maxRadius = " + std::to_string(int(maximum_search_radius)));

            // TBD - May want to adjust min / max radius
            // NOTE - Param 1 may be sensitive as well - needs to be 100 for large pictures ?
            // TBD - Need to set minDist to rows / 8, roughly ?
            std::vector<GsCircle> test_circles;
            
            cv::HoughCircles(final_search_image,
                test_circles,
                hough_mode,
                currentDp,
                /* minDist = */ minimum_distance, // Does this really matter if we are only looking for one circle ?
                /* param1 = */ currentParam1,
                /* param2 = */ currentParam2,
                /* minRadius = */ (int)minimum_search_radius,
                /* maxRadius = */ (int)maximum_search_radius);

            // Save the prior number of circles if we need it later
            if (!circles.empty()) {
                priorNumCircles = (int)round(circles.size());       // round just to make sure we get an int - this should be evenly divisible
            }
            else {
                priorNumCircles = 0;
            }

            int numCircles = 0;

            if (!test_circles.empty()) {
                numCircles = (int)std::round(test_circles.size());
                GS_LOG_TRACE_MSG(trace, "Hough FOUND " + std::to_string(numCircles) + " circles.");
            }
            else {
                numCircles = 0;
            }

            if (!RemoveSmallestConcentricCircles(test_circles)) {
                GS_LOG_TRACE_MSG(warning, "Failed to RemoveSmallestConcentricCircles.");
                return false;
            }

            // If we find only a small number of circles, that may be ok.
            // Might be able to post-process the number down further later.
            if (numCircles >= min_circles_to_return_from_hough && numCircles <= max_circles_to_return_from_hough) {
                // We found what we consider to be a reasonable number of circles
                circles.assign(test_circles.begin(), test_circles.end());
                finalNumberOfFoundCircles = numCircles;
                done = true;
                break;
            }

            // we should take only ONE of the following branches
            if (numCircles > max_circles_to_return_from_hough) {
                // We found TOO MANY circles.
                // // Hopefully, we can either further tighten the transform to reduce the number of candidates,
                // of else we've been broadening and the prior attempt gave 0 circles but now we have too many (more than 1) 
                // (but at least we have SOME circles instead of 0 now)
                GS_LOG_TRACE_MSG(trace, "Found more circles than desired (" + std::to_string(numCircles) + " circles).");

                if ((priorNumCircles == 0) && (currentParam2 != starting_param2)) {
                    // We have too many circles now, and we had no circles before.So this is as good as we can do, at least
                    // using the currently (possibly too-coarse) increment
                    // In this case, just return what we had
                    GS_LOG_TRACE_MSG(trace, "Could not narrow number of balls to just 1");
                    // Save what we have now - deep copy
                    circles.assign(test_circles.begin(), test_circles.end());

                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }


                // We had too many balls before, and we still do now. So, see if we can tighten up our Hough transform
                if (currentParam2 >= max_param2) {
                    // We've tightened things as much as we want to, but still have too many possible balls
                    // We'll try to sort them out later
                    GS_LOG_TRACE_MSG(trace, "Could not narrow number of balls to just 1.  Produced " + std::to_string(numCircles) + " balls.");

                    // Save what we have now because maybe it's as good as things get
                    circles.assign(test_circles.begin(), test_circles.end());
                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }
                else {
                    // Next time we might not get any circles, so save what we have now
                    circles.assign(test_circles.begin(), test_circles.end());
                    currentParam2 += param2_increment;
                    currentlyLooseningSearch = false;
                    done = false;
                }
            }
            else {
                // We may have found some circles this time. 
                // Hopefully we either can further loosen the transform to find more, or we can't *BUT* we found some in the earlier attempt 
                // Two possible conditions here -
                //   1 - either we have been progressively tightening(increasing) currentParam2 and we went too far and now
                //       we have zero potential balls; OR
                //   2 - we started not finding ANY balls, kept loosening(decreasing) currentParam2, but we still failed
                if (numCircles == 0 && priorNumCircles == 0) {
                    // We have no circles now, and we had no circles before.So we never found any.
                    // In this case, keep trying to broaden if we can, otherwise, we fail
                    if (currentParam2 <= min_param2) {
                        // We've loosened things as much as we want to, but still haven't identified a single ball
                        if (report_find_failures) {
                            GS_LOG_MSG(error, "Could not find any balls");
                        }
                        done = true;
                    }
                    else {
                        currentParam2 -= param2_increment;
                        currentlyLooseningSearch = true;
                        circles.assign(test_circles.begin(), test_circles.end());
                        done = false;
                    }
                }
                else if (((numCircles > 0 && numCircles < min_circles_to_return_from_hough) && priorNumCircles == 0) ||
                    (currentlyLooseningSearch == true)) {
                    // We found SOME circles, but not as many as we'd like, and we had no circles previously.
                    // So, continue to broaden the search parameters to try to get more if we can

                    // Loosen up our seach parameters to see if we can get some more circles
                    if (currentParam2 <= min_param2) {
                        // We've loosened things as much as we want to, but still haven't identified a single ball
                        GS_LOG_TRACE_MSG(trace, "Could not find as many balls as hoped");
                        // Save what we have now because it's as good as things are going to get
                        circles.assign(test_circles.begin(), test_circles.end());
                        finalNumberOfFoundCircles = numCircles;
                        done = true;
                    }
                    else {
                        currentParam2 -= param2_increment;
                        currentlyLooseningSearch = true;
                        // Save what we have now because maybe it's as good as things get
                        circles.assign(test_circles.begin(), test_circles.end());
                        done = false;
                    }
                }
                else if (numCircles == 0 && priorNumCircles > 0) {
                    // We had some circles previously, but we presumably went too far in terms of tightening and now we have none
                    // Return the prior set of balls(which was apparently more than 1)
                    GS_LOG_TRACE_MSG(trace, "Could only narrow down to " + std::to_string(numCircles) + " balls");
                    finalNumberOfFoundCircles = numCircles;
                    done = true;
                }
            }

            GS_LOG_TRACE_MSG(trace, "Found " + std::to_string(numCircles) + " circles.");
        }

    post_detection_processing:
        // Post-detection processing continues here for both HoughCircles and ONNX

        GS_LOG_MSG(trace, "Stating post_detection_processing.");

        cv::Mat candidates_image_ = rgbImg.clone();

        // Create a list of the circles with their corresponding criteria for quick sorting
        // Also draw detected circles if in debug mode

        // We may have to sort based on several criteria to find the best ball
        std::vector<CircleCandidateListElement>  foundCircleList;

        int MAX_CIRCLES_TO_EVALUATE = 200;
        bool expectedBallColorExists = false;

        const int kMaxCirclesToEmphasize = 10;


        int i = 0;
        if (finalNumberOfFoundCircles > 0) {

            i = 0;

            GsColorTriplet expectedBallRGBAverage;
            GsColorTriplet expectedBallRGBMedian;
            GsColorTriplet expectedBallRGBStd;


            if (baseBallWithSearchParams.average_color_ != GsColorTriplet(0, 0, 0)) {
                expectedBallRGBAverage = baseBallWithSearchParams.average_color_;
                expectedBallRGBMedian = baseBallWithSearchParams.median_color_;
                expectedBallRGBStd = baseBallWithSearchParams.std_color_;
                expectedBallColorExists = true;
            }
            else {
                // We don't have an expected ball color, so determine how close the candidate
                // is to the center of the masking color range
                expectedBallRGBAverage = baseBallWithSearchParams.GetRGBCenterFromHSVRange();
                expectedBallRGBMedian = expectedBallRGBAverage;  // We don't have anything better
                expectedBallRGBStd = (0, 0, 0);
                expectedBallColorExists = false;
            }

            GS_LOG_TRACE_MSG(trace, "Center of expected ball color (BGR){ " + LoggingTools::FormatGsColorTriplet(expectedBallRGBAverage));
            GS_LOG_TRACE_MSG(trace, "Expected ball median = " + LoggingTools::FormatGsColorTriplet(expectedBallRGBMedian) + " STD{ " + LoggingTools::FormatGsColorTriplet(expectedBallRGBStd));

            // Translate the circle coordinates back to the full image
            for (auto& c : circles) {
                c[0] += offset_sub_to_full.x;
                c[1] += offset_sub_to_full.y;
            }

            for (auto& c : circles) {

                i += 1;

                if (i > MAX_CIRCLES_TO_EVALUATE) {
                    break;
                }

                int found_radius = (int)std::round(c[2]);

                LoggingTools::DrawCircleOutlineAndCenter(candidates_image_, c, std::to_string(i), i, (i > kMaxCirclesToEmphasize));

                // Ignore any really small circles
                if (found_radius >= MIN_BALL_CANDIDATE_RADIUS) {

                    double calculated_color_difference = 0;
                    GsColorTriplet avg_RGB;
                    GsColorTriplet medianRGB;
                    GsColorTriplet stdRGB;

                    float rgb_avg_diff = 0.0;
                    float rgb_median_diff = 0.0;
                    float rgb_std_diff = 0.0;

                    // Putting currently uses ball colors to weed out balls that are formed from the noise of the putting green.
                    if (expectedBallColorExists || search_mode == kPutting) {
                        // Only deal with color if we will be comparing colors
                        std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(rgbImg, c);
                        avg_RGB = { stats[0] };
                        medianRGB = { stats[1] };
                        stdRGB = { stats[2] };

                        // Draw the outer circle if in debug
                        GS_LOG_TRACE_MSG(trace, "Circle of above-minimum radius " + std::to_string(MIN_BALL_CANDIDATE_RADIUS) +
                            " pixels. Average RGB is{ " + LoggingTools::FormatGsColorTriplet(avg_RGB)
                            + ". Average HSV is{ " + LoggingTools::FormatGsColorTriplet(CvUtils::ConvertRgbToHsv(avg_RGB)));

                        // Determine how "different" the average color is from the expected ball color
                        // If we don't have an expected ball color, than we use the RGB center from the  
                        // current mask
                        rgb_avg_diff = CvUtils::ColorDistance(avg_RGB, expectedBallRGBAverage);
                        rgb_median_diff = CvUtils::ColorDistance(medianRGB, expectedBallRGBMedian);   // TBD
                        rgb_std_diff = CvUtils::ColorDistance(stdRGB, expectedBallRGBStd);   // TBD

                        // Even if a potential ball has a really close median color, if the STD is even a little off, we want to down - grade it
                        // The following works to mix the three statistics together appropriately
                        // Will also penalize balls that are found toward the tail end of the list
                        //  NOMINAL - large StdDiff was throwing off? float calculated_color_difference = rgb_avg_diff + (float)(100. * pow(rgb_std_diff, 2.));
                        // TBD - this needs to be optimized.
    //                    double calculated_color_difference = pow(rgb_avg_diff,2) + (float)(2. * pow(rgb_std_diff, 2.)) + (float)(125. * pow(i, 4.));
                        // NOTE - if the flash-times are different for the ball we are using for the color, this is likely to pick the wrong thing.
                        calculated_color_difference = pow(rgb_avg_diff, 2) + (float)(20. * pow(rgb_std_diff, 2.)) + (float)(200. * pow(10 * i, 3.));

                        /* GS_LOG_TRACE_MSG(trace, "Found circle number " + std::to_string(i) + " radius = " + std::to_string(found_radius) +
                            " rgb_avg_diff = " + std::to_string(rgb_avg_diff) +
                            " CALCDiff = " + std::to_string(calculated_color_difference) + " rgbDiff = " + std::to_string(rgb_avg_diff) +
                            " rgb_median_diff = " + std::to_string(rgb_median_diff) + " rgb_std_diff = " + std::to_string(rgb_std_diff));
                        */
                    }

                    foundCircleList.push_back(CircleCandidateListElement{
                                "Ball " + std::to_string(i),
                                c,
                                calculated_color_difference,
                                found_radius,
                                avg_RGB,
                                rgb_avg_diff,
                                rgb_median_diff,
                                rgb_std_diff });
                }
                else {
                    GS_LOG_TRACE_MSG(trace, "Skipping too-small circle of radius = " + std::to_string(c[2]));
                }

            }

            LoggingTools::DebugShowImage(image_name_ + "  Hough-only-identified Circles{", candidates_image_);
        }
        else {
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not find any circles");
            }
            return false;
        }

        // Determine the average color of a rectangle within each circle, and see which is
        // closest to the color we were expecting(e.g., white)

        // GS_LOG_TRACE_MSG(trace, "Pre-sorted circle list{ " + FormatCircleCandidateList(foundCircleList));

        if (search_mode != BallSearchMode::kStrobed && expectedBallColorExists) {
            // Sort by the difference between the found ball's color and the expected oolor
            std::sort(foundCircleList.begin(), foundCircleList.end(), [](const CircleCandidateListElement& a, const CircleCandidateListElement& b)
                { return (a.calculated_color_difference < b.calculated_color_difference); });
        }
        else {
            // Do nothing if the color differences would be meaningless
        }

        GS_LOG_TRACE_MSG(trace, "Sorted circle list{     " + FormatCircleCandidateList(foundCircleList));

        // Only proceed if at least one circle was found
        // The hough transfer will have returned the "best" circle first(TBD - Confirm)
        // we will still do some post - processing to get rid of anything that looks unreasonable,
        // such as really small circles.

        bool foundCircle = (foundCircleList.size() > 0);


        if (!foundCircle) {
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not find any circles");
            }
            return false;
        }

        std::vector<CircleCandidateListElement>  candidates;
        std::vector<CircleCandidateListElement>  finalCandidates;

        if ((search_mode == BallSearchMode::kStrobed) && expectedBallColorExists) {
            // Remove any balls whose RGB difference is too great, and then re - sort based on radius and
            // return the biggest radius ball.
            struct CircleCandidateListElement& firstCircleElement = foundCircleList.front();
            float maxRGBDistance = (float)(firstCircleElement.calculated_color_difference + CANDIDATE_BALL_COLOR_TOLERANCE);

            for (auto& e : foundCircleList)
            {
                if (e.calculated_color_difference <= maxRGBDistance)
                {
                    candidates.push_back(e);

                }
            }
            GS_LOG_TRACE_MSG(trace, "Candidates after removing color mismatches{     " + FormatCircleCandidateList(candidates));

            // Sort by radius, largest first, and copy the list to the finalCandidates

            std::sort(candidates.begin(), candidates.end(), [](const CircleCandidateListElement& a, const CircleCandidateListElement& b)
                { return (a.found_radius > b.found_radius); });

            std::copy(std::begin(candidates), std::end(candidates), std::back_inserter(finalCandidates));
        }
        else {
            // If we didn't find a ball with the expected color, then the final candidates are just whatever the
            // interim candidates were
            std::copy(std::begin(foundCircleList), std::end(foundCircleList), std::back_inserter(finalCandidates));
        }

        if (finalCandidates.size() < 1) {
            foundCircle = false;
            if (report_find_failures) {
                GS_LOG_MSG(error, "Could not any final candidate ball circles.");
            }
            return false;
        }

        GsCircle bestCircle = finalCandidates.front().circle;
        if (CvUtils::CircleRadius(bestCircle) < .001) {
            GS_LOG_MSG(error, "BestCircle had 0 radius!");
            return false;
        }

        cv::Mat initial_ball_candidates_image_ = rgbImg.clone(); 
        
        int index = 0;
        for (CircleCandidateListElement& c : finalCandidates) {

            // We have one or more (possibly sketchy) initial ball candidates.  Create a ball and setup its color information
            // so that we can (if desired) use that information to further isolate the ball before we calculate the final
            // x, y, and radius information.  The color mask to get rid of stuff that is 'obviously' not the golf ball
            GolfBall b;

            // TBD - refactor so that the x & y are set from the circle for the ball instead of having to keep separate
            b.quality_ranking = index;  // Rankings start at 0
            b.set_circle(c.circle);
            return_balls.push_back(b);

            // Record the candidate graphically for later analysis
            LoggingTools::DrawCircleOutlineAndCenter(initial_ball_candidates_image_, c.circle, std::to_string(index), index, (index > kMaxCirclesToEmphasize));
            
            index++;
        }


#ifdef PERFORM_FINAL_TARGETTED_BALL_ID   // NOTE - This will currently return only a SINGLE ball, not all the candidates

        GsCircle finalCircle;

        if (!DetermineBestCircle(blurImg, bestCircle, choose_largest_final_ball, final_circle)) {
            GS_LOG_MSG(error, "Failed to DetermineBestCircle.");
            return false;
        }

#else // Not performing any additional, targetted ball ID

    GsCircle finalCircle = bestCircle;

#endif

        // Take the refined (hopefully more precise) circle for the "best" ball and assign that information to
        // update the ball.

        final_result_image_ = rgbImg.clone();
        LoggingTools::DrawCircleOutlineAndCenter(final_result_image_, finalCircle, "Ball");
        GS_LOG_MSG(trace, "Saved final_result_image_");

        // LoggingTools::DebugShowImage(image_name_ + "  Resulting Circle on image", final_result_image_);

        if (CvUtils::CircleRadius(finalCircle) < 0.001) {
            GS_LOG_MSG(error, "CvUtils::GetBallColorRgb called with circle of 0 radius.");
            return false;
        }

        // Setup the "best" (first) ball to return the found information within
        GolfBall& best_ball = return_balls[0];

        // TBD - Too easy to forget to set a parameter here - refactor
        best_ball.ball_circle_ = finalCircle;

#ifdef PERFORM_FINAL_TARGETTED_BALL_ID

#ifdef USE_ELLIPSES_FOR_FINAL_ID
        // If we are using ellipses, save the information for later analysis
        best_ball.ball_ellipse_ = largestEllipse;
#endif
#endif

        best_ball.set_circle(finalCircle);

        std::vector<GsColorTriplet> stats = CvUtils::GetBallColorRgb(rgbImg, finalCircle);
        best_ball.ball_color_ = GolfBall::BallColor::kCalibrated;
        best_ball.average_color_ = stats[0];  // Average RGB
        best_ball.radius_at_calibration_pixels_ = baseBallWithSearchParams.radius_at_calibration_pixels_;

        auto getball_end = std::chrono::high_resolution_clock::now();
        auto getball_duration = std::chrono::duration_cast<std::chrono::milliseconds>(getball_end - getball_start);
        GS_LOG_MSG(info, "GetBall (ball detection) completed in " + std::to_string(getball_duration.count()) + "ms");

        return true;
    }


    // DetermineBestCircle - Delegated to HoughDetector
    bool BallImageProc::DetermineBestCircle(const cv::Mat& input_gray_image,
                                            const GolfBall& reference_ball,
                                            bool choose_largest_final_ball,
                                            GsCircle& final_circle) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetermineBestCircle - Delegating to HoughDetector");
        return HoughDetector::DetermineBestCircle(input_gray_image, reference_ball,
                                                  choose_largest_final_ball, final_circle);
    }

    // --- Ellipse detection methods (delegated to EllipseDetector) ---

    cv::RotatedRect BallImageProc::FindBestEllipseFornaciari(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {
        return EllipseDetector::FindBestEllipseFornaciari(img, reference_ball_circle, mask_radius);
    }

    cv::RotatedRect BallImageProc::FindLargestEllipse(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {
        return EllipseDetector::FindLargestEllipse(img, reference_ball_circle, mask_radius);
    }

    // --- OLD IMPLEMENTATIONS BELOW (to be removed after validation) ---
    /*
    cv::RotatedRect BallImageProc::FindBestEllipseFornaciari_OLD(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {

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

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - Original (SUB) input image for final choices", processedImg);


        // TBD - worth it before Canny?
        // Try to remove the noise around the ball
        // TBD - This can be made better than it is.  Possibly more iterations, different kernel size

        cv::Mat kernel = (cv::Mat_<char>(3, 3) << 0, -1, 0,
            -1, 5, -1,
            0, -1, 0);

        /*** Sharpening hurt most images
        cv::filter2D(processedImg, processedImg, -1, kernel);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - sharpened image", processedImg);
        ***/

        cv::GaussianBlur(processedImg, processedImg, cv::Size(3, 3), 0);  // nominal was 11x11
        cv::erode(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        cv::dilate(processedImg, processedImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - blurred/eroded/dilated image", processedImg);


        bool edgeDetectionFailed = false;

#ifdef GS_USING_IMAGE_EQ
        // cv::equalizeHist(processedImg, processedImg);
        // LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse_fornaciari - equalized, processed final image", processedImg);
#endif

        // Parameters Settings (Sect. 4.2)
        int		iThLength = 16;   // nominal 16
        float	fThObb = 3.0f;
        float	fThPos = 1.0f;
        float	fTaoCenters = 0.05f;
        int 	iNs = 16;
        float	fMaxCenterDistance = sqrt(float(sz.width * sz.width + sz.height * sz.height)) * fTaoCenters;

        float	fThScoreScore = 0.72f;

        // Other constant parameters settings. 

        // Gaussian filter parameters, in pre-processing
        Size	szPreProcessingGaussKernelSize = Size(5, 5);    // Nominal is 5, 5
        double	dPreProcessingGaussSigma = 1.0;

        float	fDistanceToEllipseContour = 0.1f;	// (Sect. 3.3.1 - Validation)
        float	fMinReliability = 0.4f;	// Const parameters to discard bad ellipses


        // Initialize Detector with selected parameters
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


        // Detect
        vector<Ellipse> ellipses;
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

        // Look at as many ellipses as we need to in order to find the best (highest ranked) ellipse that is reasonable
        // given the ball that we are looking for
        for (auto& es : ellipses) {
            Ellipse ellipseStruct = es;
            RotatedRect e(Point(cvRound(es._xc), cvRound(es._yc)), Size(cvRound(2.0 * es._a), cvRound(2.0 * es._b)), (float)(es._rad * 180.0 / CV_PI));

            cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

            // Note - All ellipses will be in the coordinate system of the FULL image, not the sub-image

            // Translate the ellipse to the full image coordinates for comparison with the expected position of the ball
            e.center.x += offset_sub_to_full.x;
            e.center.y += offset_sub_to_full.y;

            float xc = e.center.x;
            float yc = e.center.y;
            float a = e.size.width;    // width >= height
            float b = e.size.height;
            float theta = e.angle;  // Deal with this?
            float area = a * b;
            float aspectRatio = std::max(a,b) / std::min(a, b);


            // Cull out unrealistic ellipses based on position and size
            // NOTE - there were too many non-upright ellipses
            // TBD - Need to retest everything with the new aspect ratio restriction
            if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
                (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
                area < pow(ballRadius, 2.0) ||
                area > 6 * pow(ballRadius, 2.0) ||
                (!CvUtils::IsUprightRect(theta) && false) ||
                aspectRatio > 1.15) {
                GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) + " aspectRatio = " + std::to_string(aspectRatio) + "(REJECTED)");
                GS_LOG_TRACE_MSG(trace, "     Expected max found ball radius was = " + std::to_string(ballRadius / 1.5) + ", min area: " + std::to_string(pow(ballRadius, 2.0)) + ", max area: " + std::to_string(5 * pow(ballRadius, 2.0)) + ", aspectRatio: " + std::to_string(aspectRatio) + ". (REJECTED)");

                // DEBUG - just for now show the rejected ellipses as well

                if (numDrawn++ > 5) {
                    GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
                }
                else {
                    ellipse(ellipseImg, e, color, 2);
                }
                numEllipses++;
            }
            else {
                GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b));

                if (numDrawn++ > 5) {
                    GS_LOG_TRACE_MSG(trace, "Too many ellipses to draw (skipping no. " + std::to_string(numDrawn) + ").");
                    break; // We are too far down the list in quality, so stop
                }
                else {
                    ellipse(ellipseImg, e, color, 2);
                }
                numEllipses++;

                if (area > largestArea) {
                    // Save this ellipse as our current best candidate
                    largestArea = area;
                    largestEllipse = e;
                    foundBestEllipse = true; 
                    // break;  // If we're only going to take the first (best) fit - TBD - this often breaks stuff!
                }
            }
        }

        LoggingTools::DebugShowImage("BallImageProc::FindBestEllipseFornaciari - Ellipses(" + std::to_string(numEllipses) + "):", ellipseImg);

        if (!foundBestEllipse) {
            LoggingTools::Warning("BallImageProc::FindBestEllipseFornaciari - Unable to find ellipse.");
            return largestEllipse;
        }

        return largestEllipse;
    }

    cv::RotatedRect BallImageProc::FindLargestEllipse_OLD(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius) {

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - input image for final choices", img);

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

        // Try to remove the noise around the ball
        // TBD - This can be made better than it is.  Possibly more iterations, different kernel size
        cv::erode(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);
        cv::dilate(finalChoiceSubImg, finalChoiceSubImg, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(7, 7)), cv::Point(-1, -1), 2);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - after erode/dilate of grayscale:", finalChoiceSubImg);

        while (!edgeDetectDone) {

            cv::Canny(finalChoiceSubImg, cannyOutput, lowThresh, highThresh);  // <-- These parameters are critical and touchy - TBD
            // Remove the contour lines that develop at the edge of the mask - they are just artifacts, not real, also
            // try to get rid of some of the noise that should be clearly outside the ball
            cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub, mask_radius, cv::Scalar{ 0, 0, 0 }, (int)((double)ballRadius / 12.0));
            // Also remove the inner part of the ball, at least to the extent we can safely write that area off
            cv::circle(cannyOutput, cv::Point(circleX, circleY) + offset_full_to_sub, (int)(ballRadius * 0.7), cv::Scalar{ 0, 0, 0 }, cv::FILLED);

            // LoggingTools::DebugShowImage(image_name_ + "  Current Canny SubImage:", cannyOutput);

            cv::meanStdDev(cannyOutput, meanArray, stdDevArray);

            double mean = meanArray.val[0];
            double stddev = stdDevArray.val[0];

            GS_LOG_TRACE_MSG(trace, "Ball circle finalization - Canny edges at tolerance (low,high)= " + std::to_string(lowThresh) + ", " + std::to_string(highThresh) + "): mean: " + std::to_string(mean) + "std : " + std::to_string(stddev));

            // Adjust to get more/less edge lines depending on how busy (percentage white) the image currently is
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

            // Safety net to make sure we never get in an infinite loop
            if (cannyIterations > 30) {
                edgeDetectDone = true;
                edgeDetectionFailed = true;
            }
        }

        if (edgeDetectionFailed) {
            LoggingTools::Warning("Failed to detect edges");
            cv::RotatedRect nullRect;
            return nullRect;
        }

        RemoveLinearNoise(cannyOutput);   // This has been problematic because it can rip up an otherwise good circle

        // LoggingTools::DebugShowImage(image_name_ + "  Canny:", cannyOutput);
        // Try to fill in any gaps in the best ellipse lines
        for (int dilations = 0; dilations < 2; dilations++) {
            cv::dilate(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
            cv::erode(cannyOutput, cannyOutput, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)), cv::Point(-1, -1), 2);
        }
        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Dilated/eroded Canny:", cannyOutput);



        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;
        cv::findContours(cannyOutput, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE, cv::Point(0, 0));


        cv::Mat contourImg = cv::Mat::zeros(img.size(), CV_8UC3);
        cv::Mat ellipseImg = cv::Mat::zeros(img.size(), CV_8UC3);
        cv::RNG rng(12345);
        std::vector<cv::RotatedRect> minEllipse(contours.size());
        int numEllipses = 0;

        cv::RotatedRect largestEllipse;
        double largestArea = 0;

        for (size_t i = 0; i < contours.size(); i++)
        {
            cv::Scalar color = cv::Scalar(rng.uniform(0, 256), rng.uniform(0, 256), rng.uniform(0, 256));

            // Note - All ellipses will be in the coordinate system of the FULL image, not the sub-image
            if (contours[i].size() > 25) {
                minEllipse[i] = fitEllipse(contours[i]);

                // Translate the ellipse to the full image coordinates for comparison with the expected position of the ball
                minEllipse[i].center.x += offset_sub_to_full.x;
                minEllipse[i].center.y += offset_sub_to_full.y;

                float xc = minEllipse[i].center.x;
                float yc = minEllipse[i].center.y;
                float a = minEllipse[i].size.width;    // width >= height
                float b = minEllipse[i].size.height;
                float theta = minEllipse[i].angle;  // Deal with this?
                float area = a * b;


                // Cull out unrealistic ellipses based on position and size
                // NOTE - there were too many non-upright ellipses
                if ((std::abs(xc - circleX) > (ballRadius / 1.5)) ||
                        (std::abs(yc - circleY) > (ballRadius / 1.5)) ||
                        area < pow(ballRadius, 2.0) ||
                        area > 5 * pow(ballRadius, 2.0) ||
                        (!CvUtils::IsUprightRect(theta) && false) )  {
                    GS_LOG_TRACE_MSG(trace, "Found and REJECTED ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b) + " theta = " + std::to_string(theta) + "(REJECTED)");

                    // DEBIG - just for now show the rejected ellipses as well
                    
                    ellipse(ellipseImg, minEllipse[i], color, 2);
                    numEllipses++;
                    drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);
                    
                }
                else {
                    GS_LOG_TRACE_MSG(trace, "Found ellipse, x,y = " + std::to_string(xc) + "," + std::to_string(yc) + " rw,rh = " + std::to_string(a) + "," + std::to_string(b) + " rectArea = " + std::to_string(a * b));

                    ellipse(ellipseImg, minEllipse[i], color, 2);
                    numEllipses++;
                    drawContours(contourImg, contours, (int)i, color, 2, cv::LINE_8, hierarchy, 0);

                    if (area > largestArea) {
                        // Save this ellipse as our current best candidate
                        largestArea = area;
                        largestEllipse = minEllipse[i];
                    }
                }
            }
        }

        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Contours:", contourImg);
        LoggingTools::DebugShowImage("BallImageProc::FindLargestEllipse - Ellipses(" + std::to_string(numEllipses) + "):", ellipseImg);

        return largestEllipse;
    }
    */
    // --- END OF OLD IMPLEMENTATIONS ---

    // RemoveLinearNoise has been moved to HoughDetector
    // Not working very well yet.  May want to try instead some closing/opening or convex hull model
    bool BallImageProc::RemoveLinearNoise(cv::Mat& img) {
        return HoughDetector::RemoveLinearNoise(img);
    }

    bool BallImageProc::RemoveLinearNoise_OLD(cv::Mat& img) {
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - before removing horizontal/vertical lines", img);



#ifndef USING_HORIZ_VERT_REMOVAL


#else
        // Get rid of strongly horizontal and vertical lines, given that the ball should not be affected much
        int minLineLength = std::max(2, img.cols / 25);
        cv::Mat horizontalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(minLineLength, 1));
        cv::Mat verticalKernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, minLineLength));
        // cv::morphologyEx(cannyOutput, cannyOutput, cv::MORPH_ERODE, horizontal_kernel, cv::Point(-1, -1), 2);
        // TBD - shouldn't have to XOR the images, should be able to remove the lines in-place?
        cv::Mat horizontalLinesImg = img.clone();
        cv::erode(img, horizontalLinesImg, horizontalKernel, cv::Point(-1, -1), 1);
        cv::Mat verticalLinesImg = img.clone();
        cv::erode(img, verticalLinesImg, verticalKernel, cv::Point(-1, -1), 1);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - horizontal lines to filter", horizontalLinesImg);
        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - vertical lines to filter", verticalLinesImg);
        cv::bitwise_xor(img, horizontalLinesImg, img);
        cv::bitwise_xor(img, verticalLinesImg, img);

        LoggingTools::DebugShowImage(" BallImageProc::FindLargestEllipse - after removing horizontal/vertical lines", img);
#endif
        return true;
    }

    // GetColorMaskImage (both overloads) and BallIsPresent have been
    // extracted to ball_detection/color_filter.cpp and ball_detection/roi_manager.cpp

    std::string BallImageProc::FormatCircleCandidateElement(const struct CircleCandidateListElement& e) {
        // std::locale::global(std::locale("es_CO.UTF-8"));   // Try to get comma for thousands separators - doesn't work?  TBD

        auto f = GS_FORMATLIB_FORMAT("[{: <7}: {: <18} cd={: <15.2f} fr={: <4d} av={: <10} ad={: <9.1f} md={: <9.1f}    sd={: <9.1f}]", 
            e.name,
            LoggingTools::FormatCircle(e.circle),
            e.calculated_color_difference,
            e.found_radius,
            LoggingTools::FormatGsColorTriplet(e.avg_RGB),
            e.rgb_avg_diff,
            e.rgb_median_diff,
            e.rgb_std_diff
        );
        return f;
    }

    std::string BallImageProc::FormatCircleCandidateList(const std::vector<struct CircleCandidateListElement>& candidates) {
        std::string s = "\nName     | Circle                     | Color Diff         |Radius| Avg RGB                    |rgb_avg_diff  |rgb_median_diff | rgb_std_diff\n";
        for (auto& c : candidates)
        {
            s += FormatCircleCandidateElement(c) + "\n";
        }
        return s;
    }

    void BallImageProc::RoundCircleData(std::vector<GsCircle>& circles) {
        for (auto& c : circles) {
            // TBD - original was causing a compile problem?  Maybe just use regular around? nc::around(circles, 0);
            c[0] = std::round(c[0]);
            c[1] = std::round(c[1]);
            c[2] = std::round(c[2]);
        }
    }

    // GetAreaOfInterest has been extracted to ball_detection/roi_manager.cpp

    // WaitForBallMovement has been extracted to ball_detection/roi_manager.cpp

    // Spin analysis functions have been extracted to ball_detection/spin_analyzer.{h,cpp}:
    // GetImageCharacteristics, RemoveReflections, ReduceReflections, IsolateBall,
    // MaskAreaOutsideBall, GetBallRotation, CompareCandidateAngleImages,
    // CompareRotationImage, CreateGaborKernel, ApplyGaborFilterToBall,
    // ApplyTestGaborFilter, ComputeCandidateAngleImages, GetRotatedImage,
    // Project2dImageTo3dBall, Unproject3dBallTo2dImage

    // RemoveSmallestConcentricCircles belongs to detection pipeline, kept here.

    // GetImageCharacteristics has been extracted to SpinAnalyzer

    // --- Hough detection methods (delegated to HoughDetector) ---

    bool BallImageProc::RemoveSmallestConcentricCircles(std::vector<GsCircle>& circles) {
        return HoughDetector::RemoveSmallestConcentricCircles(circles);
    }


    /**
     * Detection Algorithm Dispatcher
     * Routes detection to HoughCircles or ONNX based on kDetectionMethod configuration
     */
    bool BallImageProc::DetectBalls(const cv::Mat& preprocessed_img, BallSearchMode search_mode, 
                                   std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBalls - Method: " + kDetectionMethod);
        
        if (kDetectionMethod == "legacy") {
            return DetectBallsHoughCircles(preprocessed_img, search_mode, detected_circles);
        } else if (kDetectionMethod == "experimental" || kDetectionMethod == "experimental_sahi") {
            return DetectBallsONNX(preprocessed_img, search_mode, detected_circles);
        } else {
            GS_LOG_MSG(error, "Unknown detection method: " + kDetectionMethod + ". Falling back to legacy.");
            return DetectBallsHoughCircles(preprocessed_img, search_mode, detected_circles);
        }
    }

    /**
     * Legacy HoughCircles Detection (placeholder - will be extracted from existing GetBall method)
     */
    bool BallImageProc::DetectBallsHoughCircles(const cv::Mat& preprocessed_img, BallSearchMode search_mode, 
                                               std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsHoughCircles");
        
        // TODO: Extract existing HoughCircles detection logic from GetBall method
        // This will be implemented when we refactor GetBall to use the dispatcher
        GS_LOG_MSG(error, "HoughCircles detection not yet extracted to separate method");
        return false;
    }

    /**
     * ONNX/YOLO Detection Pipeline
     */
    std::vector<int> BallImageProc::SingleClassNMS(const std::vector<cv::Rect>& boxes,
                                                   const std::vector<float>& confidences,
                                                   float conf_threshold,
                                                   float nms_threshold) {
        
        std::vector<int> indices;
        
        std::vector<std::pair<float, int>> confidence_index_pairs;
        confidence_index_pairs.reserve(boxes.size());
        
        for (size_t i = 0; i < confidences.size(); ++i) {
            if (confidences[i] >= conf_threshold) {
                confidence_index_pairs.emplace_back(confidences[i], static_cast<int>(i));
            }
        }
        
        if (confidence_index_pairs.empty()) {
            return indices;
        }
        
        std::sort(confidence_index_pairs.begin(), confidence_index_pairs.end(),
                 [](const auto& a, const auto& b) { return a.first > b.first; });
        
        std::vector<bool> suppressed(confidence_index_pairs.size(), false);
        
        for (size_t i = 0; i < confidence_index_pairs.size(); ++i) {
            if (suppressed[i]) continue;
            
            int idx_i = confidence_index_pairs[i].second;
            indices.push_back(idx_i);
            const cv::Rect& box_i = boxes[idx_i];
            
            for (size_t j = i + 1; j < confidence_index_pairs.size(); ++j) {
                if (suppressed[j]) continue;
                
                int idx_j = confidence_index_pairs[j].second;
                const cv::Rect& box_j = boxes[idx_j];
                
                int x1 = std::max(box_i.x, box_j.x);
                int y1 = std::max(box_i.y, box_j.y);
                int x2 = std::min(box_i.x + box_i.width, box_j.x + box_j.width);
                int y2 = std::min(box_i.y + box_i.height, box_j.y + box_j.height);
                
                int intersection_width = std::max(0, x2 - x1);
                int intersection_height = std::max(0, y2 - y1);
                float intersection_area = static_cast<float>(intersection_width * intersection_height);
                
                float box_i_area = static_cast<float>(box_i.width * box_i.height);
                float box_j_area = static_cast<float>(box_j.width * box_j.height);
                float union_area = box_i_area + box_j_area - intersection_area;
                
                float iou = (union_area > 0) ? (intersection_area / union_area) : 0.0f;
                
                if (iou > nms_threshold) {
                    suppressed[j] = true;
                }
            }
        }
        
        GS_LOG_TRACE_MSG(trace, "SingleClassNMS: " + std::to_string(boxes.size()) + 
                        " boxes -> " + std::to_string(indices.size()) + " after NMS");
        
        return indices;
    }
    
    bool BallImageProc::PreloadYOLOModel() {
        if (yolo_model_loaded_) {
            GS_LOG_MSG(trace, "YOLO model already loaded, skipping preload");
            return true;
        }

        try {
            std::lock_guard<std::mutex> lock(yolo_model_mutex_);

            if (yolo_model_loaded_) {
                GS_LOG_MSG(trace, "YOLO model already loaded by another thread");
                return true;
            }

            GS_LOG_MSG(info, "Preloading YOLO model at startup for detection method: " + kDetectionMethod);
            GS_LOG_MSG(trace, "Loading YOLO model from: " + kONNXModelPath);
            auto start_time = std::chrono::high_resolution_clock::now();
            
            yolo_model_ = cv::dnn::readNetFromONNX(kONNXModelPath);
            if (yolo_model_.empty()) {
                GS_LOG_MSG(error, "Failed to preload ONNX model: " + kONNXModelPath);
                return false;
            }
            
            if (kONNXDeviceType == "CPU") {
                yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            } else {
                yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
            }
            
            yolo_letterbox_buffer_ = cv::Mat(kONNXInputSize, kONNXInputSize, CV_8UC3);
            yolo_detection_boxes_.reserve(10);  // Max 10 golf balls
            yolo_detection_confidences_.reserve(10);
            yolo_outputs_.reserve(3);  // Network typically has 1-3 output layers
            
            yolo_model_loaded_ = true;
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            GS_LOG_MSG(trace, "YOLO model preloaded successfully in " + 
                            std::to_string(duration.count()) + "ms. First detection will be fast!");
            
            return true;
        } catch (const cv::Exception& e) {
            GS_LOG_MSG(error, "OpenCV exception during YOLO model preload: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Exception during YOLO model preload: " + std::string(e.what()));
            return false;
        } catch (...) {
            GS_LOG_MSG(error, "Unknown exception during YOLO model preload");
            return false;
        }
    }

    bool BallImageProc::DetectBallsONNX(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                       std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsONNX - Dispatching to backend: " + kONNXBackend);

        // Dual-Backend Dispatcher: Try ONNX Runtime first, fallback to OpenCV DNN if needed
        if (kONNXBackend == "onnxruntime") {
            if (DetectBallsONNXRuntime(preprocessed_img, search_mode, detected_circles)) {
                return true;
            } else if (kONNXRuntimeAutoFallback) {
                GS_LOG_MSG(warning, "ONNX Runtime detection failed, falling back to OpenCV DNN");
                return DetectBallsOpenCVDNN(preprocessed_img, search_mode, detected_circles);
            } else {
                return false;
            }
        } else {
            // Use OpenCV DNN backend directly
            return DetectBallsOpenCVDNN(preprocessed_img, search_mode, detected_circles);
        }
    }

    bool BallImageProc::DetectBallsONNXRuntime(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                              std::vector<GsCircle>& detected_circles) {
        auto detection_start = std::chrono::high_resolution_clock::now();

        try {
            // Initialize detector only once with double-checked locking pattern (optimized for Pi)
            if (!onnx_detector_initialized_.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
                if (!onnx_detector_initialized_.load(std::memory_order_relaxed)) {

                    // Configure detector with static configuration
                    ONNXRuntimeDetector::Config config;
                    config.model_path = kONNXModelPath;
                    config.confidence_threshold = kONNXConfidenceThreshold;
                    config.nms_threshold = kONNXNMSThreshold;
                    config.input_width = kONNXInputSize;
                    config.input_height = kONNXInputSize;
                    config.num_threads = kONNXRuntimeThreads;

                    // Pi-optimized settings
                    config.use_arm_compute_library = true;
                    config.use_thread_affinity = true;
                    config.use_memory_pool = true;
                    config.use_neon_preprocessing = true;
                    config.use_zero_copy = true;

                    GS_LOG_MSG(info, "Attempting to initialize ONNX Runtime detector with model: " + config.model_path);
                    onnx_detector_ = std::make_unique<ONNXRuntimeDetector>(config);

                    if (!onnx_detector_->Initialize()) {
                        GS_LOG_MSG(error, "Failed to initialize ONNX Runtime detector with model: " + config.model_path);
                        onnx_detector_.reset();  // Clean up failed detector
                        return false;
                    }

                    onnx_detector_initialized_.store(true, std::memory_order_release);
                    GS_LOG_MSG(info, "ONNX Runtime detector initialized successfully");
                }
            }

            // Convert to RGB if needed (minimal overhead)
            cv::Mat input_image;
            if (preprocessed_img.channels() == 1) {
                cv::cvtColor(preprocessed_img, input_image, cv::COLOR_GRAY2RGB);
            } else {
                input_image = preprocessed_img;  // Use directly (no copy)
            }

            // Handle SAHI slicing if enabled
            if (kDetectionMethod == "experimental_sahi") {
                std::vector<cv::Mat> slices;
                slices.reserve(16);  // Pre-allocate for typical slice count

                const int overlap = static_cast<int>(kSAHISliceWidth * kSAHIOverlapRatio);
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        cv::Rect slice_rect(x, y,
                                           std::min(kSAHISliceWidth, input_image.cols - x),
                                           std::min(kSAHISliceHeight, input_image.rows - y));
                        slices.push_back(input_image(slice_rect));
                    }
                }

                // Process all slices in batch for efficiency
                std::vector<std::vector<ONNXRuntimeDetector::Detection>> batch_detections =
                    onnx_detector_->DetectBatch(slices);

                // Convert and merge all detections
                detected_circles.clear();
                detected_circles.reserve(batch_detections.size() * 2);  // Estimate

                size_t slice_idx = 0;
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        if (slice_idx < batch_detections.size()) {
                            for (const auto& detection : batch_detections[slice_idx]) {
                                GsCircle circle;
                                circle[0] = detection.bbox.x + detection.bbox.width * 0.5f + x;   // center_x
                                circle[1] = detection.bbox.y + detection.bbox.height * 0.5f + y;  // center_y
                                circle[2] = std::max(detection.bbox.width, detection.bbox.height) * 0.5f;  // radius
                                detected_circles.push_back(circle);
                            }
                        }
                        ++slice_idx;
                    }
                }
            } else {
                // Single image detection (fastest path)
                std::vector<ONNXRuntimeDetector::Detection> detections = onnx_detector_->Detect(input_image);

                // Convert ONNXRuntimeDetector::Detection to GsCircle format
                detected_circles.clear();
                detected_circles.reserve(detections.size());

                for (const auto& detection : detections) {
                    GsCircle circle;
                    circle[0] = detection.bbox.x + detection.bbox.width * 0.5f;   // center_x
                    circle[1] = detection.bbox.y + detection.bbox.height * 0.5f;  // center_y
                    circle[2] = std::max(detection.bbox.width, detection.bbox.height) * 0.5f;  // radius
                    detected_circles.push_back(circle);
                }
            }

            auto detection_end = std::chrono::high_resolution_clock::now();
            auto detection_duration = std::chrono::duration_cast<std::chrono::milliseconds>(detection_end - detection_start);

            GS_LOG_TRACE_MSG(trace, "ONNX Runtime detected " + std::to_string(detected_circles.size()) +
                           " balls in " + std::to_string(detection_duration.count()) + "ms");
            return !detected_circles.empty();

        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "ONNX Runtime detection failed: " + std::string(e.what()));
            return false;
        }
    }


    bool BallImageProc::DetectBallsOpenCVDNN(const cv::Mat& preprocessed_img, BallSearchMode search_mode,
                                            std::vector<GsCircle>& detected_circles) {
        GS_LOG_TRACE_MSG(trace, "BallImageProc::DetectBallsOpenCVDNN - Fallback backend");

        try {
            {
                std::lock_guard<std::mutex> lock(yolo_model_mutex_);
                if (!yolo_model_loaded_) {
                    GS_LOG_MSG(trace, "Loading YOLO model for OpenCV DNN backend...");
                    auto start_time = std::chrono::high_resolution_clock::now();

                    yolo_model_ = cv::dnn::readNetFromONNX(kONNXModelPath);
                    if (yolo_model_.empty()) {
                        GS_LOG_MSG(error, "Failed to load ONNX model for OpenCV DNN: " + kONNXModelPath);
                        return false;
                    }

                    // Set backend and target
                    if (kONNXDeviceType == "CPU") {
                        yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                        yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    } else {
                        yolo_model_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                        yolo_model_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    }

                    yolo_letterbox_buffer_ = cv::Mat(kONNXInputSize, kONNXInputSize, CV_8UC3);
                    yolo_detection_boxes_.reserve(50);
                    yolo_detection_confidences_.reserve(50);
                    yolo_outputs_.reserve(3);

                    yolo_model_loaded_ = true;

                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
                    GS_LOG_MSG(info, "OpenCV DNN model loaded successfully in " +
                                   std::to_string(duration.count()) + "ms (fallback backend)");
                }
            }

            auto processing_start_time = std::chrono::high_resolution_clock::now();
            GS_LOG_MSG(trace, "OpenCV DNN processing started.");

            cv::Mat input_image;
            if (preprocessed_img.channels() == 1) {
                cv::cvtColor(preprocessed_img, input_image, cv::COLOR_GRAY2RGB);
            } else if (preprocessed_img.channels() == 3) {
                input_image = preprocessed_img;
            } else {
                GS_LOG_MSG(error, "Unsupported number of channels: " + std::to_string(preprocessed_img.channels()));
                return false;
            }

            // SAHI slicing
            bool use_sahi = (kDetectionMethod == "experimental_sahi");
            std::vector<cv::Rect> slices;

            if (use_sahi) {
                int overlap = static_cast<int>(kSAHISliceWidth * kSAHIOverlapRatio);
                for (int y = 0; y < input_image.rows; y += kSAHISliceHeight - overlap) {
                    for (int x = 0; x < input_image.cols; x += kSAHISliceWidth - overlap) {
                        cv::Rect slice(x, y,
                                      std::min(kSAHISliceWidth, input_image.cols - x),
                                      std::min(kSAHISliceHeight, input_image.rows - y));
                        slices.push_back(slice);
                    }
                }
                GS_LOG_TRACE_MSG(trace, "OpenCV DNN SAHI: Created " + std::to_string(slices.size()) + " slices");
            } else {
                slices.push_back(cv::Rect(0, 0, input_image.cols, input_image.rows));
            }

            yolo_detection_boxes_.clear();
            yolo_detection_confidences_.clear();

            for (const auto& slice : slices) {
                cv::Mat slice_img = input_image(slice);

                // Create letterboxed input
                float scale = std::min(float(kONNXInputSize) / slice_img.cols,
                                     float(kONNXInputSize) / slice_img.rows);
                int new_width = int(slice_img.cols * scale);
                int new_height = int(slice_img.rows * scale);

                if (yolo_resized_buffer_.size() != cv::Size(new_width, new_height) || yolo_resized_buffer_.type() != CV_8UC3) {
                    yolo_resized_buffer_ = cv::Mat(new_height, new_width, CV_8UC3);
                }
                cv::resize(slice_img, yolo_resized_buffer_, cv::Size(new_width, new_height));

                // Create letterbox with gray padding
                yolo_letterbox_buffer_.setTo(cv::Scalar(114, 114, 114));
                int x_offset = (kONNXInputSize - new_width) / 2;
                int y_offset = (kONNXInputSize - new_height) / 2;
                yolo_resized_buffer_.copyTo(yolo_letterbox_buffer_(cv::Rect(x_offset, y_offset, new_width, new_height)));

                // Create blob
                cv::dnn::blobFromImage(yolo_letterbox_buffer_, yolo_blob_buffer_, 1.0/255.0,
                                      cv::Size(kONNXInputSize, kONNXInputSize),
                                      cv::Scalar(), false, false);  // swapRB=false for YOLOv8 BGR input

                // Run inference
                yolo_model_.setInput(yolo_blob_buffer_);
                yolo_outputs_.clear();
                yolo_model_.forward(yolo_outputs_, yolo_model_.getUnconnectedOutLayersNames());

                // Parse output 
                if (!yolo_outputs_.empty()) {
                    cv::Mat output = yolo_outputs_[0];

                    // Reshape and transpose YOLOv8 output to [detections, features] format
                    if (output.dims == 3 && output.size[0] == 1) {
                        output = output.reshape(1, output.size[1]); // [5, num_detections]
                        cv::transpose(output, output);              // [num_detections, 5]
                    }

                    float* data = (float*)output.data;
                    int num_detections = output.rows;

                    for (int i = 0; i < num_detections; ++i) {
                        float* detection = data + i * output.cols;

                        float cx_letterbox = detection[0];
                        float cy_letterbox = detection[1];
                        float w_letterbox = detection[2];
                        float h_letterbox = detection[3];
                        float confidence = detection[4];

                        if (confidence >= kONNXConfidenceThreshold) {
                            // Convert from letterbox coordinates back to slice coordinates
                            float cx_slice = (cx_letterbox - x_offset) / scale;
                            float cy_slice = (cy_letterbox - y_offset) / scale;
                            float w_slice = w_letterbox / scale;
                            float h_slice = h_letterbox / scale;

                            // Convert center format to top-left format
                            int x = static_cast<int>(cx_slice - w_slice/2) + slice.x;
                            int y = static_cast<int>(cy_slice - h_slice/2) + slice.y;
                            int w = static_cast<int>(w_slice);
                            int h = static_cast<int>(h_slice);

                            // Bounds checking
                            if (w > 0 && h > 0 && x >= 0 && y >= 0 &&
                                x + w <= input_image.cols && y + h <= input_image.rows) {
                                yolo_detection_boxes_.push_back(cv::Rect(x, y, w, h));
                                yolo_detection_confidences_.push_back(confidence);
                            }
                        }
                    }
                }
            }

            // Apply NMS and convert to circles
            std::vector<int> indices = SingleClassNMS(yolo_detection_boxes_, yolo_detection_confidences_,
                                                      kONNXConfidenceThreshold, kONNXNMSThreshold);

            detected_circles.clear();
            detected_circles.reserve(indices.size());
            for (int idx : indices) {
                const cv::Rect& box = yolo_detection_boxes_[idx];
                GsCircle circle;
                circle[0] = (float)(box.x + (int)(std::round(box.width / 2.0)));
                circle[1] = (float)(box.y + (int)(std::round(box.height / 2.0)));
                circle[2] = (float)(std::max(box.width, (int)(std::round(box.height) / 2.0)));
                detected_circles.push_back(circle);
            }

            auto processing_end_time = std::chrono::high_resolution_clock::now();
            auto processing_duration = std::chrono::duration_cast<std::chrono::milliseconds>(processing_end_time - processing_start_time);
            GS_LOG_MSG(trace, "OpenCV DNN completed processing in " + std::to_string(processing_duration.count()) + " ms (fallback)");

            GS_LOG_TRACE_MSG(trace, "OpenCV DNN detected " + std::to_string(detected_circles.size()) + " balls after NMS");
            return !detected_circles.empty();

        } catch (const cv::Exception& e) {
            GS_LOG_MSG(error, "OpenCV DNN detection failed: " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "OpenCV DNN fallback detection failed: " + std::string(e.what()));
            return false;
        }
    }

    bool BallImageProc::PreloadONNXRuntimeModel() {
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(trace, "ONNX Runtime detector already preloaded, skipping");
            return true;
        }

        std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(trace, "ONNX Runtime detector already preloaded by another thread");
            return true;
        }

        GS_LOG_MSG(info, "Preloading ONNX Runtime detector for ARM64 optimization...");

        try {
            auto start_time = std::chrono::high_resolution_clock::now();

            // Configure detector with static configuration
            ONNXRuntimeDetector::Config config;
            config.model_path = kONNXModelPath;
            config.confidence_threshold = kONNXConfidenceThreshold;
            config.nms_threshold = kONNXNMSThreshold;
            config.input_width = kONNXInputSize;
            config.input_height = kONNXInputSize;
            config.num_threads = kONNXRuntimeThreads;

            // Pi-optimized settings
            config.use_arm_compute_library = true;
            config.use_thread_affinity = true;
            config.use_memory_pool = true;
            config.use_neon_preprocessing = true;
            config.use_zero_copy = true;

            onnx_detector_ = std::make_unique<ONNXRuntimeDetector>(config);

            if (!onnx_detector_->Initialize()) {
                GS_LOG_MSG(error, "Failed to initialize ONNX Runtime detector");
                return false;
            }

            onnx_detector_initialized_.store(true, std::memory_order_release);

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            GS_LOG_MSG(info, "ONNX Runtime detector preloaded successfully in " +
                           std::to_string(duration.count()) + "ms with " +
                           std::to_string(kONNXRuntimeThreads) + " threads (ARM64 optimized)");
            return true;

        } catch (const std::exception& e) {
            GS_LOG_MSG(error, "Failed to preload ONNX Runtime detector: " + std::string(e.what()));
            return false;
        }
    }

    void BallImageProc::CleanupONNXRuntime() {
        std::lock_guard<std::mutex> lock(onnx_detector_mutex_);
        if (onnx_detector_initialized_.load(std::memory_order_relaxed)) {
            GS_LOG_MSG(info, "Cleaning up ONNX Runtime detector...");

            onnx_detector_.reset();
            onnx_detector_initialized_.store(false, std::memory_order_release);

            GS_LOG_MSG(info, "ONNX Runtime detector cleanup completed");
        }
    }

    void BallImageProc::LoadConfigurationValues() {
        // This function should be called AFTER GolfSimConfiguration::Initialize() has loaded the JSON config
        // It reads the ONNX configuration values FROM the JSON and updates the static variables

        GS_LOG_MSG(info, "Loading BallImageProc configuration values from JSON...");

        // Read ONNX/AI Detection configuration values
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXModelPath", kONNXModelPath);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kDetectionMethod", kDetectionMethod);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kBallPlacementDetectionMethod", kBallPlacementDetectionMethod);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXConfidenceThreshold", kONNXConfidenceThreshold);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXNMSThreshold", kONNXNMSThreshold);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXInputSize", kONNXInputSize);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXBackend", kONNXBackend);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXRuntimeAutoFallback", kONNXRuntimeAutoFallback);
        GolfSimConfiguration::SetConstant("gs_config.ball_identification.kONNXRuntimeThreads", kONNXRuntimeThreads);

        // Resolve relative ONNX model path against PITRAC_ROOT
        if (!kONNXModelPath.empty() && kONNXModelPath[0] != '/') {
            std::string root = GolfSimConfiguration::GetPiTracRootPath();
            if (!root.empty()) {
                kONNXModelPath = root + "/" + kONNXModelPath;
            }
        }

        GS_LOG_MSG(info, "Loaded ONNX Model Path: " + kONNXModelPath);
        GS_LOG_MSG(info, "Loaded Detection Method: " + kDetectionMethod);
        GS_LOG_MSG(info, "Loaded Backend: " + kONNXBackend);

        if (!kONNXModelPath.empty()) {
            std::ifstream model_file(kONNXModelPath);
            if (model_file.good()) {
                GS_LOG_MSG(info, "ONNX model file verified to exist at: " + kONNXModelPath);
                model_file.close();
            } else {
                GS_LOG_MSG(error, "ONNX model file NOT FOUND at: " + kONNXModelPath);
            }
        }
    }

}
