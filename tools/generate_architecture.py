"""Generate architecture figures from CMake Graphviz and Doxygen XML.

This is documentation tooling, never invoked by the application build.
Only the Python standard library is required.
"""

import argparse
import json
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INTERNAL = "#dbeafe"
EXTERNAL = "#ffedd5"


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def quote(value):
    return json.dumps(str(value), ensure_ascii=False)


def source_name(name):
    """Keep project paths portable; external include names remain external."""
    normalized = name.replace("\\", "/")
    prefix = ROOT.as_posix() + "/"
    if normalized.casefold().startswith(prefix.casefold()):
        return normalized[len(prefix):]
    return normalized


def internal(name):
    return name == "src" or name.startswith("src/")


def source_graph(xml_dir):
    nodes, edges = set(), set()
    for path in sorted(xml_dir.glob("*.xml")):
        for compound in ET.parse(path).getroot().findall("compounddef"):
            if compound.get("kind") != "file":
                continue
            location = compound.find("location")
            if location is None:
                continue
            source = source_name(location.get("file", ""))
            if not internal(source):
                continue
            nodes.add(source)
            for include in compound.findall("includes"):
                # Resolved project files have a refid; unresolved SDK headers
                # still belong in the diagram as external file nodes.
                target = source_name(include.text or "")
                ref = include.get("refid")
                if ref:
                    definition = ET.parse(xml_dir / f"{ref}.xml").getroot().find("compounddef/location")
                    if definition is not None:
                        target = source_name(definition.get("file", target))
                if not target:
                    continue
                nodes.add(target)
                edges.add((source, target))
    if not nodes or not edges:
        raise ValueError("Doxygen did not produce a source include graph")
    return nodes, edges


def directory_graph(nodes, edges):
    directories = {str(Path(node).parent).replace("\\", "/") for node in nodes if internal(node)}
    dependencies = set()
    for source, target in edges:
        if internal(source) and internal(target):
            pair = (str(Path(source).parent).replace("\\", "/"),
                    str(Path(target).parent).replace("\\", "/"))
            if pair[0] != pair[1]:
                dependencies.add(pair)
    return directories, dependencies


def write_graph(path, nodes, edges):
    lines = ["digraph dependencies {", 'rankdir=LR;',
             'graph [fontname="Arial"];', 'node [shape=box, fontname="Arial", style=filled];',
             'edge [color="#64748b"];']
    for node in sorted(nodes):
        color = INTERNAL if internal(node) else EXTERNAL
        lines.append(f"{quote(node)} [fillcolor={quote(color)}];")
    for source, target in sorted(edges):
        lines.append(f"{quote(source)} -> {quote(target)};")
    lines.append("}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def color_targets(text):
    # CMake's legend uses legendNode*, whereas real targets use node[0-9]+.
    text = re.sub(r"subgraph clusterLegend\s*\{.*?\n\s*\}", "", text, flags=re.S)
    def color(match):
        label = match.group(2).split("\\n", 1)[0]
        fill = INTERNAL if re.fullmatch(r"smoothmail(?:_[a-z_]+)?", label) else EXTERNAL
        return f'{match.group(1)}, style=filled, fillcolor="{fill}" ];'
    return re.sub(r'("node\d+"\s*\[\s*label\s*=\s*"([^"]+)"[^]]*)\]\s*;', color, text)


def generate(args):
    build = args.build.resolve()
    if not (build / "CMakeCache.txt").is_file():
        raise ValueError("--build must be an already configured application build")
    if args.output.exists():
        raise ValueError("--output must be a new directory; existing figures are never overwritten")
    dot = shutil.which(args.dot)
    doxygen = shutil.which(args.doxygen)
    if not dot or not doxygen:
        raise ValueError("Install Graphviz (dot) and Doxygen, or specify their executable paths")
    with tempfile.TemporaryDirectory(prefix="smoothmail-architecture-") as temporary:
        work = Path(temporary)
        # Configure in the supplied build without changing its test options.
        # Graphviz filters remove all test/probe/helper targets, not their build.
        options = build / "CMakeGraphVizOptions.cmake"
        if options.exists() or options.is_symlink():
            raise ValueError("Existing CMakeGraphVizOptions.cmake must not be overwritten")
        options.write_text(
            'set(GRAPHVIZ_EXECUTABLES TRUE)\n'
            'set(GRAPHVIZ_CUSTOM_TARGETS FALSE)\n'
            'set(GRAPHVIZ_GENERATE_PER_TARGET FALSE)\n'
            'set(GRAPHVIZ_GENERATE_DEPENDERS FALSE)\n'
            'set(GRAPHVIZ_IGNORE_TARGETS "tst_.*;.*[Tt]est.*;.*probe.*;smoothmail_package;'
            'smoothmail_qt_sbom_audit;portable.*;.*helper.*;Qt6::Test")\n',
            encoding="utf-8")
        try:
            run("cmake", f"--graphviz={work / 'targets.raw.dot'}", build)
        finally:
            options.unlink()
        (work / "targets.dot").write_text(
            color_targets((work / "targets.raw.dot").read_text(encoding="utf-8")),
            encoding="utf-8")
        config = work / "Doxyfile"
        config.write_text(
            f'PROJECT_NAME = SmoothMail\nOUTPUT_DIRECTORY = {quote(work / "doxygen")}\n'
            f'INPUT = {quote(ROOT / "src")}\n'
            f'INCLUDE_PATH = {quote(ROOT / "src")}\n'
            'FILE_PATTERNS = *.cpp *.h\nRECURSIVE = YES\nEXTRACT_ALL = YES\n'
            'GENERATE_XML = YES\nGENERATE_HTML = NO\nGENERATE_LATEX = NO\n'
            'QUIET = YES\nWARNINGS = NO\nENABLE_PREPROCESSING = YES\n'
            'SEARCH_INCLUDES = YES\nFULL_PATH_NAMES = YES\n',
            encoding="utf-8")
        run(doxygen, config)
        nodes, edges = source_graph(work / "doxygen/xml")
        write_graph(work / "files.dot", nodes, edges)
        write_graph(work / "directories.dot", *directory_graph(nodes, edges))
        for name in ("targets", "files", "directories"):
            run(dot, "-Tsvg", work / f"{name}.dot", "-o", work / f"{name}.svg")
        args.output.mkdir(parents=True)
        for name in ("targets", "files", "directories"):
            for extension in ("dot", "svg"):
                shutil.copyfile(work / f"{name}.{extension}", args.output / f"{name}.{extension}")
    print(f"Architecture figures: {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dot", default="dot")
    parser.add_argument("--doxygen", default="doxygen")
    args = parser.parse_args()
    try:
        generate(args)
    except (ValueError, OSError, subprocess.CalledProcessError, ET.ParseError) as error:
        parser.exit(1, f"Architecture generation failed: {error}\n")


if __name__ == "__main__":
    main()
