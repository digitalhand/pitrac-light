# PiTrac Git Hooks

Git hooks for the PiTrac launch monitor project that enforce code quality and testing standards.

## Available Hooks

### pre-commit

Runs before each commit to ensure code quality and prevent broken commits.

**Checks performed:**
1. ✅ **Trailing whitespace** - Prevents accidental whitespace
2. ✅ **Large file detection** - Warns about files >5MB (suggests Git LFS)
3. ✅ **Compilation check** - Ensures C++ code compiles (if build dir exists)
4. ✅ **Unit tests** - Runs fast unit tests for changed C++ files
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

3️⃣  C++ files changed, checking if build directory exists...
Building project...
✅ Build succeeded

4️⃣  Running unit tests...
✅ Unit tests passed

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

## Troubleshooting

### "Build failed" - but code compiles manually

```bash
# Rebuild to sync build directory
cd src
ninja -C build
```

### "Unit tests failed" - but tests pass manually

```bash
# Run tests manually to see full output
meson test -C src/build --suite unit --print-errorlogs
```

### Hook doesn't run

```bash
# Verify hook is installed and executable
ls -la .git/hooks/pre-commit

# Reinstall if missing
./hooks/install.sh
```

### Hook runs too slowly

The pre-commit hook is optimized to run only relevant checks:
- Only runs build/tests if C++ files changed
- Only runs unit tests (not integration or approval)
- Uses `--no-rebuild` for tests

If still slow:
- Check if incremental builds are working: `ninja -C src/build -t compdb`
- Ensure SSD is used for build directory
- Consider adjusting timeout in hook script

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
