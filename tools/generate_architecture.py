"""Generate architecture figures from CMake Graphviz and Doxygen XML.

This is documentation tooling, never invoked by the application build.
Tree-sitter dependencies are used only by this documentation generator.
"""

import argparse
import json
import re
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
import html

from architecture_analysis import Analysis


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


def paired_graph(nodes, edges):
    project = {node for node in nodes if internal(node)}
    def pair(name):
        path = Path(name)
        if path.suffix in (".h", ".cpp") and str(path.with_suffix(
                ".cpp" if path.suffix == ".h" else ".h")).replace("\\", "/") in project:
            return path.with_suffix("").as_posix()
        return name
    return ({pair(node) for node in project},
            {(pair(source), pair(target)) for source, target in edges
             if source in project and target in project and pair(source) != pair(target)})


def write_graph(path, nodes, edges, groups=None, labels=None):
    lines = ["digraph dependencies {", 'rankdir=LR;',
             'graph [fontname="Arial"];', 'node [shape=box, fontname="Arial", style=filled];',
             'edge [color="#64748b"];']
    def vertex(node):
        color = INTERNAL if groups is not None or internal(node) else EXTERNAL
        return f"{quote(node)} [label={quote((labels or {}).get(node, node))}, fillcolor={quote(color)}];"
    if groups is None:
        lines.extend(vertex(node) for node in sorted(nodes))
    else:
        for index, directory in enumerate(sorted(set(groups.values()))):
            lines.append(f"subgraph cluster_{index} {{")
            lines.append(f"label={quote(directory)};")
            lines.extend(vertex(node) for node in sorted(nodes) if groups[node] == directory)
            lines.append("}")
    for source, target in sorted(edges):
        lines.append(f"{quote(source)} -> {quote(target)};")
    lines.append("}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def target_sources(build):
    replies = build / ".cmake/api/v1/reply"
    index = json.loads(max(replies.glob("index-*.json"), key=lambda path: path.stat().st_mtime_ns).read_text(encoding="utf-8"))
    model_file = next(item["jsonFile"] for item in index["objects"] if item["kind"] == "codemodel")
    model = json.loads((replies / model_file).read_text(encoding="utf-8"))
    result = {}
    for configuration in model["configurations"]:
        for reference in configuration["targets"]:
            name = reference["name"]
            if not re.fullmatch(r"smoothmail(?:_[a-z_]+)?", name) or name in ("smoothmail_package", "smoothmail_qt_sbom_audit"):
                continue
            target = json.loads((replies / reference["jsonFile"]).read_text(encoding="utf-8"))
            files = set()
            for item in target.get("sources", []):
                path = item["path"]
                if not item.get("isGenerated") and internal(source_name(path)):
                    files.add(source_name(path))
            if files:
                result.setdefault(name, set()).update(files)
    if not result:
        raise ValueError("CMake file API did not report application target sources")
    return result


def source_tables(targets, analysis):
    def cell(value):
        return html.escape(value).replace("|", "&#124;").replace("\n", " ")
    lines = ["<!-- BEGIN GENERATED SOURCE TABLES -->", ""]
    for target, sources in sorted(targets.items()):
        lines.extend([f"### `{target}`", "", "| ソースファイル | 定義クラス | 定義グローバル関数 | includeヘッダ |",
                      "|---|---|---|---|"])
        for source in sorted(sources):
            entries = analysis.files.get(source, {"classes": [], "functions": [], "includes": []})
            columns = [sorted(entries[key]) for key in ("classes", "functions", "includes")]
            for row in range(max(1, *(len(column) for column in columns))):
                values = [cell(source) if row == 0 else ""]
                values.extend(cell(column[row]) if row < len(column) else "" for column in columns)
                lines.append("| " + " | ".join(values) + " |")
        lines.append("")
    lines.append("<!-- END GENERATED SOURCE TABLES -->")
    return "\n".join(lines) + "\n"


def update_document(path, tables):
    content = path.read_text(encoding="utf-8")
    pattern = r"<!-- BEGIN GENERATED SOURCE TABLES -->.*?<!-- END GENERATED SOURCE TABLES -->"
    if len(re.findall(pattern, content, re.S)) != 1:
        raise ValueError("Architecture document must contain exactly one generated source-table block")
    path.write_bytes(re.sub(pattern, lambda match: tables.strip(), content, flags=re.S).encode("utf-8"))


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
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("--output must be a new directory; existing figures are never overwritten")
    dot = shutil.which(args.dot)
    doxygen = shutil.which(args.doxygen)
    tred = shutil.which(args.tred) if args.tred else shutil.which(str(Path(dot).with_name("tred" + Path(dot).suffix))) if dot else None
    unflatten = shutil.which(args.unflatten) if args.unflatten else shutil.which(
        str(Path(dot).with_name("unflatten" + Path(dot).suffix))) if dot else None
    if not all((dot, doxygen, tred, unflatten)):
        raise ValueError("Install Graphviz (dot, tred, unflatten) and Doxygen, or specify their executable paths")
    if args.document:
        # Validate markers before invoking generators, without writing anything.
        content = args.document.read_text(encoding="utf-8")
        if len(re.findall(r"<!-- BEGIN GENERATED SOURCE TABLES -->.*?<!-- END GENERATED SOURCE TABLES -->", content, re.S)) != 1:
            raise ValueError("Architecture document must contain exactly one generated source-table block")
    sources = {path.relative_to(ROOT).as_posix(): path.read_bytes()
               for path in (ROOT / "src").rglob("*") if path.suffix in (".cpp", ".h")}
    analysis = Analysis(sources)
    with tempfile.TemporaryDirectory(prefix="smoothmail-architecture-") as temporary:
        work = Path(temporary)
        # Configure in the supplied build without changing its test options.
        # Graphviz filters remove all test/probe/helper targets, not their build.
        options = build / "CMakeGraphVizOptions.cmake"
        if options.exists() or options.is_symlink():
            raise ValueError("Existing CMakeGraphVizOptions.cmake must not be overwritten")
        options_text = (
            'set(GRAPHVIZ_EXECUTABLES TRUE)\n'
            'set(GRAPHVIZ_CUSTOM_TARGETS FALSE)\n'
            'set(GRAPHVIZ_GENERATE_PER_TARGET FALSE)\n'
            'set(GRAPHVIZ_GENERATE_DEPENDERS FALSE)\n'
            'set(GRAPHVIZ_IGNORE_TARGETS "tst_.*;.*[Tt]est.*;.*probe.*;smoothmail_package;'
            'smoothmail_qt_sbom_audit;portable.*;.*helper.*;Qt6::Test")\n')
        query = build / ".cmake/api/v1/query/client-smoothmail-docs/codemodel-v2"
        query.parent.mkdir(parents=True, exist_ok=True)
        query_created = not query.exists()
        options.write_text(options_text, encoding="utf-8")
        try:
            if query_created:
                query.touch(exist_ok=False)
            run("cmake", f"--graphviz={work / 'targets.raw.dot'}", build)
        finally:
            options.unlink()
            if query_created and query.exists():
                query.unlink()
        targets = target_sources(build)
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
            'SEARCH_INCLUDES = YES\nFULL_PATH_NAMES = YES\n'
            'EXTRACT_PRIVATE = YES\nEXTRACT_STATIC = YES\nEXTRACT_LOCAL_CLASSES = YES\n'
            'SOURCE_BROWSER = YES\nREFERENCES_RELATION = YES\nREFERENCED_BY_RELATION = YES\n',
            encoding="utf-8")
        run(doxygen, config)
        nodes, edges = source_graph(work / "doxygen/xml")
        project_nodes, project_edges = paired_graph(nodes, edges)
        write_graph(work / "files.raw.dot", project_nodes, project_edges,
                    {node: Path(node).parent.as_posix() for node in project_nodes})
        write_graph(work / "directories.dot", *directory_graph(nodes, edges))
        analysis.add_doxygen(work / "doxygen/xml", source_name)
        classes, class_edges = analysis.class_graph()
        write_graph(work / "classes.raw.dot", classes, class_edges,
                    {node: Path(analysis.classes[node]).parent.as_posix() for node in classes},
                    {node: node.rsplit("::", 1)[-1] for node in classes})
        for name in ("files", "classes"):
            with (work / f"{name}.reduced.dot").open("wb") as output:
                subprocess.run([tred, str(work / f"{name}.raw.dot")], stdout=output, check=True)
            run(unflatten, "-l", "3", "-f", "-o", work / f"{name}.dot", work / f"{name}.reduced.dot")
        tables = source_tables(targets, analysis)
        for name in ("targets", "files", "directories", "classes"):
            run(dot, "-Tsvg", work / f"{name}.dot", "-o", work / f"{name}.svg")
        args.output.mkdir(parents=True)
        for name in ("targets", "files", "directories", "classes"):
            for extension in ("dot", "svg"):
                shutil.copyfile(work / f"{name}.{extension}", args.output / f"{name}.{extension}")
        (args.output / "tables.md").write_bytes(tables.encode("utf-8"))
        if args.document:
            update_document(args.document, tables)
        if analysis.errors:
            print("Tree-sitter syntax recovery used for: " + ", ".join(analysis.errors))
    print(f"Architecture figures: {args.output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dot", default="dot")
    parser.add_argument("--doxygen", default="doxygen")
    parser.add_argument("--tred")
    parser.add_argument("--unflatten")
    parser.add_argument("--document", type=Path, help="Update only the marked generated tables in architecture.md")
    args = parser.parse_args()
    try:
        generate(args)
    except (ValueError, OSError, subprocess.CalledProcessError, ET.ParseError) as error:
        parser.exit(1, f"Architecture generation failed: {error}\n")


if __name__ == "__main__":
    main()
