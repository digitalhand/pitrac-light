package cmd

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"github.com/spf13/cobra"
)

func init() {
	rootCmd.AddCommand(buildCmd)

	buildCmd.Flags().Bool("clean", false, "wipe build dir before building")
	buildCmd.Flags().Int("jobs", 4, "number of parallel build jobs")
	buildCmd.Flags().String("type", "release", "build type (release, debug, debugoptimized)")
	buildCmd.Flags().Bool("test", false, "run meson test after build")
	buildCmd.Flags().Bool("install", false, "copy binary to /usr/lib/pitrac/ after build")
	buildCmd.Flags().Bool("dry-run", false, "print commands without executing")
}

var buildCmd = &cobra.Command{
	Use:   "build",
	Short: "Build PiTrac from source (meson + ninja)",
	RunE:  runBuild,
}

func runBuild(cmd *cobra.Command, args []string) error {
	root := strings.TrimSpace(os.Getenv("PITRAC_ROOT"))
	rootSource := "PITRAC_ROOT env var"
	if root == "" {
		detected, err := detectRepoRoot()
		if err != nil {
			return fmt.Errorf("PITRAC_ROOT not set and could not detect repo root: %w\nHint: Run from repo directory or set PITRAC_ROOT=/path/to/pitrac-light", err)
		}
		root = detected
		rootSource = "auto-detected"
	}

	srcDir := filepath.Join(root, "src")
	buildDir := filepath.Join(srcDir, "build")

	// Verify the source directory exists
	if _, err := os.Stat(srcDir); err != nil {
		return fmt.Errorf("source directory not found: %s\nPITRAC_ROOT (%s): %s\nHint: Ensure PITRAC_ROOT points to the correct pitrac-light directory", srcDir, rootSource, root)
	}

	clean, _ := cmd.Flags().GetBool("clean")
	jobs, _ := cmd.Flags().GetInt("jobs")
	buildType, _ := cmd.Flags().GetString("type")
	runTest, _ := cmd.Flags().GetBool("test")
	installBinary, _ := cmd.Flags().GetBool("install")
	dryRun, _ := cmd.Flags().GetBool("dry-run")

	printHeader("Build PiTrac")
	fmt.Printf("src_dir:    %s\n", srcDir)
	fmt.Printf("build_dir:  %s\n", buildDir)
	fmt.Printf("build_type: %s\n", buildType)
	fmt.Printf("jobs:       %d\n", jobs)
	fmt.Printf("dry_run:    %v\n\n", dryRun)

	if clean {
		printStep(1, 3, "clean build directory")
		if dryRun {
			fmt.Printf("      rm -rf %s\n", buildDir)
			fmt.Println("      [DRY] skipped")
		} else {
			if err := os.RemoveAll(buildDir); err != nil {
				return fmt.Errorf("failed to clean build dir: %w", err)
			}
			fmt.Printf("      %s removed %s\n", markSuccess(), buildDir)
		}
	}

	// meson setup
	mesonArgs := []string{
		"setup", buildDir,
		"--buildtype=" + buildType,
		"--prefix=/opt/pitrac",
	}
	if !clean {
		if _, err := os.Stat(buildDir); err == nil {
			mesonArgs = append(mesonArgs, "--reconfigure")
		}
	}

	stepNum := 1
	totalSteps := 2
	if clean {
		stepNum = 2
		totalSteps = 3
	}
	if runTest {
		totalSteps++
	}
	if installBinary {
		totalSteps++
	}

	printStep(stepNum, totalSteps, "meson setup")
	mesonCmd := append([]string{"meson"}, mesonArgs...)
	printCommand(mesonCmd)

	if dryRun {
		fmt.Println("      [DRY] skipped")
	} else {
		start := time.Now()
		c := exec.Command("meson", mesonArgs...)
		c.Dir = srcDir
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr
		if err := c.Run(); err != nil {
			return fmt.Errorf("meson setup failed: %w", err)
		}
		printDuration(time.Since(start))
	}

	// ninja build
	stepNum++
	ninjaArgs := []string{"-C", buildDir, fmt.Sprintf("-j%d", jobs)}

	printStep(stepNum, totalSteps, "ninja build")
	ninjaCmd := append([]string{"ninja"}, ninjaArgs...)
	printCommand(ninjaCmd)

	if dryRun {
		fmt.Println("      [DRY] skipped")
	} else {
		start := time.Now()
		c := exec.Command("ninja", ninjaArgs...)
		c.Stdout = os.Stdout
		c.Stderr = os.Stderr
		if err := c.Run(); err != nil {
			return fmt.Errorf("ninja build failed: %w", err)
		}
		printDuration(time.Since(start))
	}

	// optional test
	if runTest {
		stepNum++
		printStep(stepNum, totalSteps, "meson test")
		testArgs := []string{"test", "-C", buildDir, "--print-errorlogs"}
		testCmd := append([]string{"meson"}, testArgs...)
		printCommand(testCmd)

		if dryRun {
			fmt.Println("      [DRY] skipped")
		} else {
			start := time.Now()
			c := exec.Command("meson", testArgs...)
			c.Stdout = os.Stdout
			c.Stderr = os.Stderr
			if err := c.Run(); err != nil {
				return fmt.Errorf("meson test failed: %w", err)
			}
			printDuration(time.Since(start))
		}
	}

	// optional install
	if installBinary {
		stepNum++
		binaryPath := filepath.Join(buildDir, "pitrac_lm")
		installDir := "/usr/lib/pitrac"
		installPath := filepath.Join(installDir, "pitrac_lm")

		printStep(stepNum, totalSteps, "install binary to "+installDir)
		installCmds := [][]string{
			{"sudo", "mkdir", "-p", installDir},
			{"sudo", "cp", binaryPath, installPath},
		}
		for _, ic := range installCmds {
			printCommand(ic)
		}

		if dryRun {
			fmt.Println("      [DRY] skipped")
		} else {
			start := time.Now()
			for _, ic := range installCmds {
				c := exec.Command(ic[0], ic[1:]...)
				c.Stdout = os.Stdout
				c.Stderr = os.Stderr
				if err := c.Run(); err != nil {
					return fmt.Errorf("install failed (%s): %w", formatCommand(ic), err)
				}
			}
			printDuration(time.Since(start))
		}
	}

	fmt.Println()
	printStatus(markSuccess(), "build", "complete")
	return nil
}
