import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import package_windows
from release_common import regular
from stage_windows_release import resolve


class ReleaseTests(unittest.TestCase):
    def test_preflight_never_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "existing.zip"
            output.write_bytes(b"user artifact")
            with self.assertRaisesRegex(ValueError, "already exists"):
                package_windows.preflight(root, root, output, root, root)
            self.assertEqual(output.read_bytes(), b"user artifact")

    def test_safe_evidence_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "text").write_text("license", encoding="utf-8")
            self.assertEqual(regular(root, "text"), (root / "text").resolve())
            for relative in ("../text", "a/../../text", "C:/text", "/text", "a\\text"):
                with self.subTest(relative=relative), self.assertRaises(ValueError):
                    regular(root, relative)

    def test_crt_graph_and_delay_imports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            crt = root / "crt"
            crt.mkdir()
            for name in ("vcruntime140.dll", "vcruntime140_1.dll"):
                (crt / name).write_bytes(b"binary fixture")
            def inspect(path):
                return {"vcruntime140_1.dll"} if path.name == "vcruntime140.dll" else (
                    {"vcruntime140.dll", "kernel32.dll"} if path.name == "app.exe" else set())
            resolved, crts = resolve([root / "app.exe"], [], crt, inspect)
            self.assertEqual(resolved, crts)
            self.assertEqual(len(crts), 2)

    def test_runtime_rejections(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("msvcp140d.dll", "unknown.dll", "vcruntime999.dll"):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    resolve([root / "app"], [], root, lambda path: {name})
            a, b = root / "a", root / "b"
            a.mkdir()
            b.mkdir()
            for path in (a, b):
                (path / "library.dll").write_bytes(b"fixture")
            with self.assertRaisesRegex(ValueError, "conflicting"):
                resolve([root / "app"], [a, b], root, lambda path: {"library.dll"})

    @unittest.skipUnless(sys.platform == "win32", "Windows import names are case-insensitive")
    def test_runtime_preserves_supplier_filename_case(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            (root / "Qt6Core.dll").write_bytes(b"Qt fixture")
            (root / "vcruntime140.dll").write_bytes(b"CRT fixture")
            def inspect(path):
                return {"qt6core.dll", "vcruntime140.dll"} if path.name == "app" else set()
            resolved, _ = resolve([root / "app"], [root], root, inspect)
            self.assertIn("Qt6Core.dll", [path.name for path in resolved])


if __name__ == "__main__":
    unittest.main()
