import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/source-replacement"))
import prepare_package_review as review
from release_common import digest, read_json


class SourceReplacementPreparationTests(unittest.TestCase):
    def fixture(self, root):
        archive = root / "product.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            for name, data in {
                "launch.cmd": b"launcher", "runtime/Smoothmail.exe": b"product",
                "runtime/Qt6Svg.dll": b"original svg", "runtime/KF6Codecs.dll": b"original codecs",
            }.items():
                bundle.writestr("SmoothMail/" + name, data)
        args = SimpleNamespace(package=archive, expected_sha256=digest(archive),
                               review_root=root / "review", application_build=root / "app",
                               qt_root=root / "sdk", output=root / "prepared")
        files = [args.review_root / "probe/source_replacement_probe.exe",
                 args.qt_root / "bin/Qt6Test.dll", args.qt_root / "plugins/platforms/qoffscreen.dll",
                 *[args.application_build / (name + ".exe") for name in review.TESTS]]
        for path in files:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"test fixture")
        libraries = args.review_root / "modified/install/bin"
        libraries.mkdir(parents=True)
        (libraries / "Qt6Svg.dll").write_bytes(b"modified svg")
        (libraries / "KF6Codecs.dll").write_bytes(b"modified codecs")
        return args

    def test_only_two_libraries_change_and_release_archive_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            review.prepare(args)
            result = read_json(args.output / "preparation.json")
            self.assertEqual(result["changed_files"], ["runtime/KF6Codecs.dll", "runtime/Qt6Svg.dll"])
            self.assertEqual(result["variants"]["baseline"]["application_sha256"],
                             result["variants"]["replacement"]["application_sha256"])
            self.assertEqual(digest(args.package), args.expected_sha256)
            for variant in ("baseline", "replacement"):
                with zipfile.ZipFile(args.output / "input" / (variant + ".zip")) as archive:
                    self.assertEqual(archive.read("SmoothMail/runtime/Smoothmail.exe"), b"product")
            with self.assertRaisesRegex(ValueError, "already exists"):
                review.prepare(args)

    def test_wrong_package_identity_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            args.expected_sha256 = "0" * 64
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                review.prepare(args)
            self.assertFalse(args.output.exists())

    def test_unmodified_library_does_not_count_as_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            (args.review_root / "modified/install/bin/Qt6Svg.dll").write_bytes(b"original svg")
            with self.assertRaisesRegex(ValueError, "Unexpected differences"):
                review.prepare(args)
            self.assertEqual(digest(args.package), args.expected_sha256)


if __name__ == "__main__":
    unittest.main()
