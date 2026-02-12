package cmd

import (
	"fmt"
	"os"
	"strings"
	"time"
)

const (
	ansiReset  = "\033[0m"
	ansiRed    = "\033[31m"
	ansiGreen  = "\033[32m"
	ansiYellow = "\033[33m"
	ansiCyan   = "\033[36m"
	ansiBold   = "\033[1m"
	ansiDim    = "\033[2m"
)

func useColor() bool {
	if os.Getenv("NO_COLOR") != "" {
		return false
	}

	term := os.Getenv("TERM")
	return term != "" && term != "dumb"
}

func colorize(color, text string) string {
	if !useColor() {
		return text
	}
	return color + text + ansiReset
}

func styleText(style, text string) string {
	if !useColor() {
		return text
	}
	return style + text + ansiReset
}

func markSuccess() string {
	return colorize(ansiGreen, "✓")
}

func markFailure() string {
	return colorize(ansiRed, "✗")
}

func markWarning() string {
	return colorize(ansiYellow, "!")
}

func markInfo() string {
	return colorize(ansiCyan, "•")
}

func headerText(text string) string {
	return styleText(ansiBold+ansiCyan, text)
}

func dimText(text string) string {
	return styleText(ansiDim, text)
}

func printStatus(mark, name, detail string) {
	fmt.Printf("%s %-34s %s\n", mark, name, detail)
}

func printHeader(title string) {
	fmt.Println(headerText(title))
	fmt.Println(dimText(strings.Repeat("─", len(title))))
}

func printSummaryBox(passed, warned, failed int) {
	total := passed + warned + failed
	parts := []string{
		colorize(ansiGreen, fmt.Sprintf("%d passed", passed)),
	}
	if warned > 0 {
		parts = append(parts, colorize(ansiYellow, fmt.Sprintf("%d warned", warned)))
	}
	if failed > 0 {
		parts = append(parts, colorize(ansiRed, fmt.Sprintf("%d failed", failed)))
	}
	fmt.Printf("\n%s  %s\n", dimText(fmt.Sprintf("[%d checks]", total)), strings.Join(parts, dimText(", ")))
}

func printStep(i, total int, name string) {
	fmt.Printf("%s %s\n",
		dimText(fmt.Sprintf("[%d/%d]", i, total)),
		styleText(ansiBold, name))
}

func printCommand(parts []string) {
	fmt.Printf("      %s\n", dimText(formatCommand(parts)))
}

func printDuration(d time.Duration) {
	var s string
	if d < time.Second {
		s = fmt.Sprintf("%dms", d.Milliseconds())
	} else if d < time.Minute {
		s = fmt.Sprintf("%.1fs", d.Seconds())
	} else {
		m := int(d.Minutes())
		sec := int(d.Seconds()) % 60
		s = fmt.Sprintf("%dm%02ds", m, sec)
	}
	fmt.Printf("      %s %s\n", markSuccess(), dimText(s))
}

// helpGroup defines a visual grouping for cobra help output.
type helpGroup struct {
	title    string
	commands []helpEntry
}

type helpEntry struct {
	name string
	desc string
}

func printGroupedHelp(groups []helpGroup) {
	for _, g := range groups {
		fmt.Printf("\n%s\n", headerText(g.title))
		for _, e := range g.commands {
			fmt.Printf("  %-24s %s\n", e.name, dimText(e.desc))
		}
	}
	fmt.Println()
}
