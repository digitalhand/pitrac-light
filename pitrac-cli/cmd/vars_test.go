package cmd

import "testing"

func TestRequiredEnvVars_NonEmpty(t *testing.T) {
	if len(requiredEnvVars) == 0 {
		t.Error("requiredEnvVars should not be empty")
	}
}

func TestOptionalEnvVars_NonEmpty(t *testing.T) {
	if len(optionalEnvVars) == 0 {
		t.Error("optionalEnvVars should not be empty")
	}
}

func TestAllEnvVars(t *testing.T) {
	all := allEnvVars()
	expected := len(requiredEnvVars) + len(optionalEnvVars)
	if len(all) != expected {
		t.Errorf("allEnvVars() returned %d items, expected %d", len(all), expected)
	}

	// First items should be the required ones
	for i, v := range requiredEnvVars {
		if all[i] != v {
			t.Errorf("allEnvVars()[%d] = %q, want %q", i, all[i], v)
		}
	}
}
