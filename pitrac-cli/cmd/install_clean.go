package cmd

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spf13/cobra"
)

type cleanTarget struct {
	id          string
	description string
	// paths returns the directories to remove. Each entry is expanded at
	// runtime so that $HOME and env-var overrides are honoured.
	paths func() []string
	// buildsOnly returns only the build sub-directories (keep source trees).
	buildsOnly func() []string
}

func buildCleanTargets() []cleanTarget {
	home, _ := os.UserHomeDir()
	opencvVer := envOrDefault("REQUIRED_OPENCV_VERSION", "4.12.0")
	ortVer := envOrDefault("ORT_VERSION", "1.17.3")

	return []cleanTarget{
		{
			id:          "opencv",
			description: fmt.Sprintf("OpenCV source + build (~%s)", opencvVer),
			paths: func() []string {
				return []string{
					filepath.Join(home, "opencv-"+opencvVer),
					filepath.Join(home, "opencv_contrib-"+opencvVer),
				}
			},
			buildsOnly: func() []string {
				return []string{
					filepath.Join(home, "opencv-"+opencvVer, "build"),
				}
			},
		},
		{
			id:          "onnx",
			description: fmt.Sprintf("ONNX Runtime source + build (~%s)", ortVer),
			paths: func() []string {
				return []string{
					filepath.Join(home, "src", "onnxruntime-"+ortVer),
				}
			},
			buildsOnly: func() []string {
				return []string{
					filepath.Join(home, "src", "onnxruntime-"+ortVer, "build"),
				}
			},
		},
		{
			id:          "libcamera",
			description: "libcamera build tree",
			paths: func() []string {
				return []string{
					envOrDefault("LIBCAMERA_WORK_DIR", "/tmp/pitrac-libcamera-build"),
				}
			},
			buildsOnly: func() []string {
				base := envOrDefault("LIBCAMERA_WORK_DIR", "/tmp/pitrac-libcamera-build")
				return []string{
					filepath.Join(base, "libcamera", "build"),
				}
			},
		},
		{
			id:          "rpicam",
			description: "rpicam-apps build tree",
			paths: func() []string {
				return []string{
					envOrDefault("RPICAM_WORK_DIR", "/tmp/pitrac-rpicam-build"),
				}
			},
			buildsOnly: func() []string {
				base := envOrDefault("RPICAM_WORK_DIR", "/tmp/pitrac-rpicam-build")
				return []string{
					filepath.Join(base, "rpicam-apps", "build"),
				}
			},
		},
		{
			id:          "pitrac",
			description: "PiTrac meson build directory (src/build)",
			paths: func() []string {
				root := os.Getenv("PITRAC_ROOT")
				if root == "" {
					if detected, err := detectRepoRoot(); err == nil {
						root = detected
					}
				}
				if root == "" {
					return nil
				}
				return []string{
					filepath.Join(root, "src", "build"),
				}
			},
			buildsOnly: func() []string {
				root := os.Getenv("PITRAC_ROOT")
				if root == "" {
					if detected, err := detectRepoRoot(); err == nil {
						root = detected
					}
				}
				if root == "" {
					return nil
				}
				return []string{
					filepath.Join(root, "src", "build"),
				}
			},
		},
	}
}

func init() {
	cleanCmd := &cobra.Command{
		Use:   "clean [target...]",
		Short: "Remove build artifacts and source trees from install targets",
		Long: `Remove build artifacts left behind by 'pitrac-cli install'.

Without arguments, lists what can be cleaned and how much space each uses.
With target names, removes those targets' build directories.

Targets: opencv, onnx, libcamera, rpicam, pitrac, all

Flags:
  --builds-only   Remove only build/ directories, keep source trees
  --yes           Skip confirmation prompt`,
		RunE: func(cmd *cobra.Command, args []string) error {
			dryRun, _ := cmd.Flags().GetBool("dry-run")
			yes, _ := cmd.Flags().GetBool("yes")
			buildsOnly, _ := cmd.Flags().GetBool("builds-only")

			if len(args) == 0 {
				return runCleanList()
			}
			return runClean(args, dryRun, yes, buildsOnly)
		},
	}
	cleanCmd.Flags().Bool("dry-run", false, "show what would be removed without deleting")
	cleanCmd.Flags().Bool("yes", false, "skip confirmation prompt")
	cleanCmd.Flags().Bool("builds-only", false, "remove only build directories, keep source trees")

	installCmd.AddCommand(cleanCmd)
}

