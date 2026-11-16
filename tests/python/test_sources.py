import hashlib
import io
import sys
import tarfile
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from collect_legal import attribution_files
from collect_sources import SourceInputs, tree_digest
from release_common import digest


class SourceTests(unittest.TestCase):
    def test_tree_digest_includes_names_and_rejects_git(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "a").write_bytes(b"same")
            first = tree_digest(root)
            (root / "a").rename(root / "b")
            self.assertNotEqual(tree_digest(root), first)
            (root / ".git").mkdir()
            with self.assertRaises(ValueError):
                tree_digest(root)

    def test_archive_pin_and_layout(self):
        for member_name, link, valid in (("fixture/a", False, True), ("../escape", False, False),
                                         ("fixture/link", True, False), ("fixture/.git/config", False, False)):
            with self.subTest(member=member_name), tempfile.TemporaryDirectory() as temporary, ExitStack() as stack:
                root = Path(temporary)
                archive = root / "fixture-abc.tar"
                with tarfile.open(archive, "w") as tar:
                    member = tarfile.TarInfo(member_name)
                    if link:
                        member.type, member.linkname = tarfile.SYMTYPE, "../escape"
                        tar.addfile(member)
                    else:
                        member.size = 1
                        tar.addfile(member, io.BytesIO(b"x"))
                inputs = SourceInputs(root, root, stack)
                tree = hashlib.sha256(("a " + hashlib.sha256(b"x").hexdigest() + "\n").encode()).hexdigest()
                inputs.manifest = {"fixture": {"commit": "abc"}}
                inputs.pins = {"fixture": {"commit": "abc", "sha256": digest(archive), "tree_sha256": tree}}
                if valid:
                    self.assertEqual((inputs.source("fixture") / "a").read_bytes(), b"x")
                    inputs.pins["fixture"]["sha256"] = "0" * 64
                with self.assertRaises(ValueError):
                    inputs.source("fixture")
                self.assertFalse((root.parent / "escape").exists())

    def test_qt_attribution_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary).resolve()
            (source / "src").mkdir()
            (source / "LICENSE").write_bytes(b"original")
            attribution = source / "src/qt_attribution.json"
            self.assertEqual(attribution_files(source, attribution, {"LicenseFile": "../LICENSE"}), {source / "LICENSE"})
            for item in ({"LicenseFile": "../../escape"}, {"LicenseFiles": "LICENSE"},
                         {"LicenseFiles": [1]}, {"CopyrightFile": "C:/private"}):
                with self.subTest(item=item), self.assertRaises(ValueError):
                    attribution_files(source, attribution, item)


if __name__ == "__main__":
    unittest.main()
