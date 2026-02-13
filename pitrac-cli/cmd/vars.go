// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Digital Hand LLC.

package cmd

// Shared environment variable lists used across doctor, env validate,
// config validate, and buildCommonArgs.
var requiredEnvVars = []string{
	"PITRAC_ROOT",
	"PITRAC_MSG_BROKER_FULL_ADDRESS",
	"PITRAC_WEBSERVER_SHARE_DIR",
	"PITRAC_BASE_IMAGE_LOGGING_DIR",
}

var optionalEnvVars = []string{
	"PITRAC_GSPRO_HOST_ADDRESS",
	"PITRAC_WEB_SERVER_URL",
}

// allEnvVars returns required + optional in order.
func allEnvVars() []string {
	out := make([]string, 0, len(requiredEnvVars)+len(optionalEnvVars))
	out = append(out, requiredEnvVars...)
	out = append(out, optionalEnvVars...)
	return out
}
