import importlib.util
import tempfile
import unittest
import sys
import json
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("architecture", ROOT / "tools/generate_architecture.py")
architecture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(architecture)


class ArchitectureTests(unittest.TestCase):
    def test_source_pairing_filters_external_and_self_edges(self):
        nodes = {"src/core/a.h", "src/core/a.cpp", "src/ui/b.h", "QObject"}
        edges = {("src/core/a.cpp", "src/core/a.h"), ("src/core/a.h", "src/ui/b.h"),
                 ("src/ui/b.h", "QObject")}
        self.assertEqual(architecture.paired_graph(nodes, edges),
                         ({"src/core/a", "src/ui/b.h"}, {("src/core/a", "src/ui/b.h")}))

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

    def test_grouping_and_labels_keep_qualified_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "classes.dot"
            architecture.write_graph(path, {"one::A", "two::A"}, {("one::A", "two::A")},
                                     {"one::A": "src/a", "two::A": "src/b"},
                                     {"one::A": "A", "two::A": "A"})
            result = path.read_text(encoding="utf-8")
            self.assertIn('"one::A" -> "two::A"', result)
            self.assertEqual(result.count("subgraph cluster_"), 2)

    def test_document_update_is_bounded_and_requires_markers(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "architecture.md"
            path.write_text("before\n<!-- BEGIN GENERATED SOURCE TABLES -->old"
                            "<!-- END GENERATED SOURCE TABLES -->\nafter\n", encoding="utf-8")
            architecture.update_document(path, "<!-- BEGIN GENERATED SOURCE TABLES -->new"
                                         "<!-- END GENERATED SOURCE TABLES -->")
            self.assertTrue(path.read_text().startswith("before\n"))
            self.assertTrue(path.read_text().endswith("\nafter\n"))
            path.write_text("user content", encoding="utf-8")
            with self.assertRaises(ValueError):
                architecture.update_document(path, "replacement")
            self.assertEqual(path.read_text(), "user content")

    def test_cmake_file_api_excludes_tests_generated_and_external_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = Path(temporary)
            reply = build / ".cmake/api/v1/reply"
            reply.mkdir(parents=True)
            (reply / "index-1.json").write_text(json.dumps({"objects": [{"kind": "codemodel", "jsonFile": "model.json"}]}))
            (reply / "model.json").write_text(json.dumps({"configurations": [{"targets": [
                {"name": "smoothmail_ui", "jsonFile": "ui.json"},
                {"name": "tst_mainwindow", "jsonFile": "not-read.json"}]}]}))
            (reply / "ui.json").write_text(json.dumps({"sources": [
                {"path": "src/ui/mainwindow.cpp"}, {"path": "src/ui/mainwindow.h"},
                {"path": "src/ui/generated.cpp", "isGenerated": True},
                {"path": "tests/tst_mainwindow.cpp"}, {"path": "external/a.cpp"}]}))
            self.assertEqual(architecture.target_sources(build),
                             {"smoothmail_ui": {"src/ui/mainwindow.cpp", "src/ui/mainwindow.h"}})

    def test_source_table_one_item_per_row_and_markdown_escaping(self):
        analysis = SimpleNamespace(files={"src/a.h": {"classes": {"A", "B"}, "functions": {"f()"},
                                                        "includes": {"<vector>"}}})
        table = architecture.source_tables({"smoothmail": {"src/a.h"}}, analysis)
        self.assertIn("| src/a.h | A | f() | &lt;vector&gt; |", table)
        self.assertIn("|  | B |  |  |", table)


@unittest.skipUnless(importlib.util.find_spec("tree_sitter") and importlib.util.find_spec("tree_sitter_cpp"),
                     "optional architecture-generation dependencies are not installed")
class ClassAnalysisTests(unittest.TestCase):
    def test_signatures_variables_calls_and_global_function_chains(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/example.h": b"""
            namespace sample {
            class Return {}; class Argument {}; class Local {}; class Called { public: void run() {} };
            class Terminal {};
            void g() { Terminal value; }
            void f() { g(); }
            class A {
                Called member;
            public:
                Return method(const Argument &arg) { Local local; member.run(); f(); return {}; }
            };
            }
        """})
        nodes, edges = analysis.class_graph()
        self.assertEqual(len(nodes), 6)
        for target in ("Return", "Argument", "Local", "Called", "Terminal"):
            self.assertIn(("sample::A", "sample::" + target), edges)
        self.assertFalse(any(node.endswith(("::f", "::g")) for node in nodes))
        self.assertEqual(analysis.files["src/example.h"]["functions"], {"sample::f()", "sample::g()"})

    def test_namespace_identity_and_out_of_line_members(self):
        from architecture_analysis import Analysis
        analysis = Analysis({
            "src/a.h": b"namespace one { class B {}; class A { public: void run(); }; } namespace two { class B {}; }",
            "src/a.cpp": b"namespace one { void A::run() { B value; } }",
        })
        nodes, edges = analysis.class_graph()
        self.assertIn("two::B", nodes)
        self.assertIn(("one::A", "one::B"), edges)
        self.assertNotIn(("one::A", "two::B"), edges)

    def test_recursive_global_helpers_terminate(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/a.h": b"""
            class B {}; void g(); void f() { g(); } void g() { B b; f(); }
            class A { public: void run() { f(); } };
        """})
        self.assertIn(("A", "B"), analysis.class_graph()[1])

    def test_auto_factory_method_and_callback_alias(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/a.h": b"""
            class C {};
            class B { public: void run() { C c; } };
            B factory() { return {}; }
            class Argument {};
            class A {
                using Callback = std::function<void(const Argument &)>;
            public:
                void run(Callback callback) { auto object = factory(); object.run(); }
            };
        """})
        edges = analysis.class_graph()[1]
        self.assertIn(("A", "C"), edges)
        self.assertIn(("A", "Argument"), edges)

    def test_qt_normalization_preserves_original_parameter_text(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/a.h": b"""
            class A { Q_OBJECT
            signals:
                void changed();
            public slots:
                void run() {}
            };
            void f(const QString &value = {}) {}
            Q_DECLARE_METATYPE(A)
        """})
        self.assertEqual(analysis.errors, [])
        self.assertIn("f(const QString &value = {})", analysis.files["src/a.h"]["functions"])

    def test_signature_only_class_dependencies(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/a.h": b"class B {}; class C {}; class A { virtual B run(const C &) = 0; };"})
        self.assertTrue({("A", "B"), ("A", "C")} <= analysis.class_graph()[1])

    def test_doxygen_reference_augments_syntax_call_graph(self):
        from architecture_analysis import Analysis
        analysis = Analysis({"src/a.cpp": b"class B {}; void g() { B b; } class A { public: void run() {} };"})
        self.assertNotIn(("A", "B"), analysis.class_graph()[1])
        with tempfile.TemporaryDirectory() as temporary:
            xml = Path(temporary)
            (xml / "class.xml").write_text(
                '<doxygen><compounddef kind="class"><compoundname>A</compoundname><sectiondef>'
                '<memberdef id="run"><name>run</name><location file="src/a.cpp" bodyfile="src/a.cpp" bodystart="1"/>'
                '<references refid="global_g">g</references></memberdef></sectiondef></compounddef></doxygen>')
            (xml / "global.xml").write_text(
                '<doxygen><compounddef kind="file"><sectiondef><memberdef id="global_g"><name>g</name>'
                '<location file="src/a.cpp" bodyfile="src/a.cpp" bodystart="1"/>'
                '</memberdef></sectiondef></compounddef></doxygen>')
            analysis.add_doxygen(xml, lambda name: name)
            self.assertIn(("A", "B"), analysis.class_graph()[1])


if __name__ == "__main__":
    unittest.main()
