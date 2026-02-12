#!/usr/bin/env python3
"""
PiTrac Dependency Analyzer

Analyzes #include dependencies across the codebase and generates:
- Dependency graph (GraphViz DOT format)
- Circular dependency detection
- Module dependency report
"""

import os
import re
import sys
from pathlib import Path
from collections import defaultdict
from typing import Dict, List, Set, Tuple

class DependencyAnalyzer:
    def __init__(self, src_dir: str):
        self.src_dir = Path(src_dir)
        self.dependencies: Dict[str, Set[str]] = defaultdict(set)
        self.module_dependencies: Dict[str, Set[str]] = defaultdict(set)
        self.modules: Set[str] = set()
        self.circular_deps: List[List[str]] = []

    def get_module_name(self, file_path: Path) -> str:
        """Get module name from file path."""
        rel_path = file_path.relative_to(self.src_dir)
        parts = rel_path.parts

        # Map to module names
        if len(parts) == 1:
            return "core"  # Root src/ files
        elif parts[0] in ['utils', 'Camera', 'ImageAnalysis']:
            return parts[0]
        elif parts[0] == 'sim':
            return 'sim' if len(parts) == 2 else f"sim/{parts[1]}"
        elif parts[0] in ['core', 'encoder', 'image', 'output', 'preview', 'post_processing_stages']:
            return parts[0]
        elif parts[0] == 'tests':
            return 'tests'
        else:
            return parts[0]

    def parse_includes(self, file_path: Path) -> Set[str]:
        """Parse #include statements from a file."""
        includes = set()

        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    # Match #include "..." or #include <...>
                    match = re.match(r'\s*#include\s+[<"]([^>"]+)[>"]', line)
                    if match:
                        include = match.group(1)
                        # Keep non-absolute includes. Resolution happens later.
                        if not include.startswith('/') and not re.match(r'^[A-Za-z]:[\\/]', include):
                            includes.add(include)
        except Exception as e:
            print(f"Warning: Could not parse {file_path}: {e}", file=sys.stderr)

        return includes

    def build_include_index(self, source_files: List[Path]) -> Tuple[Dict[str, str], Dict[str, Set[str]]]:
        """Build lookup tables for resolving include paths to modules."""
        include_to_module: Dict[str, str] = {}
        file_name_to_modules: Dict[str, Set[str]] = defaultdict(set)

        for file_path in source_files:
            rel_path = file_path.relative_to(self.src_dir).as_posix()
            module = self.get_module_name(file_path)

            include_to_module[rel_path] = module
            file_name_to_modules[file_path.name].add(module)

        return include_to_module, file_name_to_modules

    def resolve_include_module(
        self,
        include: str,
        include_to_module: Dict[str, str],
        file_name_to_modules: Dict[str, Set[str]]
    ) -> str | None:
        """Resolve an include string to a local module name."""
        normalized = include.replace('\\', '/')

        # Exact relative path include (e.g. "core/options.hpp").
        if normalized in include_to_module:
            return include_to_module[normalized]

        # Bare include by filename (e.g. "options.hpp"), only if unambiguous.
        include_name = Path(normalized).name
        modules = file_name_to_modules.get(include_name, set())
        if len(modules) == 1:
            return next(iter(modules))

        # Fallback for module-prefixed includes when file is missing from scan.
        parts = normalized.split('/')
        if parts:
            if parts[0] == 'sim' and len(parts) > 1:
                sim_module = f"sim/{parts[1]}"
                if sim_module in self.modules:
                    return sim_module
            if parts[0] in self.modules:
                return parts[0]

        return None

    def analyze(self):
        """Analyze all source files and build dependency graph."""
        # Find all source files
        source_files = []
        for ext in ['*.cpp', '*.h', '*.hpp']:
            source_files.extend(self.src_dir.rglob(ext))

        # Exclude build directories
        source_files = [f for f in source_files if 'build' not in f.parts]
        self.modules = {self.get_module_name(file_path) for file_path in source_files}
        for module in self.modules:
            self.module_dependencies[module]
        include_to_module, file_name_to_modules = self.build_include_index(source_files)

        print(f"Analyzing {len(source_files)} source files...")

        # Build file-level dependencies
        for file_path in source_files:
            file_name = file_path.name
            includes = self.parse_includes(file_path)
            self.dependencies[file_name] = includes

            # Build module-level dependencies
            from_module = self.get_module_name(file_path)
            for include in includes:
                to_module = self.resolve_include_module(include, include_to_module, file_name_to_modules)
                if to_module and from_module != to_module:
                    self.module_dependencies[from_module].add(to_module)

        print(f"Found {len(self.dependencies)} files with dependencies")
        print(f"Found {len(self.modules)} modules")

    def detect_circular_dependencies(self) -> List[List[str]]:
        """Detect circular dependencies using DFS."""
        def dfs(node: str, visited: Set[str], path: List[str]) -> bool:
            if node in path:
                # Found cycle
                cycle_start = path.index(node)
                cycle = path[cycle_start:] + [node]
                if cycle not in self.circular_deps:
                    self.circular_deps.append(cycle)
                return True

            if node in visited:
                return False

            visited.add(node)
            path.append(node)

            for dep in self.module_dependencies.get(node, []):
                dfs(dep, visited, path.copy())

            return False

        visited = set()
        for module in sorted(self.modules):
            dfs(module, visited, [])

        return self.circular_deps

    def generate_dot_graph(self, output_file: str):
        """Generate GraphViz DOT file for module dependencies."""
        with open(output_file, 'w') as f:
            f.write('digraph PiTracDependencies {\n')
            f.write('    rankdir=LR;\n')
            f.write('    node [shape=box, style=rounded];\n')
            f.write('    \n')

            # Define module colors
            colors = {
                'core': '#FFE5E5',
                'utils': '#E5F5E5',
                'Camera': '#E5E5FF',
                'ImageAnalysis': '#FFE5FF',
                'sim': '#FFFFE5',
                'sim/common': '#FFFFD0',
                'sim/gspro': '#FFFFD0',
                'tests': '#F0F0F0',
                'encoder': '#FFE5E5',
                'image': '#FFE5E5',
                'output': '#FFE5E5',
                'preview': '#FFE5E5',
                'post_processing_stages': '#FFE5E5',
            }

            # Add nodes
            for module in sorted(self.modules):
                color = colors.get(module, '#FFFFFF')
                f.write(f'    "{module}" [fillcolor="{color}", style="filled,rounded"];\n')

            f.write('\n')

            # Add edges
            for from_module, to_modules in sorted(self.module_dependencies.items()):
                for to_module in sorted(to_modules):
                    f.write(f'    "{from_module}" -> "{to_module}";\n')

            f.write('}\n')

        print(f"Generated DOT graph: {output_file}")

    def generate_plantuml_diagram(self, output_file: str):
        """Generate PlantUML component diagram for module dependencies."""
        with open(output_file, 'w') as f:
            f.write('@startuml PiTrac Module Dependencies\n')
            f.write('!theme plain\n')
            f.write('skinparam componentStyle rectangle\n')
            f.write('skinparam backgroundColor white\n')
            f.write('skinparam defaultTextAlignment center\n')
            f.write('\n')
            f.write('title PiTrac Launch Monitor - Module Dependencies\n')
            f.write('\n')

            # Define module colors and stereotypes
            module_styles = {
                'core': ('Technology', '#FFE5E5'),
                'utils': ('Utility', '#E5F5E5'),
                'Camera': ('BoundedContext', '#E5E5FF'),
                'ImageAnalysis': ('BoundedContext', '#FFE5FF'),
                'sim': ('Integration', '#FFFFE5'),
                'sim/common': ('Integration', '#FFFFD0'),
                'sim/gspro': ('Integration', '#FFFFD0'),
                'tests': ('Testing', '#F0F0F0'),
                'encoder': ('Infrastructure', '#FFE5E5'),
                'image': ('Infrastructure', '#FFE5E5'),
                'output': ('Infrastructure', '#FFE5E5'),
                'preview': ('Infrastructure', '#FFE5E5'),
                'post_processing_stages': ('Processing', '#FFE5E5'),
            }

            # Add components with colors
            f.write('package "PiTrac Launch Monitor" {\n')

            # Group by type
            bounded_contexts = []
            infrastructure = []
            integration = []
            other = []

            for module in sorted(self.modules):
                stereotype, color = module_styles.get(module, ('Component', '#FFFFFF'))

                if stereotype == 'BoundedContext':
                    bounded_contexts.append((module, color))
                elif stereotype in ['Infrastructure', 'Processing']:
                    infrastructure.append((module, color))
                elif stereotype == 'Integration':
                    integration.append((module, color))
                else:
                    other.append((module, color))

            # Bounded Contexts
            if bounded_contexts:
                f.write('\n  package "Bounded Contexts" #EEEEEE {\n')
                for module, color in bounded_contexts:
                    clean_name = module.replace('/', '_')
                    f.write(f'    component [{module}] as {clean_name} {color}\n')
                f.write('  }\n')

            # Infrastructure
            if infrastructure:
                f.write('\n  package "Infrastructure" #EEEEEE {\n')
                for module, color in infrastructure:
                    clean_name = module.replace('/', '_')
                    f.write(f'    component [{module}] as {clean_name} {color}\n')
                f.write('  }\n')

            # Integration
            if integration:
                f.write('\n  package "Simulator Integration" #EEEEEE {\n')
                for module, color in integration:
                    clean_name = module.replace('/', '_')
                    f.write(f'    component [{module}] as {clean_name} {color}\n')
                f.write('  }\n')

            # Other modules
            if other:
                f.write('\n')
                for module, color in other:
                    clean_name = module.replace('/', '_')
                    f.write(f'  component [{module}] as {clean_name} {color}\n')

            f.write('}\n\n')

            # Add dependencies
            for from_module, to_modules in sorted(self.module_dependencies.items()):
                from_clean = from_module.replace('/', '_')
                for to_module in sorted(to_modules):
                    to_clean = to_module.replace('/', '_')
                    f.write(f'{from_clean} --> {to_clean}\n')

            f.write('\n')

            # Add legend
            f.write('legend right\n')
            f.write('  |= Color |= Type |\n')
            f.write('  | <#E5E5FF> | Bounded Context |\n')
            f.write('  | <#E5F5E5> | Utilities |\n')
            f.write('  | <#FFE5E5> | Infrastructure |\n')
            f.write('  | <#FFFFD0> | Simulator Integration |\n')
            f.write('  | <#F0F0F0> | Testing |\n')
            f.write('endlegend\n')

            f.write('\n@enduml\n')

        print(f"Generated PlantUML diagram: {output_file}")

    def generate_report(self, output_file: str):
        """Generate human-readable dependency report."""
        with open(output_file, 'w') as f:
            f.write("# PiTrac Module Dependencies\n\n")
            f.write(f"**Analysis Date:** {Path.cwd()}\n\n")
            f.write("---\n\n")

            # Summary
            f.write("## Summary\n\n")
            f.write(f"- **Total Files Analyzed:** {len(self.dependencies)}\n")
            f.write(f"- **Total Modules:** {len(self.modules)}\n")
            f.write(f"- **Circular Dependencies:** {len(self.circular_deps)}\n\n")

            # Module overview
            f.write("## Modules\n\n")
            f.write("| Module | Depends On | Dependents |\n")
            f.write("|--------|------------|------------|\n")

            # Calculate reverse dependencies
            reverse_deps = defaultdict(set)
            for from_mod, to_mods in self.module_dependencies.items():
                for to_mod in to_mods:
                    reverse_deps[to_mod].add(from_mod)

            for module in sorted(self.modules):
                deps = self.module_dependencies[module]
                rev_deps = reverse_deps.get(module, set())
                f.write(f"| {module} | {len(deps)} | {len(rev_deps)} |\n")

            f.write("\n")

            # Detailed dependencies
            f.write("## Detailed Module Dependencies\n\n")
            for module in sorted(self.modules):
                f.write(f"### {module}\n\n")

                deps = sorted(self.module_dependencies[module])
                if deps:
                    f.write("**Depends on:**\n")
                    for dep in deps:
                        f.write(f"- `{dep}`\n")
                else:
                    f.write("**No dependencies**\n")

                f.write("\n")

                rev_deps = sorted(reverse_deps.get(module, []))
                if rev_deps:
                    f.write("**Used by:**\n")
                    for dep in rev_deps:
                        f.write(f"- `{dep}`\n")
                else:
                    f.write("**Not used by other modules**\n")

                f.write("\n---\n\n")

            # Circular dependencies
            if self.circular_deps:
                f.write("## ⚠️ Circular Dependencies\n\n")
                f.write(f"Found **{len(self.circular_deps)}** circular dependency chains:\n\n")
                for i, cycle in enumerate(self.circular_deps, 1):
                    f.write(f"{i}. {' → '.join(cycle)}\n")
                f.write("\n")
                f.write("**Action Required:** Circular dependencies should be broken by:\n")
                f.write("- Introducing interfaces/abstractions\n")
                f.write("- Moving shared code to a common module\n")
                f.write("- Using dependency injection\n\n")
            else:
                f.write("## ✅ No Circular Dependencies\n\n")
                f.write("All module dependencies are acyclic.\n\n")

            # Recommendations
            f.write("## Recommendations\n\n")

            # Find highly coupled modules
            high_deps = [(m, len(deps)) for m, deps in self.module_dependencies.items() if len(deps) > 5]
            if high_deps:
                f.write("### Highly Coupled Modules\n\n")
                f.write("Modules with more than 5 dependencies:\n\n")
                for module, count in sorted(high_deps, key=lambda x: x[1], reverse=True):
                    f.write(f"- **{module}**: {count} dependencies\n")
                f.write("\n")
                f.write("Consider refactoring to reduce coupling.\n\n")

            # Find widely used modules
            high_users = [(m, len(deps)) for m, deps in reverse_deps.items() if len(deps) > 5]
            if high_users:
                f.write("### Widely Used Modules\n\n")
                f.write("Modules used by more than 5 other modules:\n\n")
                for module, count in sorted(high_users, key=lambda x: x[1], reverse=True):
                    f.write(f"- **{module}**: used by {count} modules\n")
                f.write("\n")
                f.write("These are good candidates for:\n")
                f.write("- Comprehensive testing\n")
                f.write("- API stability\n")
                f.write("- Documentation\n\n")

        print(f"Generated report: {output_file}")

