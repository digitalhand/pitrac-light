package cmd

import (
	"strings"
	"testing"
)

func TestResolvedVersion_Dev(t *testing.T) {
	v := resolvedVersion()
	if v == "" {
		t.Error("resolvedVersion() returned empty string")
	}
	// Should at minimum contain some version info
	if !strings.Contains(v, "dev") && !strings.Contains(v, "v") && !strings.Contains(v, ".") {
		t.Errorf("resolvedVersion() = %q, expected version-like string", v)
	}
}

func TestResolvedVersion_WithLdflags(t *testing.T) {
	orig := Version
	origCommit := Commit
	origDate := Date
	defer func() {
		Version = orig
		Commit = origCommit
		Date = origDate
	}()

	Version = "v1.2.3"
	Commit = "abc123"
	Date = "2025-01-01"

	v := resolvedVersion()
	if !strings.Contains(v, "v1.2.3") {
		t.Errorf("expected v1.2.3 in version, got %q", v)
	}
	if !strings.Contains(v, "abc123") {
		t.Errorf("expected abc123 in version, got %q", v)
	}
}
