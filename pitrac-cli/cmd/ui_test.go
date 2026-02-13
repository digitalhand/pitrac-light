// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Digital Hand LLC.

package cmd

import (
	"os"
	"testing"
)

func TestUseColor_NoColor(t *testing.T) {
	t.Setenv("NO_COLOR", "1")
	t.Setenv("TERM", "xterm-256color")

	if useColor() {
		t.Error("expected useColor() = false when NO_COLOR is set")
	}
}

func TestUseColor_TermDumb(t *testing.T) {
	// Clear NO_COLOR
	os.Unsetenv("NO_COLOR")
	t.Setenv("TERM", "dumb")

	if useColor() {
		t.Error("expected useColor() = false when TERM=dumb")
	}
}

func TestUseColor_TermEmpty(t *testing.T) {
	os.Unsetenv("NO_COLOR")
	t.Setenv("TERM", "")

	if useColor() {
		t.Error("expected useColor() = false when TERM is empty")
	}
}

func TestUseColor_Normal(t *testing.T) {
	os.Unsetenv("NO_COLOR")
	t.Setenv("TERM", "xterm-256color")

	if !useColor() {
		t.Error("expected useColor() = true with normal TERM")
	}
}

func TestColorize_NoColor(t *testing.T) {
	t.Setenv("NO_COLOR", "1")

	got := colorize(ansiGreen, "hello")
	if got != "hello" {
		t.Errorf("expected plain text, got %q", got)
	}
}
