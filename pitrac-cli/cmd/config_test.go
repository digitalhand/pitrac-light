package cmd

import (
	"strings"
	"testing"
)

func TestBuildCommonArgs_AllPresent(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
	}

	args, err := buildCommonArgs(values)
	if err != nil {
		t.Fatal(err)
	}

	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "--config_file") {
		t.Error("expected --config_file in output")
	}
	if !strings.Contains(joined, "golf_sim_config.json") {
		t.Error("expected golf_sim_config.json in output")
	}
	if !strings.Contains(joined, "--msg_broker_address") {
		t.Error("expected --msg_broker_address in output")
	}
}

func TestBuildCommonArgs_MissingRequired(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT": "/home/user/PiTrac",
		// missing the other three
	}

	_, err := buildCommonArgs(values)
	if err == nil {
		t.Error("expected error for missing required values")
	}
}

func TestBuildCommonArgs_SimHostAddress(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
		"PITRAC_SIM_HOST_ADDRESS":        "10.0.0.99",
	}

	args, err := buildCommonArgs(values)
	if err != nil {
		t.Fatal(err)
	}

	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "--gspro_host_address") {
		t.Error("expected --gspro_host_address when PITRAC_SIM_HOST_ADDRESS is set")
	}
	if !strings.Contains(joined, "10.0.0.99") {
		t.Error("expected sim address value in output")
	}
}

func TestBuildCommonArgs_DeprecatedGSProFallback(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
		"PITRAC_GSPRO_HOST_ADDRESS":      "10.0.0.51",
	}

	args, err := buildCommonArgs(values)
	if err != nil {
		t.Fatal(err)
	}

	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "--gspro_host_address") {
		t.Error("expected --gspro_host_address when deprecated PITRAC_GSPRO_HOST_ADDRESS is set")
	}
	if !strings.Contains(joined, "10.0.0.51") {
		t.Error("expected GSPro address value in output")
	}
}

func TestBuildCommonArgs_SimHostOverridesGSPro(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
		"PITRAC_SIM_HOST_ADDRESS":        "10.0.0.99",
		"PITRAC_GSPRO_HOST_ADDRESS":      "10.0.0.51",
	}

	args, err := buildCommonArgs(values)
	if err != nil {
		t.Fatal(err)
	}

	joined := strings.Join(args, " ")
	if !strings.Contains(joined, "10.0.0.99") {
		t.Error("expected PITRAC_SIM_HOST_ADDRESS to take precedence")
	}
	if strings.Contains(joined, "10.0.0.51") {
		t.Error("did not expect deprecated PITRAC_GSPRO_HOST_ADDRESS when PITRAC_SIM_HOST_ADDRESS is set")
	}
}

func TestBuildCommonArgs_NoSimAddress(t *testing.T) {
	values := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
	}

	args, err := buildCommonArgs(values)
	if err != nil {
		t.Fatal(err)
	}

	joined := strings.Join(args, " ")
	if strings.Contains(joined, "--gspro_host_address") {
		t.Error("did not expect --gspro_host_address when no sim address env var is set")
	}
}
