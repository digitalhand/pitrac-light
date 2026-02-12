package cmd

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadEnvFile_Basic(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "test.env")

	content := `# comment
export PITRAC_ROOT="/home/user/PiTrac"
PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://127.0.0.1:61616
export PITRAC_WEBSERVER_SHARE_DIR='/home/user/shares/'
PITRAC_BASE_IMAGE_LOGGING_DIR="/home/user/logs"

# another comment
`
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}

	values, err := loadEnvFile(path)
	if err != nil {
		t.Fatal(err)
	}

	tests := map[string]string{
		"PITRAC_ROOT":                    "/home/user/PiTrac",
		"PITRAC_MSG_BROKER_FULL_ADDRESS": "tcp://127.0.0.1:61616",
		"PITRAC_WEBSERVER_SHARE_DIR":     "/home/user/shares/",
		"PITRAC_BASE_IMAGE_LOGGING_DIR":  "/home/user/logs",
	}

	for key, want := range tests {
		if got := values[key]; got != want {
			t.Errorf("%s = %q, want %q", key, got, want)
		}
	}
}

func TestLoadEnvFile_EmptyLines(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "empty.env")

	if err := os.WriteFile(path, []byte("\n\n\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	values, err := loadEnvFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(values) != 0 {
		t.Errorf("expected 0 values, got %d", len(values))
	}
}

func TestLoadEnvFile_MissingFile(t *testing.T) {
	_, err := loadEnvFile("/nonexistent/path/env")
	if err == nil {
		t.Error("expected error for missing file")
	}
}

func TestLoadEnvFile_CommentsOnly(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "comments.env")

	content := `# just comments
# nothing else
`
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}

	values, err := loadEnvFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(values) != 0 {
		t.Errorf("expected 0 values, got %d", len(values))
	}
}
