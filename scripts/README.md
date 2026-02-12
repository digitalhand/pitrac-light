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
- `assets/diagram/module-dependencies.puml` - PlantUML component diagram (primary)
- `assets/module-dependencies.dot` - GraphViz DOT file (alternative)
- `assets/module-dependencies.svg` - Visual dependency graph (SVG)
- `assets/module-dependencies.png` - Visual dependency graph (PNG)
- `docs/DEPENDENCIES.md` - Human-readable dependency report

**What it analyzes:**
- Module-level dependencies (utils, Camera, ImageAnalysis, sim, core, etc.)
- Circular dependency detection
- Highly coupled modules
- Widely used modules

**Generating visual graphs:**

*PlantUML (recommended - matches project diagram standards):*
```bash
# Install PlantUML (if not already installed)
sudo apt install plantuml

# Generate PNG from PlantUML
plantuml assets/diagram/module-dependencies.puml

# Or use IDE plugins (VS Code: PlantUML extension)
```

*GraphViz (alternative):*
```bash
# Install GraphViz (if not already installed)
sudo apt install graphviz

# Generate SVG
dot -Tsvg assets/module-dependencies.dot -o assets/module-dependencies.svg

# Or PNG
dot -Tpng assets/module-dependencies.dot -o assets/module-dependencies.png
```

**Interpreting the results:**

*PlantUML diagram:*
- Modules grouped into packages: "Bounded Contexts", "Infrastructure", "Simulator Integration"
- Color-coded components with legend
- **Blue (Camera/ImageAnalysis):** Bounded contexts - should be isolated
- **Green (utils):** Utility modules - should be dependency-free
- **Yellow (sim):** Simulator integrations
- **Red (core/infrastructure):** Core launch monitor logic
- **Arrows:** Dependency relationships (A → B means "A depends on B")

*GraphViz diagram:*
- Flat layout with color-coded nodes (same color scheme as PlantUML)
- Left-to-right dependency flow (rankdir=LR)

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
- PlantUML (`sudo apt install plantuml`) - for PlantUML component diagrams (recommended)
- GraphViz (`sudo apt install graphviz`) - for GraphViz visual dependency graphs (alternative)

## See Also

- [`docs/DEPENDENCIES.md`](../docs/DEPENDENCIES.md) - Generated dependency report
- [`BUILD_SYSTEM.md`](../BUILD_SYSTEM.md) - Build system documentation
- [`CLAUDE.md`](../CLAUDE.md) - Contributor guidelines
