"""Release package guards and archive verification, without MSVC/Qt/network."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("windows_package", ROOT / "tools/package-windows.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


def fixture_pe(payload=b"fixture", machine=0x8664):
    header = bytearray(128)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 60, 64)
    header[64:68] = b"PE\0\0"
    struct.pack_into("<H", header, 68, machine)
    struct.pack_into("<H", header, 88, 0x20b)
    return header + payload


class WindowsDistributionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.stage = self.root / "runtime"
        self.stage.mkdir()
        self.legal = self.root / "legal"
        self.legal.mkdir()
        self.manifest = {"schema": 1, "components": [{"name": "fixture", "version": "1", "binaries": ["*.dll", "sasl2/*.dll"], "license_files": ["license.txt"], "source_notice": "sources.md"}]}
        (self.legal / "license.txt").write_text("Fixture license\n", encoding="utf-8")
        (self.legal / "sources.md").write_text("Corresponding sources: https://example.test/sources\n", encoding="utf-8")
        self.write_manifest()

    def write_manifest(self):
        (self.legal / "manifest.json").write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_zip_has_relative_paths_fixed_metadata_and_matching_inventory(self):
        (self.stage / "smoothmail.exe").write_bytes(fixture_pe())
        (self.stage / "library.dll").write_bytes(fixture_pe())
        PACKAGE.prepare_legal(self.stage, self.legal)
        output = self.root / "portable.zip"
        PACKAGE.make_archive(self.stage, output)
        with zipfile.ZipFile(output) as archive:
            self.assertIn("SmoothMail/smoothmail.exe", archive.namelist())
            self.assertIn("SmoothMail/legal/license.txt", archive.namelist())
            self.assertNotIn("SmoothMail/start-smoothmail.cmd", archive.namelist())
            for item in archive.infolist():
                self.assertEqual(item.date_time, (1980, 1, 1, 0, 0, 0))
                self.assertNotIn(str(self.root), item.filename)
            for row in archive.read("SmoothMail/runtime-sha256.txt").decode().splitlines():
                digest, name = row.split("  ", 1)
                self.assertEqual(hashlib.sha256(archive.read("SmoothMail/" + name)).hexdigest(), digest)

    def test_refuses_missing_licenses(self):
        (self.stage / "uncovered.dll").write_bytes(b"fixture")
        self.manifest["components"][0]["binaries"] = ["another.dll"]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "coverage"):
            PACKAGE.prepare_legal(self.stage, self.legal)

    def test_rejects_path_traversal_and_opaque_license_input(self):
        for name in ("../outside.txt", "C:/private/file", "..\\outside.txt", "/etc/passwd"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                PACKAGE.relative(name)
        (self.legal / "license.txt").write_bytes(b"\xff\xfe\x00")
        with self.assertRaises(UnicodeError):
            PACKAGE.legal_manifest(self.legal)

    def test_private_paths_are_rejected_without_echoing_their_content(self):
        path = self.stage / "smoothmail.exe"
        for encoding in ("utf-8", "utf-16-le"):
            for value in (r"C:\Users\private-builder\project\file.cpp", "/home/private-builder/project/file.cpp", r"\\private-pc\share\source"):
                for padding in (b"", b"x"):
                    path.write_bytes(fixture_pe(padding + value.encode(encoding)))
                    with self.assertRaises(ValueError) as result:
                        PACKAGE.audit_file(path, path.name)
                    self.assertNotIn("private-builder", str(result.exception))
                    self.assertNotIn("private-pc", str(result.exception))

    def test_rejects_invalid_or_other_architecture_binaries(self):
        path = self.stage / "smoothmail.exe"
        for data in (b"not an exe", fixture_pe(machine=0x14c), fixture_pe(machine=0xaa64)):
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                PACKAGE.audit_file(path, path.name)

    def test_machine_bytes_and_public_urls_are_not_private_paths(self):
        path = self.stage / "smoothmail.exe"
        path.write_bytes(fixture_pe(b"\x89x:/\x90\xff\x00https://example.test/sources\x00"))
        PACKAGE.audit_file(path, path.name)

    def test_rejects_debug_and_user_data(self):
        for name in ("Qt6Cored.dll", "qt6keychaind.dll", "ucrtbased.dll", "msvcp140d_atomic_wait.dll", "smoothmail.pdb", "mail.sqlite", "session.log", "settings.ini"):
            path = self.stage / name
            path.write_bytes(b"fixture")
            with self.subTest(name=name), self.assertRaises(ValueError):
                PACKAGE.audit_file(path, name)

    def test_existing_output_is_preserved(self):
        output = self.root / "portable.zip"
        output.write_bytes(b"user artifact")
        with self.assertRaises(ValueError):
            PACKAGE.make_archive(self.stage, output)
        self.assertEqual(output.read_bytes(), b"user artifact")

    def test_missing_release_inputs_stop_before_any_build(self):
        args = argparse.Namespace(qt_root=self.root / "qt", dependency_prefix=self.root / "Release", native_bin=self.root / "native", crt_bin=self.root / "crt", legal_dir=self.legal, output=self.root / "portable.zip")
        with patch.object(PACKAGE.subprocess, "run") as run, self.assertRaisesRegex(ValueError, "Release"):
            PACKAGE.build_package(args)
        run.assert_not_called()
        self.assertFalse(args.output.exists())

    def test_successful_pipeline_builds_only_application_and_never_downloads(self):
        args = argparse.Namespace(qt_root=self.root / "qt", dependency_prefix=self.root / "Release", native_bin=self.root / "native", crt_bin=self.root / "crt", legal_dir=self.legal, output=self.root / "portable.zip", cmake="cmake")
        with patch.object(PACKAGE, "preflight"), patch.object(PACKAGE, "prepare_legal"), patch.object(PACKAGE, "make_archive"), patch.object(PACKAGE.tempfile, "mkdtemp", return_value=str(self.root / "job")), patch.object(PACKAGE.subprocess, "run") as run:
            PACKAGE.build_package(args)
        self.assertEqual(run.call_count, 3)
        commands = [call.args[0] for call in run.call_args_list]
        self.assertIn("-DCMAKE_BUILD_TYPE=Release", commands[0])
        self.assertIn("-DSMOOTHMAIL_DISTRIBUTION=ON", commands[0])
        self.assertEqual(commands[1][-2:], ["--target", "smoothmail"])
        self.assertTrue(commands[2][-1].endswith("StageWindowsRelease.cmake"))
        self.assertFalse(any(word in {"git", "curl", "vcpkg"} for command in commands for word in command))


if __name__ == "__main__":
    unittest.main()
