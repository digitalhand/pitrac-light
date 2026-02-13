/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Performs image processing such as finding a ball in a picture.
// TBD - The separation of responsibilities with the golf_sim_camera needs to be clarified.

#pragma once


#include <iostream>
#include <filesystem>
#include <mutex>
#include <atomic>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/dnn.hpp>

#ifdef __unix__
#include <onnxruntime/core/session/onnxruntime_cxx_api.h>
#else
#include <onnxruntime_cxx_api.h>
#endif

#include "utils/logging_tools.h"
#include "gs_camera.h"
#include "colorsys.h"
#include "golf_ball.h"
#include "onnx_runtime_detector.hpp"
#include "ball_detection/spin_analyzer.h"
#include "ball_detection/color_filter.h"
#include "ball_detection/roi_manager.h"
#include "ball_detection/hough_detector.h"
#include "ball_detection/ellipse_detector.h"
#include "ball_detection/search_strategy.h"
#include "ball_detection/ball_detector_facade.h"


namespace golf_sim {

class BallImageProc
{
public:

    // The following are constants that control how the ball (circle) identification works.
    // They are set from the configuration .json file.
    // Note: Spin analysis constants have been moved to SpinAnalyzer class.

    static double kPlacedBallCannyLower;
    static double kPlacedBallCannyUpper;
    static double kPlacedBallStartingParam2;
    static double kPlacedBallMinParam2;
    static double kPlacedBallMaxParam2;
    static double kPlacedBallCurrentParam1;
    static double kPlacedBallParam2Increment;
    static int kPlacedMinHoughReturnCircles;
    static int kPlacedMaxHoughReturnCircles;
    static int kPlacedPreHoughBlurSize;
    static int kPlacedPreCannyBlurSize;

    static double kStrobedBallsCannyLower;
    static double kStrobedBallsCannyUpper;
    static int kStrobedBallsPreCannyBlurSize;
    static int kStrobedBallsPreHoughBlurSize;

    static double kStrobedBallsStartingParam2;
    static double kStrobedBallsMinParam2;
    static double kStrobedBallsMaxParam2;
    static double kStrobedBallsCurrentParam1;
    static double kStrobedBallsParam2Increment;
    static int kStrobedBallsMinHoughReturnCircles;
    static int kStrobedBallsMaxHoughReturnCircles;
    static int kPuttingPreHoughBlurSize;

    static bool kStrobedBallsUseAltHoughAlgorithm;
    static double kStrobedBallsAltCannyLower;
    static double kStrobedBallsAltCannyUpper;
    static int kStrobedBallsAltPreCannyBlurSize;
    static int kStrobedBallsAltPreHoughBlurSize;
    static double kStrobedBallsAltStartingParam2;
    static double kStrobedBallsAltMinParam2;
    static double kStrobedBallsAltMaxParam2;
    static double kStrobedBallsAltCurrentParam1;
    static double kStrobedBallsAltHoughDpParam1;
    static double kStrobedBallsAltParam2Increment;

    static bool kUseCLAHEProcessing;
    static int kCLAHEClipLimit;
    static int kCLAHETilesGridSize;

    static double kPuttingBallStartingParam2;
    static double kPuttingBallMinParam2;
    static double kPuttingBallMaxParam2;
    static double kPuttingBallCurrentParam1;
    static double kPuttingBallParam2Increment;
    static int kPuttingMinHoughReturnCircles;
    static int kPuttingMaxHoughReturnCircles;
    static double kPuttingHoughDpParam1;

    // TBD - Some of these are redundant - put 'em all in ball_image_proc or in gs_camera, but not both
    static double kExternallyStrobedEnvCannyLower;
    static double kExternallyStrobedEnvCannyUpper;
    static double kExternallyStrobedEnvCurrentParam1;
    static double kExternallyStrobedEnvMinParam2;
    static double kExternallyStrobedEnvMaxParam2;
    static double kExternallyStrobedEnvStartingParam2;
    static double kExternallyStrobedEnvNarrowingParam2;
    static double kExternallyStrobedEnvNarrowingDpParam;

    static double kExternallyStrobedEnvParam2Increment;
    static int kExternallyStrobedEnvMinHoughReturnCircles;
    static int kExternallyStrobedEnvMaxHoughReturnCircles;
    static int kExternallyStrobedEnvPreHoughBlurSize;
    static int kExternallyStrobedEnvPreCannyBlurSize;

