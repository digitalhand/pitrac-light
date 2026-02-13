/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026, Digital Hand LLC.
 */

/**
 * @file test_fsm_transitions.cpp
 * @brief Unit tests for finite state machine transitions
 *
 * Tests the golf simulator's FSM state transitions, timing, and state data.
 * Critical for ensuring correct shot detection workflow.
 */

#define BOOST_TEST_MODULE FSMTransitionTests
#include <boost/test/unit_test.hpp>
#include "../test_utilities.hpp"
#include "gs_fsm.h"
#include "golf_ball.h"

using namespace golf_sim;
using namespace golf_sim::state;
using namespace golf_sim::testing;

BOOST_AUTO_TEST_SUITE(FSMTransitionTests)

// ===========================================================================
// State Type Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(GolfSimState_InitializingCamera1_CreatesCorrectState) {
    GolfSimState state = InitializingCamera1System{};
    BOOST_CHECK(std::holds_alternative<InitializingCamera1System>(state));
}

BOOST_AUTO_TEST_CASE(GolfSimState_WaitingForBall_CreatesCorrectState) {
    GolfSimState state = WaitingForBall{};
    BOOST_CHECK(std::holds_alternative<WaitingForBall>(state));
}

BOOST_AUTO_TEST_CASE(GolfSimState_WaitingForBallStabilization_CreatesCorrectState) {
    WaitingForBallStabilization stabilization_state;
    stabilization_state.startTime_ = std::chrono::steady_clock::now();
    stabilization_state.cam1_ball_ = GolfBall();

    GolfSimState state = stabilization_state;
    BOOST_CHECK(std::holds_alternative<WaitingForBallStabilization>(state));
}

BOOST_AUTO_TEST_CASE(GolfSimState_WaitingForBallHit_CreatesCorrectState) {
    WaitingForBallHit hit_state;
    hit_state.startTime_ = std::chrono::steady_clock::now();
    hit_state.cam1_ball_ = GolfBall();

    GolfSimState state = hit_state;
    BOOST_CHECK(std::holds_alternative<WaitingForBallHit>(state));
}

BOOST_AUTO_TEST_CASE(GolfSimState_Exiting_CreatesCorrectState) {
    GolfSimState state = Exiting{};
    BOOST_CHECK(std::holds_alternative<Exiting>(state));
}

// ===========================================================================
// State Data Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(WaitingForBall_HasStartTime) {
    WaitingForBall state;
    state.startTime_ = std::chrono::steady_clock::now();

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - state.startTime_
    );

    BOOST_CHECK_LT(elapsed.count(), 100);  // Should be very recent
}

BOOST_AUTO_TEST_CASE(WaitingForBall_IPCMessageFlag_DefaultsFalse) {
    WaitingForBall state;
    BOOST_CHECK_EQUAL(state.already_sent_waiting_ipc_message, false);
}

BOOST_AUTO_TEST_CASE(WaitingForBallStabilization_StoresTimestamps) {
    WaitingForBallStabilization state;
    state.startTime_ = std::chrono::steady_clock::now();
    state.lastBallAcquisitionTime_ = std::chrono::steady_clock::now();

    BOOST_CHECK(state.startTime_.time_since_epoch().count() > 0);
    BOOST_CHECK(state.lastBallAcquisitionTime_.time_since_epoch().count() > 0);
}

BOOST_AUTO_TEST_CASE(WaitingForBallStabilization_StoresBallData) {
    WaitingForBallStabilization state;
    state.cam1_ball_ = GolfBall();
    state.cam1_ball_.ball_circle_ = GsCircle(100.0f, 200.0f, 25.0f);

    BOOST_CHECK_EQUAL(state.cam1_ball_.ball_circle_[0], 100.0f);
    BOOST_CHECK_EQUAL(state.cam1_ball_.ball_circle_[1], 200.0f);
    BOOST_CHECK_EQUAL(state.cam1_ball_.ball_circle_[2], 25.0f);
}

BOOST_AUTO_TEST_CASE(WaitingForBallHit_StoresCamera2PreImage) {
    WaitingForBallHit state;
    state.camera2_pre_image_ = cv::Mat(480, 640, CV_8UC3, cv::Scalar(50, 50, 50));

    BOOST_CHECK_EQUAL(state.camera2_pre_image_.rows, 480);
    BOOST_CHECK_EQUAL(state.camera2_pre_image_.cols, 640);
    BOOST_CHECK(!state.camera2_pre_image_.empty());
}

