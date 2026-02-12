# PiTrac Analysis Scripts

Utility scripts for analyzing the PiTrac codebase.

## Available Scripts

### analyze_dependencies.py

Analyzes `#include` dependencies across the codebase and generates dependency reports.

**Usage:**
```bash
python3 scripts/analyze_dependencies.py
```

**Outputs:**
- `docs/module-dependencies.dot` - GraphViz DOT file
- `docs/module-dependencies.svg` - Visual dependency graph (requires GraphViz)
- `docs/DEPENDENCIES.md` - Human-readable dependency report

**What it analyzes:**
- Module-level dependencies (utils, Camera, ImageAnalysis, sim, core, etc.)
- Circular dependency detection
- Highly coupled modules
- Widely used modules

**Generating the visual graph:**
```bash
# Install GraphViz (if not already installed)
sudo apt install graphviz

# Generate SVG
dot -Tsvg docs/module-dependencies.dot -o docs/module-dependencies.svg

# Or PNG
dot -Tpng docs/module-dependencies.dot -o docs/module-dependencies.png
```

**Interpreting the results:**

- **Green boxes (utils):** Utility modules - should be dependency-free
- **Blue boxes (Camera/ImageAnalysis):** Bounded contexts - should be isolated
- **Yellow boxes (sim):** Simulator integrations
- **Red boxes (core):** Core launch monitor logic
- **Arrows:** Dependency relationships (A → B means "A depends on B")

## Adding New Scripts

When adding new analysis scripts:

1. Add script to `scripts/` directory
2. Make it executable: `chmod +x scripts/your_script.py`
3. Add shebang: `#!/usr/bin/env python3`
4. Document it in this README
5. Follow existing patterns for output (generate to `docs/`)

## Dependencies

**Required:**
- Python 3.6+

**Optional:**
- GraphViz (`sudo apt install graphviz`) - for visual dependency graphs

## See Also

- [`docs/DEPENDENCIES.md`](../docs/DEPENDENCIES.md) - Generated dependency report
- [`BUILD_SYSTEM.md`](../BUILD_SYSTEM.md) - Build system documentation
- [`CLAUDE.md`](../CLAUDE.md) - Contributor guidelines