    static double kExternallyStrobedEnvHoughDpParam1;
    static int kExternallyStrobedEnvMinimumSearchRadius;
    static int kExternallyStrobedEnvMaximumSearchRadius;
    static double kStrobedNarrowingRadiiDpParam;
    static double kStrobedNarrowingRadiiParam2;
    static int kExternallyStrobedEnvNarrowingPreCannyBlurSize;
    static int kExternallyStrobedEnvNarrowingPreHoughBlurSize;

    static bool kExternallyStrobedUseCLAHEProcessing;
    static int kExternallyStrobedCLAHEClipLimit;
    static int kExternallyStrobedCLAHETilesGridSize;

    static bool kUseDynamicRadiiAdjustment;
    static int kNumberRadiiToAverageForDynamicAdjustment;
    static double kStrobedNarrowingRadiiMinRatio;
    static double kStrobedNarrowingRadiiMaxRatio;

    static double kPlacedNarrowingRadiiMinRatio;
    static double kPlacedNarrowingRadiiMaxRatio;
    static double kPlacedNarrowingStartingParam2;
    static double kPlacedNarrowingRadiiDpParam;
    static double kPlacedNarrowingParam1;


    // kLogIntermediateSpinImagesToFile moved to SpinAnalyzer
    static double kPlacedBallHoughDpParam1;
    static double kStrobedBallsHoughDpParam1;
    static bool kUseBestCircleRefinement;
    static bool kUseBestCircleLargestCircle;

    static double kBestCircleCannyLower;
    static double kBestCircleCannyUpper;
    static int kBestCirclePreCannyBlurSize;
    static int kBestCirclePreHoughBlurSize;
    static double kBestCircleParam1;
    static double kBestCircleParam2;
    static double kBestCircleHoughDpParam1;

    static double kExternallyStrobedBestCircleCannyLower;
    static double kExternallyStrobedBestCircleCannyUpper;
    static int kExternallyStrobedBestCirclePreCannyBlurSize;
    static int kExternallyStrobedBestCirclePreHoughBlurSize;
    static double kExternallyStrobedBestCircleParam1;
    static double kExternallyStrobedBestCircleParam2;
    static double kExternallyStrobedBestCircleHoughDpParam1;


    // TBD - Identifying the "best" circle after doing a rough circle identification
    // may be going away.
    static double kBestCircleIdentificationMinRadiusRatio;
    static double kBestCircleIdentificationMaxRadiusRatio;

    // Gabor filter constants moved to SpinAnalyzer

    // ONNX Detection Configuration
    static std::string kDetectionMethod;
    static std::string kBallPlacementDetectionMethod;
    static std::string kONNXModelPath;
    static float kONNXConfidenceThreshold;
    static float kONNXNMSThreshold;
    static int kONNXInputSize;
    static int kSAHISliceHeight;
    static int kSAHISliceWidth;
    static float kSAHIOverlapRatio;
    static std::string kONNXDeviceType;

    static std::string kONNXBackend;  // "onnxruntime" (primary) or "opencv_dnn" (fallback)
    static bool kONNXRuntimeAutoFallback;  // Enable automatic fallback to OpenCV DNN
    static int kONNXRuntimeThreads;  // Number of threads for ONNX Runtime (ARM optimization)

    // RotationSearchSpace is now defined in ball_detection/spin_analyzer.h

    // The image in which to try to identify a golf ball - set prior to calling
    // the identification methods
    cv::Mat img_;

    // The ball image processing works in the context of a golf ball
    GolfBall ball_;

    // Any radius less than 0.0 means it is currently unknown
    // If set, searches for balls will be limited to this radius range
    int min_ball_radius_ = -1;
    int max_ball_radius_ = -1;

    // This will be used in any debug windows to identify the image
    std::string image_name_;

    // These will be returned for potential debugging
    // Color-based masking was an early technique that we're moving away from
    cv::Mat color_mask_image_;

    // The location mask is a total(black or white) mask to subset the image down to just
    // the area(s) that we are interested in
    cv::Mat area_mask_image_;

    // Shows the points of the image that were considered as possibly being the golf ball
    cv::Mat candidates_image_;
    
    // Shows the ball that was identified with a circle and center point on top of original image
    cv::Mat final_result_image_;

    BallImageProc();
    ~BallImageProc();


    static BallImageProc* get_ball_image_processor();

    enum BallSearchMode {
        kUnknown = 0,
        kFindPlacedBall = 1,
        kStrobed = 2,
        kExternallyStrobed = 3,
        kPutting = 4
    };