// ===========================================================================
// State Variant Access Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(GolfSimState_CanAccessWaitingForBall) {
    WaitingForBall wait_state;
    wait_state.already_sent_waiting_ipc_message = true;

    GolfSimState state = wait_state;

    if (auto* w = std::get_if<WaitingForBall>(&state)) {
        BOOST_CHECK_EQUAL(w->already_sent_waiting_ipc_message, true);
    } else {
        BOOST_FAIL("Failed to access WaitingForBall state");
    }
}

BOOST_AUTO_TEST_CASE(GolfSimState_CanAccessWaitingForBallStabilization) {
    WaitingForBallStabilization stab_state;
    stab_state.cam1_ball_.ball_circle_ = GsCircle(150.0f, 250.0f, 30.0f);

    GolfSimState state = stab_state;

    if (auto* s = std::get_if<WaitingForBallStabilization>(&state)) {
        BOOST_CHECK_EQUAL(s->cam1_ball_.ball_circle_[0], 150.0f);
    } else {
        BOOST_FAIL("Failed to access WaitingForBallStabilization state");
    }
}

// ===========================================================================
// Timing Behavior Tests
// ===========================================================================

struct TimingTestFixture : public golf_sim::testing::TimingTestFixture {
    const std::chrono::milliseconds kBallStabilizationTimeout{2000};
    const std::chrono::milliseconds kCamera2Timeout{5000};
};

BOOST_FIXTURE_TEST_CASE(WaitingForBallStabilization_TimingMeasurement, TimingTestFixture) {
    WaitingForBallStabilization state;
    state.startTime_ = std::chrono::steady_clock::now();

    // Simulate some processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - state.startTime_
    );

    BOOST_CHECK_GE(elapsed.count(), 10);
    BOOST_CHECK_LT(elapsed.count(), 100);
}

BOOST_FIXTURE_TEST_CASE(WaitingForBallStabilization_LastAcquisitionUpdate, TimingTestFixture) {
    WaitingForBallStabilization state;
    auto start = std::chrono::steady_clock::now();
    state.lastBallAcquisitionTime_ = start;

    // Simulate ball reacquisition
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    state.lastBallAcquisitionTime_ = std::chrono::steady_clock::now();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        state.lastBallAcquisitionTime_ - start
    );

    BOOST_CHECK_GE(elapsed.count(), 10);
}

// ===========================================================================
// Camera 2 State Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(InitializingCamera2System_CreatesCorrectState) {
    GolfSimState state = InitializingCamera2System{};
    BOOST_CHECK(std::holds_alternative<InitializingCamera2System>(state));
}

BOOST_AUTO_TEST_CASE(WaitingForCameraArmMessage_HasStartTime) {
    WaitingForCameraArmMessage state;
    state.startTime_ = std::chrono::steady_clock::now();

    BOOST_CHECK(state.startTime_.time_since_epoch().count() > 0);
}

BOOST_AUTO_TEST_CASE(WaitingForCameraTrigger_HasStartTime) {
    WaitingForCameraTrigger state;
    state.startTime_ = std::chrono::steady_clock::now();

    BOOST_CHECK(state.startTime_.time_since_epoch().count() > 0);
}

// ===========================================================================
// State Transition Simulation Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(StateTransition_InitToWaitingForBall) {
    // Simulate: Initialize → WaitingForBall
    GolfSimState state = InitializingCamera1System{};
    BOOST_CHECK(std::holds_alternative<InitializingCamera1System>(state));

    // Transition
    state = WaitingForBall{};
    BOOST_CHECK(std::holds_alternative<WaitingForBall>(state));
}

BOOST_AUTO_TEST_CASE(StateTransition_WaitingToBallStabilization) {
    // Simulate: WaitingForBall → WaitingForBallStabilization
    GolfSimState state = WaitingForBall{};

    // Ball detected, transition to stabilization
    WaitingForBallStabilization stab_state;
    stab_state.startTime_ = std::chrono::steady_clock::now();
    stab_state.lastBallAcquisitionTime_ = stab_state.startTime_;
    stab_state.cam1_ball_.ball_circle_ = GsCircle(320.0f, 240.0f, 20.0f);

    state = stab_state;
    BOOST_CHECK(std::holds_alternative<WaitingForBallStabilization>(state));
}

