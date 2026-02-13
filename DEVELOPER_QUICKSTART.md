# PiTrac Developer Quick Start

**Get up and running with PiTrac development in 15 minutes.**

## Prerequisites

- Raspberry Pi 4/5 with Raspberry Pi OS
- Go 1.21+ (for pitrac-cli)
- Basic familiarity with C++, Meson, and Git

## Quick Setup

### 1. Clone and Install CLI (5 minutes)

```bash
# Clone repository
git clone https://github.com/digitalhand/pitrac-light.git
cd pitrac-light

# Install pitrac-cli (if not already installed)
cd pitrac-cli
go build -o pitrac-cli .
sudo install -m 0755 pitrac-cli /usr/local/bin/pitrac-cli
cd ..

# Verify installation
pitrac-cli --help
```

### 2. Install Dependencies (5 minutes)

```bash
# Check system readiness
pitrac-cli doctor

# Install all dependencies
pitrac-cli install full --yes

# Setup environment
pitrac-cli env setup --force
source ~/.bashrc

# Initialize config
pitrac-cli config init
```

### 3. Build and Test (5 minutes)

```bash
# Build project
cd src
meson setup build --buildtype=release --prefix=/opt/pitrac
ninja -C build -j4

# Run tests
meson test -C build --print-errorlogs

# Install git hooks (optional but recommended)
cd ..
./hooks/install.sh
```

**🎉 You're ready to develop!**

---

## Development Workflow

### Making Changes

```bash
# 1. Create feature branch
git checkout -b feature/my-feature

# 2. Make changes to code
vim src/my_module.cpp

# 3. Build incrementally
ninja -C src/build

# 4. Run relevant tests
meson test -C src/build "MyModule Tests" --print-errorlogs

# 5. Commit (hooks run automatically)
git add src/my_module.cpp
git commit -m "feat: Add awesome feature"

# 6. Push and create PR
git push origin feature/my-feature
```

### Running the System

```bash
# Start all services (broker + both cameras)
pitrac-cli service start

# Check status
pitrac-cli service status

# View logs
tail -f ~/.pitrac/logs/camera1.log

# Stop services
pitrac-cli service stop
```

### Testing Workflow

```bash
# Run all tests
meson test -C src/build --print-errorlogs

# Run specific test suites
meson test -C src/build --suite unit           # Fast unit tests
meson test -C src/build --suite integration    # Integration tests
meson test -C src/build --suite approval       # Regression tests

# Run bounded context tests
ctest --test-dir src/Camera/build --output-on-failure
ctest --test-dir src/ImageAnalysis/build --output-on-failure

# Generate coverage report
meson configure src/build -Db_coverage=true
meson test -C src/build
ninja -C src/build coverage-html
firefox src/build/meson-logs/coveragereport/index.html
```

---

## Common Tasks

### Adding a New Feature

**Example: Add a new utility function**

1. **Add function to appropriate module:**
   ```cpp
   // src/utils/cv_utils.h
   namespace golf_sim {
       struct CvUtils {
           static double MyNewFunction(double input);
       };
   }

   // src/utils/cv_utils.cpp
   double CvUtils::MyNewFunction(double input) {
       return input * 2.0;
   }
   ```

2. **Write tests:**
   ```cpp
   // src/tests/unit/test_cv_utils.cpp
   BOOST_AUTO_TEST_CASE(MyNewFunction_Doubles_Input) {
       double result = CvUtils::MyNewFunction(5.0);
       BOOST_CHECK_CLOSE(result, 10.0, 0.01);
   }
   ```

3. **Build and test:**
   ```bash
   ninja -C src/build
   meson test -C src/build "CvUtils Unit Tests"
   ```

4. **Commit:**
   ```bash
   git add src/utils/cv_utils.{h,cpp} src/tests/unit/test_cv_utils.cpp
   git commit -m "feat: Add MyNewFunction to CvUtils"
   ```

### Debugging a Test Failure

```bash
# 1. Run failing test with verbose output
meson test -C src/build "TestName" --verbose --print-errorlogs

# 2. Run test executable directly for more control
./src/build/test_my_module --log_level=all

# 3. Debug with gdb
gdb --args ./src/build/test_my_module

# 4. Check test logs
cat src/build/meson-logs/testlog.txt
```

### Updating Configuration

```bash
# 1. Edit config file
vim ~/.pitrac/config/golf_sim_config.json

# 2. Validate config
pitrac-cli validate config

# 3. Restart services to pick up changes
pitrac-cli service restart
```

---

## Key Files and Documentation

| File | Purpose |
|------|---------|
| **README.md** | User installation and getting started |
| **CLAUDE.md** | Contributor guidelines and coding standards |
| **BUILD_SYSTEM.md** | Build system reference (Meson vs CMake) |
| **CONFIG_MIGRATION_AUDIT.md** | Configuration migration plan |
| **src/tests/README.md** | Comprehensive testing guide |
| **hooks/README.md** | Git hooks documentation |
| **.github/workflows/ci.yml** | CI/CD pipeline configuration |

## Directory Structure

```
pitrac-light/
├── src/                           # Main source code
│   ├── tests/                     # Test infrastructure
│   │   ├── unit/                  # Unit tests
│   │   ├── integration/           # Integration tests
│   │   └── approval/              # Approval tests
│   ├── utils/                     # Utility modules
│   ├── Camera/                    # Camera bounded context (CMake)
│   ├── ImageAnalysis/             # Image analysis bounded context (CMake)
│   ├── sim/                       # Simulator integrations
│   └── meson.build                # Main build file
├── test_data/                     # Test images and baselines
├── hooks/                         # Git hooks
├── .github/workflows/             # CI/CD configuration
└── pitrac-cli/                    # CLI tool source
```

---

## Troubleshooting

### Build fails with "dependency not found"

```bash
# Reinstall dependencies
pitrac-cli install full --yes

# Verify installation
pitrac-cli validate install
```

### Tests fail with "test data not found"

```bash
# Create test data directory
mkdir -p test_data/images

# Add test images (copy from existing PiTrac installation)
cp ~/.pitrac/test_images/* test_data/images/
```

### Git hooks don't run

```bash
# Reinstall hooks
./hooks/install.sh

# Verify installation
ls -la .git/hooks/pre-commit
```

### Incremental build not working

```bash
# Clean and rebuild
rm -rf src/build
cd src
meson setup build --buildtype=release
ninja -C build
```

---

## Getting Help

- **Documentation:** Check the files in the table above
- **Issues:** [GitHub Issues](https://github.com/digitalhand/pitrac-light/issues)
- **CI Status:** [GitHub Actions](https://github.com/digitalhand/pitrac-light/actions)
- **PRs:** [Pull Requests](https://github.com/digitalhand/pitrac-light/pulls)

---

## Next Steps

After completing this quick start:

1. ✅ **Read CLAUDE.md** - Understand coding standards and contribution workflow
2. ✅ **Read BUILD_SYSTEM.md** - Learn when to use Meson vs CMake
3. ✅ **Read src/tests/README.md** - Master the testing infrastructure
4. ✅ **Pick an issue** - Find a "good first issue" and start contributing!

---

**Last Updated:** 2025-02-12
**Feedback:** Open an issue if something in this guide doesn't work!
