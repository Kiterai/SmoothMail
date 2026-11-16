import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("architecture", ROOT / "tools/generate_architecture.py")
architecture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(architecture)


class ArchitectureTests(unittest.TestCase):
    def test_target_colors_and_legend(self):
        text = ('digraph G {\nsubgraph clusterLegend {\nlegendNode0 [label="Executable"];\n}\n'
                '"node0" [ label = "smoothmail_ui", shape = octagon ];\n'
                '"node1" [ label = "Qt6::Widgets", shape = pentagon ];\n}')
        result = architecture.color_targets(text)
        self.assertNotIn("legendNode", result)
        self.assertIn('fillcolor="#dbeafe"', result)
        self.assertIn('fillcolor="#ffedd5"', result)

    def test_directory_graph_excludes_external_and_self_edges(self):
        nodes = {"src/ui/a.cpp", "src/ui/b.h", "src/core/c.h", "QtCore/QObject"}
        edges = {("src/ui/a.cpp", target) for target in nodes}
        self.assertEqual(architecture.directory_graph(nodes, edges),
                         ({"src/ui", "src/core"}, {("src/ui", "src/core")}))

    def test_doxygen_include_resolution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "a.xml").write_text(
                '<doxygen><compounddef kind="file"><location file="src/ui/a.cpp"/>'
                '<includes refid="b">b.h</includes><includes>QObject</includes>'
                '</compounddef></doxygen>', encoding="utf-8")
            (root / "b.xml").write_text(
                '<doxygen><compounddef kind="file"><location file="src/core/b.h"/>'
                '</compounddef></doxygen>', encoding="utf-8")
            nodes, edges = architecture.source_graph(root)
            self.assertEqual(nodes, {"src/ui/a.cpp", "src/core/b.h", "QObject"})
            self.assertIn(("src/ui/a.cpp", "src/core/b.h"), edges)


if __name__ == "__main__":
    unittest.main()