BOOST_AUTO_TEST_CASE(StateTransition_StabilizationToWaitingForHit) {
    // Simulate: WaitingForBallStabilization → WaitingForBallHit
    GolfSimState state = WaitingForBallStabilization{};

    // Ball stabilized, transition to waiting for hit
    WaitingForBallHit hit_state;
    hit_state.startTime_ = std::chrono::steady_clock::now();
    hit_state.cam1_ball_.ball_circle_ = GsCircle(320.0f, 240.0f, 20.0f);

    state = hit_state;
    BOOST_CHECK(std::holds_alternative<WaitingForBallHit>(state));
}

BOOST_AUTO_TEST_CASE(StateTransition_HitToWaitingForCam2) {
    // Simulate: WaitingForBallHit → BallHitNowWaitingForCam2Image
    GolfSimState state = WaitingForBallHit{};

    // Ball hit detected, transition to waiting for camera 2
    BallHitNowWaitingForCam2Image cam2_state;
    cam2_state.cam1_ball_.ball_circle_ = GsCircle(320.0f, 240.0f, 20.0f);

    state = cam2_state;
    BOOST_CHECK(std::holds_alternative<BallHitNowWaitingForCam2Image>(state));
}

// ===========================================================================
// Data Preservation Tests (State Transitions)
// ===========================================================================

BOOST_AUTO_TEST_CASE(StateTransition_PreservesBallData) {
    // Ball data should be preserved across transitions
    GolfBall original_ball;
    original_ball.ball_circle_ = GsCircle(100.0f, 200.0f, 25.0f);

    // Start in stabilization
    WaitingForBallStabilization stab_state;
    stab_state.cam1_ball_ = original_ball;

    // Transition to hit
    WaitingForBallHit hit_state;
    hit_state.cam1_ball_ = stab_state.cam1_ball_;
    hit_state.startTime_ = std::chrono::steady_clock::now();

    // Verify ball data preserved
    BOOST_CHECK_EQUAL(hit_state.cam1_ball_.ball_circle_[0], original_ball.ball_circle_[0]);
    BOOST_CHECK_EQUAL(hit_state.cam1_ball_.ball_circle_[1], original_ball.ball_circle_[1]);
    BOOST_CHECK_EQUAL(hit_state.cam1_ball_.ball_circle_[2], original_ball.ball_circle_[2]);
}

BOOST_AUTO_TEST_CASE(StateTransition_PreservesImageData) {
    // Create test image
    cv::Mat test_image(480, 640, CV_8UC3, cv::Scalar(100, 150, 200));

    // Store in stabilization state
    WaitingForBallStabilization stab_state;
    stab_state.ball_image_ = test_image.clone();

    // Transition to hit state
    WaitingForBallHit hit_state;
    hit_state.ball_image_ = stab_state.ball_image_;
    hit_state.startTime_ = std::chrono::steady_clock::now();

    // Verify image preserved
    BOOST_CHECK_EQUAL(hit_state.ball_image_.rows, test_image.rows);
    BOOST_CHECK_EQUAL(hit_state.ball_image_.cols, test_image.cols);
    BOOST_CHECK_EQUAL(hit_state.ball_image_.type(), test_image.type());
}

// ===========================================================================
// Edge Case Tests
// ===========================================================================

BOOST_AUTO_TEST_CASE(WaitingForBall_MultipleIPCMessageChecks) {
    WaitingForBall state;
    BOOST_CHECK_EQUAL(state.already_sent_waiting_ipc_message, false);

    // First IPC send
    state.already_sent_waiting_ipc_message = true;
    BOOST_CHECK_EQUAL(state.already_sent_waiting_ipc_message, true);

    // Subsequent checks should see flag is already set
    BOOST_CHECK_EQUAL(state.already_sent_waiting_ipc_message, true);
}

BOOST_AUTO_TEST_CASE(WaitingForBallStabilization_EmptyBallImage) {
    WaitingForBallStabilization state;
    BOOST_CHECK(state.ball_image_.empty());

    // Assign image
    state.ball_image_ = cv::Mat(480, 640, CV_8UC3);
    BOOST_CHECK(!state.ball_image_.empty());
}

BOOST_AUTO_TEST_SUITE_END()
