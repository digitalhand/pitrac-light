# PiTrac Git Hooks

Git hooks for the PiTrac launch monitor project that enforce code quality and testing standards.

## Available Hooks

### pre-commit

Runs before each commit to ensure code quality and prevent broken commits.

**Checks performed:**
1. ✅ **Trailing whitespace** - Prevents accidental whitespace
2. ✅ **Large file detection** - Warns about files >5MB (suggests Git LFS)
3. ⏭️  **Compilation check** - DISABLED (build on Raspberry Pi target device)
4. ⏭️  **Unit tests** - DISABLED (test on Raspberry Pi target device)
5. ⚠️  **TODO comments** - Warns about TODOs without issue links
6. ⚠️  **SetConstant() migration** - Warns about new SetConstant() calls (we're migrating away)

## Installation

### Quick Install (Recommended)

```bash
# From repository root
./hooks/install.sh
```

This copies the hooks to `.git/hooks/` and makes them executable.

### Manual Install

```bash
cp hooks/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

## Usage

Once installed, the pre-commit hook runs automatically:

```bash
git commit -m "Your commit message"
```

**Output example:**
```
🔍 Running pre-commit checks...

1️⃣  Checking for trailing whitespace...
✅ No trailing whitespace

2️⃣  Checking for large files (>5MB)...

3️⃣  C++ files changed - build check skipped (build on target device)

4️⃣  Unit tests skipped (test on target device)

5️⃣  Checking for TODO comments without issue links...

6️⃣  Checking for new SetConstant() calls...

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
✅ All pre-commit checks passed!
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

## Bypassing Hooks

**Not recommended**, but sometimes necessary:

```bash
git commit --no-verify -m "Emergency fix"
```

Use `--no-verify` only when:
- Making urgent hotfixes
- Committing work-in-progress to a feature branch
- You've manually verified all checks pass

## Development Workflow

**Note:** Build and test checks are disabled in this hook because PiTrac is developed on a workstation but built/tested on the Raspberry Pi target device.

**Recommended workflow:**
1. Commit changes locally (pre-commit hook runs basic checks)
2. Push to repository or copy to Raspberry Pi
3. Build on Raspberry Pi: `meson setup build && ninja -C build`
4. Run tests on Raspberry Pi: `meson test -C build`
5. Validate with real hardware

This ensures the code is tested in the actual deployment environment.

## Troubleshooting

### Hook doesn't run

```bash
# Verify hook is installed and executable
ls -la .git/hooks/pre-commit

# Reinstall if missing
./hooks/install.sh
```

## Customization

Edit `hooks/pre-commit` to customize behavior:

**Skip specific checks:**
```bash
# Comment out unwanted sections
# Check 4: Run Unit Tests
# if [ -d "src/build" ] && [ -n "$changed_cpp_files" ]; then
#     ...
# fi
```

**Change timeout:**
```bash
# Add timeout to test command
timeout 60s meson test -C src/build --suite unit ...
```

**Add custom checks:**
```bash
# Add after Check 6
echo ""
echo "7️⃣  Running custom check..."
# Your custom logic here
```

## Uninstalling

```bash
rm .git/hooks/pre-commit
```

## CI Integration

These same checks run in CI via GitHub Actions (`.github/workflows/ci.yml`), but with additional checks:
- Integration tests
- Approval tests
- Coverage reporting
- Multi-configuration builds (debug/release)
- Documentation checks

## See Also

- [`src/tests/README.md`](../src/tests/README.md) - Full testing guide
- [`CLAUDE.md`](../CLAUDE.md) - Contributor guidelines
- [`.github/workflows/ci.yml`](../.github/workflows/ci.yml) - CI configuration

---

**Last Updated:** 2025-02-12
**Maintainer:** PiTrac Development Team