def main():
    src_dir = Path(__file__).parent.parent / 'src'

    if not src_dir.exists():
        print(f"Error: Source directory not found: {src_dir}")
        sys.exit(1)

    analyzer = DependencyAnalyzer(str(src_dir))
    analyzer.analyze()
    analyzer.detect_circular_dependencies()

    # Generate outputs
    docs_dir = Path(__file__).parent.parent / 'docs'
    docs_dir.mkdir(exist_ok=True)

    assets_dir = Path(__file__).parent.parent / 'assets'
    assets_dir.mkdir(exist_ok=True)

    diagram_dir = assets_dir / 'diagram'
    diagram_dir.mkdir(exist_ok=True)

    # Generate all formats
    analyzer.generate_dot_graph(str(assets_dir / 'module-dependencies.dot'))
    analyzer.generate_plantuml_diagram(str(diagram_dir / 'module-dependencies.puml'))
    analyzer.generate_report(str(docs_dir / 'DEPENDENCIES.md'))

    print("\n✅ Dependency analysis complete!")
    print(f"   - PlantUML diagram: assets/diagram/module-dependencies.puml")
    print(f"   - DOT graph: assets/module-dependencies.dot")
    print(f"   - Report: docs/DEPENDENCIES.md")
    print("\nTo generate PlantUML images:")
    print("   plantuml assets/diagram/module-dependencies.puml")
    print("   (or use PlantUML plugin in IDE)")
    print("\nTo generate GraphViz images:")
    print("   dot -Tsvg assets/module-dependencies.dot -o assets/module-dependencies.svg")
    print("   dot -Tpng assets/module-dependencies.dot -o assets/module-dependencies.png")

if __name__ == '__main__':
    main()