func runCleanList() error {
	targets := buildCleanTargets()
	printHeader("Cleanable Build Artifacts")
	fmt.Println("Use: pitrac-cli install clean <target> [--builds-only] [--dry-run] [--yes]")
	fmt.Println()

	var totalBytes int64
	for _, t := range targets {
		paths := t.paths()
		var size int64
		var exists bool
		for _, p := range paths {
			if s, ok := dirSize(p); ok {
				size += s
				exists = true
			}
		}
		if exists {
			printStatus(markWarning(), t.id, fmt.Sprintf("%s  %s", formatSize(size), t.description))
			totalBytes += size
		} else {
			printStatus(markInfo(), t.id, fmt.Sprintf("%-10s %s", "(clean)", t.description))
		}
	}

	fmt.Printf("\n%s total reclaimable: %s\n", markInfo(), formatSize(totalBytes))
	fmt.Println()
	fmt.Println("Clean all:          pitrac-cli install clean all")
	fmt.Println("Builds only:        pitrac-cli install clean all --builds-only")
	fmt.Println("Specific target:    pitrac-cli install clean opencv onnx")
	fmt.Println()
	return nil
}

func runClean(args []string, dryRun, yes, buildsOnly bool) error {
	targets := buildCleanTargets()
	targetMap := make(map[string]cleanTarget, len(targets))
	for _, t := range targets {
		targetMap[t.id] = t
	}

	// Resolve which targets to clean
	var selected []cleanTarget
	for _, arg := range args {
		if arg == "all" {
			selected = targets
			break
		}
		t, ok := targetMap[arg]
		if !ok {
			return fmt.Errorf("unknown clean target: %s (available: opencv, onnx, libcamera, rpicam, pitrac, all)", arg)
		}
		selected = append(selected, t)
	}

	// Collect paths
	type removal struct {
		target string
		path   string
	}
	var removals []removal
	for _, t := range selected {
		var paths []string
		if buildsOnly {
			paths = t.buildsOnly()
		} else {
			paths = t.paths()
		}
		for _, p := range paths {
			if _, err := os.Stat(p); err == nil {
				removals = append(removals, removal{target: t.id, path: p})
			}
		}
	}

	if len(removals) == 0 {
		fmt.Println("Nothing to clean.")
		return nil
	}

	mode := "full"
	if buildsOnly {
		mode = "builds-only"
	}

	printHeader(fmt.Sprintf("Clean (%s)", mode))
	var totalSize int64
	for _, r := range removals {
		s, _ := dirSize(r.path)
		totalSize += s
		fmt.Printf("  %s %s %s\n", markWarning(), dimText(fmt.Sprintf("[%s]", r.target)), r.path)
	}
	fmt.Printf("\n  Total: %s across %d directories\n\n", formatSize(totalSize), len(removals))

	if dryRun {
		fmt.Println("  [DRY RUN] No files removed.")
		return nil
	}

	if !yes {
		if !confirmProceed("Remove these directories? [y/N]: ") {
			fmt.Println("Aborted.")
			return nil
		}
	}

	var failed []string
	for _, r := range removals {
		fmt.Printf("  Removing %s ...", r.path)
		if err := os.RemoveAll(r.path); err != nil {
			fmt.Printf(" %s %v\n", markFailure(), err)
			failed = append(failed, r.path)
		} else {
			fmt.Printf(" %s\n", markSuccess())
		}
	}

	fmt.Println()
	if len(failed) > 0 {
		printStatus(markWarning(), "clean", fmt.Sprintf("%d of %d removals failed", len(failed), len(removals)))
		return fmt.Errorf("%d removal(s) failed", len(failed))
	}
	printStatus(markSuccess(), "clean", fmt.Sprintf("reclaimed %s", formatSize(totalSize)))
	return nil
}

func dirSize(path string) (int64, bool) {
	info, err := os.Stat(path)
	if err != nil {
		return 0, false
	}
	if !info.IsDir() {
		return info.Size(), true
	}

	var size int64
	_ = filepath.Walk(path, func(_ string, info os.FileInfo, err error) error {
		if err != nil {
			return nil // skip unreadable entries
		}
		if !info.IsDir() {
			size += info.Size()
		}
		return nil
	})
	return size, true
}

func formatSize(bytes int64) string {
	const (
		kb = 1024
		mb = 1024 * kb
		gb = 1024 * mb
	)
	switch {
	case bytes >= gb:
		return fmt.Sprintf("%.1f GB", float64(bytes)/float64(gb))
	case bytes >= mb:
		return fmt.Sprintf("%.1f MB", float64(bytes)/float64(mb))
	case bytes >= kb:
		return fmt.Sprintf("%.1f KB", float64(bytes)/float64(kb))
	default:
		return fmt.Sprintf("%d B", bytes)
	}
}

func envOrDefault(key, fallback string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return fallback
}

// detectRepoRoot is defined in env.go
