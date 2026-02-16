package cmd

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
)

type imx296Sensor struct {
	MediaDevice int
	I2CBus      int
}

var imx296Pattern = regexp.MustCompile(`imx296\s+(\d+)-001a`)

func detectPiModel() string {
	data, err := os.ReadFile("/proc/device-tree/model")
	if err != nil {
		return ""
	}
	model := strings.ReplaceAll(string(data), "\x00", "")
	switch {
	case strings.Contains(model, "Raspberry Pi 5"):
		return "pi5"
	case strings.Contains(model, "Raspberry Pi 4"):
		return "pi4"
	default:
		return ""
	}
}

func discoverIMX296Sensors(maxMedia int) ([]imx296Sensor, error) {
	sensors := make([]imx296Sensor, 0)
	seen := make(map[string]struct{})

	for m := 0; m <= maxMedia; m++ {
		mediaPath := fmt.Sprintf("/dev/media%d", m)
		if !fileExists(mediaPath) {
			continue
		}
		out, err := exec.Command("media-ctl", "-d", mediaPath, "--print-dot").CombinedOutput()
		if err != nil {
			continue
		}

		matches := imx296Pattern.FindAllStringSubmatch(string(out), -1)
		for _, match := range matches {
			if len(match) < 2 {
				continue
			}
			bus := strings.TrimSpace(match[1])
			key := fmt.Sprintf("%d:%s", m, bus)
			if _, ok := seen[key]; ok {
				continue
			}
			seen[key] = struct{}{}

			var busNum int
			if _, err := fmt.Sscanf(bus, "%d", &busNum); err != nil {
				continue
			}
			sensors = append(sensors, imx296Sensor{MediaDevice: m, I2CBus: busNum})
		}
	}

	sort.Slice(sensors, func(i, j int) bool {
		if sensors[i].MediaDevice == sensors[j].MediaDevice {
			return sensors[i].I2CBus < sensors[j].I2CBus
		}
		return sensors[i].MediaDevice < sensors[j].MediaDevice
	})

	return sensors, nil
}

func libcameraTuningDirsForCurrentPi() []string {
	switch detectPiModel() {
	case "pi5":
		return []string{
			"/usr/share/libcamera/ipa/rpi/pisp",
			"/usr/local/share/libcamera/ipa/rpi/pisp",
		}
	case "pi4":
		return []string{
			"/usr/share/libcamera/ipa/rpi/vc4",
			"/usr/local/share/libcamera/ipa/rpi/vc4",
		}
	default:
		return nil
	}
}

func directoryExists(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.IsDir()
}

func sha256OfFile(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func syncCameraTuningFiles(repoRoot string, fileNames []string) error {
	dirs := libcameraTuningDirsForCurrentPi()
	if len(dirs) == 0 {
		return nil
	}

	for _, name := range fileNames {
		src := filepath.Join(repoRoot, "assets", "CameraTools", name)
		if !fileExists(src) {
			continue
		}
		for _, dir := range dirs {
			if !directoryExists(dir) {
				continue
			}
			dst := filepath.Join(dir, name)
			cmd := exec.Command("sudo", "install", "-m", "0644", src, dst)
			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("failed to sync %s -> %s: %w (%s)", src, dst, err, strings.TrimSpace(string(out)))
			}
		}
	}
	return nil
}

