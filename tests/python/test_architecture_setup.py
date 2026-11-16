import hashlib
import io
import json
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import setup_architecture as setup
from generate_architecture import target_sources


class ArchitectureSetupTests(unittest.TestCase):
    def test_pinned_cache_mismatch_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "archive.zip"
            path.write_bytes(b"user content")
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                setup.download("https://example.invalid/not-requested", path, "0" * 64)
            self.assertEqual(path.read_bytes(), b"user content")
            setup.download("https://example.invalid/not-requested", path, hashlib.sha256(b"user content").hexdigest())

    def test_archive_layout_guards(self):
        for name, kind in (("../escape", tarfile.REGTYPE), ("/absolute", tarfile.REGTYPE),
                           ("linked", tarfile.SYMTYPE), ("C:/escape", tarfile.REGTYPE)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                archive = root / "archive.tar"
                with tarfile.open(archive, "w") as bundle:
                    member = tarfile.TarInfo(name)
                    member.type = kind
                    member.size = 1 if kind == tarfile.REGTYPE else 0
                    member.linkname = "../escape"
                    bundle.addfile(member, io.BytesIO(b"x") if member.size else None)
                with self.assertRaises(ValueError):
                    setup.unpack(archive, root / "extracted")
                self.assertFalse((root / "escape").exists())

    def test_reusable_binary_bytes_are_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "tool.tar"
            with tarfile.open(archive, "w") as bundle:
                member = tarfile.TarInfo("tool")
                member.size = 5
                bundle.addfile(member, io.BytesIO(b"valid"))
            unpacked = root / "installed"
            setup.unpack(archive, unpacked)
            setup.verify_unpacked(archive, unpacked)
            (unpacked / "tool").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "bytes differ"):
                setup.verify_unpacked(archive, unpacked)
            self.assertEqual((unpacked / "tool").read_bytes(), b"changed")

    def test_sdk_free_cmake_model(self):
        try:
            import ninja
        except ImportError:
            self.skipTest("isolated documentation toolchain is not installed")
        ninja_exe = Path(ninja.BIN_DIR) / ("ninja.exe" if sys.platform == "win32" else "ninja")
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            # Deliberately unusable compiler and SDK. Documentation must never
            # enable a language or enter real dependency/license preparation.
            result = subprocess.run(["cmake", "-S", str(ROOT), "-B", str(build), "-G", "Ninja",
                                     "-DSMOOTHMAIL_DOCUMENTATION_ONLY=ON", f"-DCMAKE_MAKE_PROGRAM={ninja_exe}",
                                     "-DCMAKE_CXX_COMPILER=must-not-be-used", "-DQt6_ROOT=must-not-be-used"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            targets = target_sources(build)
            self.assertEqual(len(targets), 10)
            self.assertIn("src/main.cpp", targets["smoothmail"])
            self.assertIn("src/storage/downloadfile.cpp", targets["smoothmail_storage"])
            self.assertFalse(any("tests/" in source for sources in targets.values() for source in sources))
            self.assertFalse((build / "license-resources").exists())
            manifest = json.loads((build / "documentation-targets.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["smoothmail"]["kind"], "EXECUTABLE")


if __name__ == "__main__":
    unittest.main()
