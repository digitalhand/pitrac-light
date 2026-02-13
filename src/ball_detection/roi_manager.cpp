/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <cmath>
#include <iomanip>
#include <iostream>

#include <boost/timer/timer.hpp>

#include "ball_detection/roi_manager.h"
#include "gs_camera.h"
#include "utils/logging_tools.h"


namespace golf_sim {

    cv::Rect ROIManager::GetAreaOfInterest(const GolfBall& ball, const cv::Mat& img) {

        // The area of interest is right in front (ball-fly direction) of the ball.  Anything in
        // the ball or behind it could just be lighting changes or the human teeing up.
        int x = (int)ball.ball_circle_[0];
        int y = (int)ball.ball_circle_[1];
        int r = (int)ball.ball_circle_[2];

        // The 1.1 just makes sure we are mostely outside of where the ball currently is
        int xmin = std::max(x, 0);      // OLD: std::max(x + (int)(r*1.1), 0);
        int xmax = std::min(x + 10*r, img.cols);
        int ymin = std::max(y - 6*r, 0);
        int ymax = std::min(y + (int)(r*1.5), img.rows);

        cv::Rect rect{ cv::Point(xmin, ymin), cv::Point(xmax, ymax) };

        return rect;
    }

    bool ROIManager::BallIsPresent(const cv::Mat& img) {
        GS_LOG_TRACE_MSG(trace, "BallIsPresent: image=" + LoggingTools::SummarizeImage(img));
        return true;

        /*
               // TBD - r is ball radius - should refactor these constants
               dm = radius / (GolfBall.r * f)
               // get pall position in spatial coordinates
               pos_p = nc::array([[x], [y], [1], [dm]] )
               pos_w = P_inv.dot(pos_p)
               return pos_w / pos_w[3][0], time
       */
    }