    // Find a golf ball in the picture - this is the main workhorse of the system.
    // if the baseBallWithSearchParams has color information, that information 
    // will be used to search for the ball in the picture
    // The returned balls will have at least the following ball information accurately set:
    //      circle
    // If chooseLargestFinalBall is set true, then even a poorer-matching final ball candidate will be chosen over a smaller, better-scored candidate.
    // If expectingBall is true, then the system will not be as picky when trying to find a ball.  Otherwise, if false (when the system does not
    // know if a ball will be present), the system will require a more perfect ball in order to reduce false positives.
    bool GetBall(  const cv::Mat& img, 
                   const GolfBall& baseBallWithSearchParams, 
                   std::vector<GolfBall> &return_balls, 
                   cv::Rect& expectedBallArea, 
                   BallSearchMode search_mode,
                   bool chooseLargestFinalBall=false,
                   bool report_find_failures =true );

    // --- ROI/movement methods (delegated to ROIManager) ---

    bool BallIsPresent(const cv::Mat& img) {
        return ROIManager::BallIsPresent(img);
    }

    // Performs some iterative refinement to try to identify the best ball circle.
    static bool DetermineBestCircle(const cv::Mat& gray_image,
                                    const GolfBall& reference_ball,
                                    bool choose_largest_final_ball,
                                    GsCircle& final_circle);

    static bool WaitForBallMovement(GolfSimCamera& c, cv::Mat& firstMovementImage, const GolfBall& ball, const long waitTimeSecs) {
        return ROIManager::WaitForBallMovement(c, firstMovementImage, ball, waitTimeSecs);
    }

    // --- Spin analysis methods (delegated to SpinAnalyzer) ---

    // Inputs are two balls and the images within which those balls exist
    // Returns the estimated amount of rotation in x, y, and z axes in degrees
    static cv::Vec3d GetBallRotation(const cv::Mat& full_gray_image1,
                                    const GolfBall& ball1,
                                    const cv::Mat& full_gray_image2,
                                    const GolfBall& ball2) {
        return SpinAnalyzer::GetBallRotation(full_gray_image1, ball1, full_gray_image2, ball2);
    }

    static bool ComputeCandidateAngleImages(const cv::Mat& base_dimple_image,
                                    const RotationSearchSpace& search_space,
                                    cv::Mat& output_candidate_mat,
                                    cv::Vec3i& output_candidate_elements_mat_size,
                                    std::vector< RotationCandidate>& output_candidates,
                                    const GolfBall& ball) {
        return SpinAnalyzer::ComputeCandidateAngleImages(base_dimple_image, search_space, output_candidate_mat, output_candidate_elements_mat_size, output_candidates, ball);
    }

    // Returns the index within candidates that has the best comparison.
    // Returns -1 on failure.
    static int CompareCandidateAngleImages(const cv::Mat* target_image,
                                            const cv::Mat* candidate_elements_mat,
                                            const cv::Vec3i* candidate_elements_mat_size,
                                            std::vector<RotationCandidate>* candidates,
                                            std::vector<std::string>& comparison_csv_data) {
        return SpinAnalyzer::CompareCandidateAngleImages(target_image, candidate_elements_mat, candidate_elements_mat_size, candidates, comparison_csv_data);
    }

    static cv::Vec2i CompareRotationImage(const cv::Mat& img1, const cv::Mat& img2, const int index = 0) {
        return SpinAnalyzer::CompareRotationImage(img1, img2, index);
    }

    static cv::Mat MaskAreaOutsideBall(cv::Mat& ball_image, const GolfBall& ball, float mask_reduction_factor, const cv::Scalar& maskValue = (255, 255, 255)) {
        return SpinAnalyzer::MaskAreaOutsideBall(ball_image, ball, mask_reduction_factor, maskValue);
    }

    static void GetRotatedImage(const cv::Mat& gray_2D_input_image, const GolfBall& ball, const cv::Vec3i rotation, cv::Mat& outputGrayImg) {
        SpinAnalyzer::GetRotatedImage(gray_2D_input_image, ball, rotation, outputGrayImg);
    }

    // Ellipse detection methods (delegated to EllipseDetector)
    static cv::RotatedRect FindLargestEllipse(cv::Mat& img, const GsCircle& reference_ball_circle, int mask_radius);
    static cv::RotatedRect FindBestEllipseFornaciari(cv::Mat& img,
                                                    const GsCircle& reference_ball_circle,
                                                    int mask_radius);

    // Hough detection methods (delegated to HoughDetector)
    static bool RemoveSmallestConcentricCircles(std::vector<GsCircle>& circles);

    // --- Color filter methods (delegated to ColorFilter) ---

    cv::Mat GetColorMaskImage(const cv::Mat& hsvImage, const GolfBall& ball, double wideningAmount = 0.0) {
        return ColorFilter::GetColorMaskImage(hsvImage, ball, wideningAmount);
    }

