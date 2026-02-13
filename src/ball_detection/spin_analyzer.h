/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

// Handles golf ball spin (rotation) analysis using Gabor filters and 3D hemisphere projection.
// Extracted from ball_image_proc.cpp as part of Phase 3.1 modular refactoring.

#pragma once

#include <vector>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "golf_ball.h"
#include "utils/logging_tools.h"


namespace golf_sim {

// When comparing what are otherwise b/w images, this value indicates that
// the comparison should not be performed on the particular pixel
extern const uchar kPixelIgnoreValue;

// Holds one potential rotated golf ball candidate image and associated data
struct RotationCandidate {
    short index = 0;
    cv::Mat img;
    int x_rotation_degrees = 0; // All Rotations are in degrees
    int y_rotation_degrees = 0;
    int z_rotation_degrees = 0;
    int pixels_examined = 0;
    int pixels_matching = 0;
    double score = 0;
};

// This determines which potential 3D angles will be searched for spin processing
struct RotationSearchSpace {
    int anglex_rotation_degrees_increment = 0;
    int anglex_rotation_degrees_start = 0;
    int anglex_rotation_degrees_end = 0;
    int angley_rotation_degrees_increment = 0;
    int angley_rotation_degrees_start = 0;
    int angley_rotation_degrees_end = 0;
    int anglez_rotation_degrees_increment = 0;
    int anglez_rotation_degrees_start = 0;
    int anglez_rotation_degrees_end = 0;
};

class SpinAnalyzer {
public:

    // --- Configuration constants (loaded from JSON config) ---

    static int kCoarseXRotationDegreesIncrement;
    static int kCoarseXRotationDegreesStart;
    static int kCoarseXRotationDegreesEnd;
    static int kCoarseYRotationDegreesIncrement;
    static int kCoarseYRotationDegreesStart;
    static int kCoarseYRotationDegreesEnd;
    static int kCoarseZRotationDegreesIncrement;
    static int kCoarseZRotationDegreesStart;
    static int kCoarseZRotationDegreesEnd;

    static int kGaborMaxWhitePercent;
    static int kGaborMinWhitePercent;

    static bool kLogIntermediateSpinImagesToFile;

    // --- Public API ---

    // Inputs are two balls and the images within which those balls exist.
    // Returns the estimated amount of rotation in x, y, and z axes in degrees.
    static cv::Vec3d GetBallRotation(const cv::Mat& full_gray_image1,
                                     const GolfBall& ball1,
                                     const cv::Mat& full_gray_image2,
                                     const GolfBall& ball2);

    static bool ComputeCandidateAngleImages(const cv::Mat& base_dimple_image,
                                            const RotationSearchSpace& search_space,
                                            cv::Mat& output_candidate_mat,
                                            cv::Vec3i& output_candidate_elements_mat_size,
                                            std::vector<RotationCandidate>& output_candidates,
                                            const GolfBall& ball);

    // Returns the index within candidates that has the best comparison.
    // Returns -1 on failure.
    static int CompareCandidateAngleImages(const cv::Mat* target_image,
                                           const cv::Mat* candidate_elements_mat,
                                           const cv::Vec3i* candidate_elements_mat_size,
                                           std::vector<RotationCandidate>* candidates,
                                           std::vector<std::string>& comparison_csv_data);

    static cv::Vec2i CompareRotationImage(const cv::Mat& img1, const cv::Mat& img2, const int index = 0);

    static cv::Mat MaskAreaOutsideBall(cv::Mat& ball_image, const GolfBall& ball, float mask_reduction_factor, const cv::Scalar& maskValue = (255, 255, 255));

    static void GetRotatedImage(const cv::Mat& gray_2D_input_image, const GolfBall& ball, const cv::Vec3i rotation, cv::Mat& outputGrayImg);

    // Load configuration values from the JSON config system
    static void LoadConfigurationValues();

private:

    // Assumes the ball is fully within the image.
    // Updates the input ball to reflect the new position within the isolated image.
    static cv::Mat IsolateBall(const cv::Mat& img, GolfBall& ball);

    static cv::Mat ReduceReflections(const cv::Mat& img, const cv::Mat& mask);

    // Sets pixels that were over-saturated in the original_image to be the special "ignore" kPixelIgnoreValue
    // in the filtered_image.
    static void RemoveReflections(const cv::Mat& original_image, cv::Mat& filtered_image, const cv::Mat& mask);

    // If prior_binary_threshold < 0, then there is no prior threshold and a new one will be determined.
    static cv::Mat ApplyGaborFilterToBall(const cv::Mat& img, const GolfBall& ball, float& calibrated_binary_threshold, float prior_binary_threshold = -1);

    // Applies the gabor filter with the specified parameters and returns the final image and white percentage
    static cv::Mat ApplyTestGaborFilter(const cv::Mat& img_f32,
        const int kernel_size, double sig, double lm, double th, double ps, double gm, float binary_threshold,
        int& white_percent);

    static cv::Mat CreateGaborKernel(int ks, double sig, double th, double lm, double gm, double ps);

    static cv::Mat Project2dImageTo3dBall(const cv::Mat& image_gray, const GolfBall& ball, const cv::Vec3i& rotation_angles_degrees);

    static void Unproject3dBallTo2dImage(const cv::Mat& src3D, cv::Mat& destination_image_gray, const GolfBall& ball);

    // Given a grayscale (0-255) image and a percentage, returns brightness_cutoff from 0-255
    static void GetImageCharacteristics(const cv::Mat& img,
                                        const int brightness_percentage,
                                        int& brightness_cutoff,
                                        int& lowest_brightness,
                                        int& highest_brightness);
};

}