    bool ROIManager::WaitForBallMovement(GolfSimCamera &c, cv::Mat& firstMovementImage, const GolfBall& ball, const long waitTimeSecs) {
        BOOST_LOG_FUNCTION();

        GS_LOG_TRACE_MSG(trace, "wait_for_movement called with ball = " + ball.Format());

        //min area of motion detectable - based on ball radius, should be at least as large as a third of a ball
        int min_area = (int)pow(ball.ball_circle_[2],2.0);  // Rougly a third of the ball size

        boost::timer::cpu_timer timer1;

        cv::Mat firstFrame, gray, imageDifference, thresh;
        std::vector<std::vector<cv::Point> > contours;
        std::vector<cv::Vec4i> hierarchy;

        int startupFrameCount = 0;
        int frameLoopCount = 0;

        long r = (int)ball.measured_radius_pixels_;
        cv::Rect ballRect{ (int)( ball.x() - r ), (int)( ball.y() - r ), (int)(2 * r), (int)(2 * r) };

        bool foundMotion = false;

        cv::Mat frame;

        while (!foundMotion) {

            boost::timer::cpu_times elapsedTime = timer1.elapsed();

            if (elapsedTime.wall / 1.0e9 > waitTimeSecs) {
                LoggingTools::Warning("BallImageProc::WaitForBallMovement - time ran out");
                break;
            }

            cv::Mat fullFrame = c.getNextFrame();

            frameLoopCount++;

            if (fullFrame.empty()) {
                LoggingTools::Warning("frame was not captured");
                return(false);
            }

            // We will skip a few frames first for everything stabilize (TBD - is this necessary?)
            if (startupFrameCount < 1) {
                ++startupFrameCount;
                continue;
            }

            // LoggingTools::DebugShowImage("Next Frame", fullFrame);

            // We don't want to look at changes in the image just anywhere, instead narrow down to the
            // area around the ball, especially behind it.
            // TBD - Handed-Specific!

            cv::Rect areaOfInterest = GetAreaOfInterest(ball, fullFrame);
            frame = fullFrame(cv::Range(areaOfInterest.tl().y, areaOfInterest.br().y),
                                         cv::Range(areaOfInterest.tl().x, areaOfInterest.br().x));

            LoggingTools::DebugShowImage("Area of Interest", frame);

            //pre processing
            //resize(frame, frame, Size (1200,900));
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
            // WAS ORIGINALLY - cv::GaussianBlur(gray, gray, cv::Size(21, 21), 0, 0);
            // A 7x7 kernel is plenty of blurring for our purpose (of removing transient spikes).
            // It is almost twice as fast as a larger 21x21 kernel!
            cv::GaussianBlur(gray, gray, cv::Size(7, 7), 0, 0);

            //initialize first frame if necessary and don't do any comparison yet (as we only have one frame)
            if (firstFrame.empty()) {
                gray.copyTo(firstFrame);
                continue;
            }

            // Maintain a circular file of recent images so that we can, e.g., perform club face analysis
            // TBD
            //

            //LoggingTools::DebugShowImage("First Frame Image", firstFrame);
            //LoggingTools::DebugShowImage("Blurred Image", gray);

            const int kThreshLevel = 70;

            // get difference
            cv::absdiff(firstFrame, gray, imageDifference);

            // LoggingTools::DebugShowImage("Difference", imageDifference);

            cv::threshold(imageDifference, thresh, kThreshLevel, 255.0, cv::THRESH_BINARY );  //  | cv::THRESH_OTSU);
            // GS_LOG_TRACE_MSG(trace, "Otsu Threshold Value was:" + std::to_string(t));

            // fill in any small holes
            // TBD - TAKING TIME?  NECESSARY?
            // cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1, -1), 2, 1, 1);

            // LoggingTools::DebugShowImage("Threshold image: ", thresh);

            cv::findContours(thresh, contours, hierarchy, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);


            int totalAreaOfDeltas = 0;
            bool atLeastOneLargeAreaOfChange = false;

            //loop over contours
            for (size_t i = 0; i < contours.size(); i++) {
                //get the boundboxes and save the ROI as an Image
                cv::Rect boundRect = cv::boundingRect(cv::Mat(contours[i]));

                /* Only use if the original ball will be included in the area of interest
                // Quick way to test for rectangle inclusion
                if ((boundRect & ballRect) == boundRect) {
                    // Ignore any changes where the ball is - it could just be a lighting change
                    continue;
                }
                */
                long area = (long)cv::contourArea(contours[i]);
                if (area > min_area) {
                    atLeastOneLargeAreaOfChange = true;
                }
                totalAreaOfDeltas += area;
                cv::rectangle(frame, boundRect.tl(), boundRect.br(), cv::Scalar(255, 255, 0), 3, 8, 0);
            }

            LoggingTools::DebugShowImage("Contours of areas meeting minimum threshold", frame);

            // If we didn't find at least one substantial change in the area of interest, keep waiting
            if (!atLeastOneLargeAreaOfChange || (totalAreaOfDeltas < min_area) ) {
                //GS_LOG_TRACE_MSG(trace, "Didn't find any substantial changes between frames");
                continue;
            }

            foundMotion = true;

            firstMovementImage = frame;
        }

        timer1.stop();
        boost::timer::cpu_times times = timer1.elapsed();
        std::cout << std::fixed << std::setprecision(8)
            << "Total Frame Loop Count = " << frameLoopCount << std::endl
            << "Startup Frame Loop Count = " << startupFrameCount << std::endl
            << times.wall / 1.0e9 << "s wall, "
            << times.user / 1.0e9 << "s user + "
            << times.system / 1.0e9 << "s system.\n";

        //draw everything
        LoggingTools::DebugShowImage("First Frame", firstFrame);
        LoggingTools::DebugShowImage("Action feed", frame);
        LoggingTools::DebugShowImage("Difference", imageDifference);
        LoggingTools::DebugShowImage("Thresh", thresh);
        /*
        */

        return foundMotion;
    }

}