    static cv::Mat GetColorMaskImage(const cv::Mat& hsvImage,
        const GsColorTriplet input_lowerHsv,
        const GsColorTriplet input_upperHsv,
        double wideningAmount = 0.0) {
        return ColorFilter::GetColorMaskImage(hsvImage, input_lowerHsv, input_upperHsv, wideningAmount);
    }

    bool PreProcessStrobedImage(cv::Mat& search_image, BallSearchMode search_mode);

    // ONNX Detection Methods
    static bool DetectBalls(const cv::Mat& preprocessed_img, BallSearchMode search_mode, std::vector<GsCircle>& detected_circles);
    static bool DetectBallsHoughCircles(const cv::Mat& preprocessed_img, BallSearchMode search_mode, std::vector<GsCircle>& detected_circles);
    static bool DetectBallsONNX(const cv::Mat& preprocessed_img, BallSearchMode search_mode, std::vector<GsCircle>& detected_circles);

    static bool DetectBallsONNXRuntime(const cv::Mat& preprocessed_img, BallSearchMode search_mode, std::vector<GsCircle>& detected_circles);
    static bool DetectBallsOpenCVDNN(const cv::Mat& preprocessed_img, BallSearchMode search_mode, std::vector<GsCircle>& detected_circles);

    static bool PreloadYOLOModel();
    static bool PreloadONNXRuntimeModel();
    static void CleanupONNXRuntime();

    // Load configuration values from JSON after config is initialized
    static void LoadConfigurationValues();

    // Custom single-class NMS optimized for golf balls (faster than generic multi-class NMS)
    static std::vector<int> SingleClassNMS(const std::vector<cv::Rect>& boxes,
                                          const std::vector<float>& confidences,
                                          float conf_threshold,
                                          float nms_threshold);

private:
    // ONNX Runtime detector instance - replaces all static ONNX members
    static std::unique_ptr<ONNXRuntimeDetector> onnx_detector_;
    static std::atomic<bool> onnx_detector_initialized_;
    static std::mutex onnx_detector_mutex_;

    static cv::dnn::Net yolo_model_;
    static bool yolo_model_loaded_;
    static std::mutex yolo_model_mutex_;  // Thread safety for model loading
    // Prevents allocating ~1.2MB per frame (640x640x3 multiple times)
    static cv::Mat yolo_input_buffer_;        // Reusable input conversion buffer
    static cv::Mat yolo_letterbox_buffer_;    // 640x640x3 letterboxed image
    static cv::Mat yolo_resized_buffer_;      // Resized image before letterboxing
    static cv::Mat yolo_blob_buffer_;         // Blob for network input
    static std::vector<cv::Rect> yolo_detection_boxes_;     // Detection results
    static std::vector<float> yolo_detection_confidences_;  // Detection confidences
    static std::vector<cv::Mat> yolo_outputs_;              // Network outputs

    // When we create a candidate ball list, the elements of that list include not only 
    // the ball, but also the ball identifier(e.g., 1, 2...),
    // as well as information about the difference between the ball's average/median/std color versus the expected color.
    // The following constants identify where in each element the information is

    struct CircleCandidateListElement {
        std::string     name;
        GsCircle        circle;
        double          calculated_color_difference;
        int             found_radius;
        GsColorTriplet  avg_RGB;
        float           rgb_avg_diff;
        float           rgb_median_diff;
        float           rgb_std_diff;
    };

    static std::string FormatCircleCandidateElement(const struct CircleCandidateListElement& e);
    static std::string FormatCircleCandidateList(const std::vector<struct CircleCandidateListElement>& e);

    // This is an early attempt to remove lines from an image, such as those caused when using the 
    // system with another strobe-based launch monitor
    static bool RemoveLinearNoise(cv::Mat& img);

    inline bool CompareColorDiff(const CircleCandidateListElement& a, const CircleCandidateListElement& b)
    {
        return (a.calculated_color_difference < b.calculated_color_difference);
    }

    void RoundCircleData(std::vector<GsCircle>& circles);

    static cv::Rect GetAreaOfInterest(const GolfBall& ball, const cv::Mat& img) {
        return ROIManager::GetAreaOfInterest(ball, img);
    }

    // Spin analysis private methods have been moved to SpinAnalyzer class.
    // See ball_detection/spin_analyzer.h for: IsolateBall, ReduceReflections, RemoveReflections,
    // ApplyGaborFilterToBall, ApplyTestGaborFilter, CreateGaborKernel, Project2dImageTo3dBall,
    // Unproject3dBallTo2dImage, GetImageCharacteristics

};

}