func cameraDoctorChecks(repoRoot string) []checkResult {
	results := make([]checkResult, 0)

	piModel := detectPiModel()
	if piModel == "" {
		return results
	}

	if path, err := exec.LookPath("media-ctl"); err == nil {
		results = append(results, checkResult{
			name:     "tool:media-ctl",
			required: true,
			ok:       true,
			detail:   path,
		})
	} else {
		results = append(results, checkResult{
			name:     "tool:media-ctl",
			required: true,
			ok:       false,
			detail:   "not found in PATH",
		})
		return results
	}

	sensors, err := discoverIMX296Sensors(5)
	if err != nil {
		results = append(results, checkResult{
			name:     "camera:imx296-discovery",
			required: true,
			ok:       false,
			detail:   err.Error(),
		})
		return results
	}
	if len(sensors) == 0 {
		results = append(results, checkResult{
			name:     "camera:imx296-discovery",
			required: true,
			ok:       false,
			detail:   "no IMX296 sensor found via media-ctl",
		})
		return results
	}

	detected := make([]string, 0, len(sensors))
	for _, s := range sensors {
		detected = append(detected, fmt.Sprintf("media%d->i2c-%d", s.MediaDevice, s.I2CBus))
		i2cDev := fmt.Sprintf("/dev/i2c-%d", s.I2CBus)
		results = append(results, checkResult{
			name:     fmt.Sprintf("camera:i2c-bus:%d", s.I2CBus),
			required: true,
			ok:       fileExists(i2cDev),
			detail:   i2cDev,
		})
	}
	results = append(results, checkResult{
		name:     "camera:imx296-discovery",
		required: true,
		ok:       true,
		detail:   strings.Join(detected, ", "),
	})

	tuningNames := []string{"imx296_noir.json", "imx296_mono.json"}
	tuningDirs := libcameraTuningDirsForCurrentPi()
	for _, name := range tuningNames {
		src := filepath.Join(repoRoot, "assets", "CameraTools", name)
		if !fileExists(src) {
			results = append(results, checkResult{
				name:     "camera:tuning-src:" + name,
				required: false,
				ok:       false,
				detail:   "missing in assets/CameraTools",
			})
			continue
		}

		srcHash, err := sha256OfFile(src)
		if err != nil {
			results = append(results, checkResult{
				name:     "camera:tuning-src:" + name,
				required: true,
				ok:       false,
				detail:   err.Error(),
			})
			continue
		}
		results = append(results, checkResult{
			name:     "camera:tuning-src:" + name,
			required: true,
			ok:       true,
			detail:   src,
		})

		for _, dir := range tuningDirs {
			if !directoryExists(dir) {
				continue
			}
			dst := filepath.Join(dir, name)
			ok := fileExists(dst)
			detail := "missing"
			if ok {
				dstHash, err := sha256OfFile(dst)
				if err != nil {
					ok = false
					detail = err.Error()
				} else if dstHash != srcHash {
					ok = false
					detail = "checksum mismatch with assets/CameraTools"
				} else {
					detail = "checksum match"
				}
			}
			results = append(results, checkResult{
				name:     fmt.Sprintf("camera:tuning:%s:%s", name, dir),
				required: true,
				ok:       ok,
				detail:   detail,
			})
		}
	}

	return results
}

func runCameraServicePreflight() error {
	piModel := detectPiModel()
	if piModel == "" {
		return nil
	}

	repoRoot, err := detectRepoRoot()
	if err != nil {
		if wd, wdErr := os.Getwd(); wdErr == nil {
			repoRoot = wd
		} else {
			return fmt.Errorf("unable to determine repo root: %w", err)
		}
	}

	printStatus(markInfo(), "camera-preflight", "syncing IMX296 tuning files...")
	if err := syncCameraTuningFiles(repoRoot, []string{"imx296_noir.json", "imx296_mono.json"}); err != nil {
		printStatus(markFailure(), "camera-preflight", "tuning sync failed")
		return err
	}
	printStatus(markSuccess(), "camera-preflight", "tuning files synced")

	sensors, err := discoverIMX296Sensors(5)
	if err != nil {
		printStatus(markFailure(), "camera-preflight", "IMX296 discovery failed")
		return err
	}
	if len(sensors) == 0 {
		printStatus(markFailure(), "camera-preflight", "no IMX296 sensors discovered")
		return fmt.Errorf("no IMX296 sensors found via media-ctl")
	}

	for _, s := range sensors {
		i2cDev := fmt.Sprintf("/dev/i2c-%d", s.I2CBus)
		if !fileExists(i2cDev) {
			printStatus(markFailure(), "camera-preflight", fmt.Sprintf("missing %s for media%d", i2cDev, s.MediaDevice))
			return fmt.Errorf("discovered media%d on i2c-%d but %s is missing", s.MediaDevice, s.I2CBus, i2cDev)
		}
	}

	pairs := make([]string, 0, len(sensors))
	for _, s := range sensors {
		pairs = append(pairs, fmt.Sprintf("media%d->i2c-%d", s.MediaDevice, s.I2CBus))
	}
	printStatus(markSuccess(), "camera-preflight", "detected "+strings.Join(pairs, ", "))

	return nil
}
