package cmd

import "testing"

func TestBuildCameraServiceArgs_AlwaysSinglePi(t *testing.T) {
	t.Setenv("PITRAC_ROOT", "/tmp/pitrac-light")
	t.Setenv("PITRAC_MSG_BROKER_FULL_ADDRESS", "tcp://127.0.0.1:61616")
	t.Setenv("PITRAC_WEBSERVER_SHARE_DIR", "/tmp/LM_Shares/Images/")
	t.Setenv("PITRAC_BASE_IMAGE_LOGGING_DIR", "/tmp/LM_Shares/PiTracLogs")

	camera1Args, err := buildCameraServiceArgs(1, 0)
	if err != nil {
		t.Fatalf("camera1 args failed: %v", err)
	}
	if !containsArg(camera1Args, "--run_single_pi") {
		t.Fatalf("camera1 args missing --run_single_pi: %v", camera1Args)
	}
	if !containsArgPair(camera1Args, "--system_mode", "camera1") {
		t.Fatalf("camera1 args missing --system_mode camera1: %v", camera1Args)
	}

	camera2Args, err := buildCameraServiceArgs(2, 0)
	if err != nil {
		t.Fatalf("camera2 args failed: %v", err)
	}
	if !containsArg(camera2Args, "--run_single_pi") {
		t.Fatalf("camera2 args missing --run_single_pi: %v", camera2Args)
	}
	if !containsArgPair(camera2Args, "--system_mode", "camera2") {
		t.Fatalf("camera2 args missing --system_mode camera2: %v", camera2Args)
	}
}

func containsArg(args []string, want string) bool {
	for _, a := range args {
		if a == want {
			return true
		}
	}
	return false
}

func containsArgPair(args []string, key, value string) bool {
	for i := 0; i < len(args)-1; i++ {
		if args[i] == key && args[i+1] == value {
			return true
		}
	}
	return false
}
