"""Local Git/CMake tests for the patch-copy integrity guards; no network access."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
GIT = shutil.which("git")
CMAKE = os.environ.get("SMOOTHMAIL_CMAKE", shutil.which("cmake"))


@unittest.skipUnless(GIT and CMAKE, "Git and CMake are required")
class KioPatchTest(unittest.TestCase):
    def setUp(self):
        # Do not use Windows mode-0700 TemporaryDirectory: that requires ACL
        # changes in some sandboxes. Keep owned fixtures under ignored build/.
        self.root = ROOT / "build/kio-patch-tests" / uuid.uuid4().hex
        self.pristine = self.root / "pristine"
        self.pristine.mkdir(parents=True)
        self.git("init", str(self.pristine))
        (self.pristine / "config.txt").write_text("before\n", encoding="utf-8", newline="\n")
        self.git("-C", str(self.pristine), "add", "config.txt")
        tree = self.git("-C", str(self.pristine), "write-tree").stdout.strip()
        self.commit = self.git("-C", str(self.pristine), "-c", "user.name=SmoothMail test",
                               "-c", "user.email=test@example.invalid", "commit-tree", tree,
                               "-m", "Fixture").stdout.strip()
        self.git("-C", str(self.pristine), "update-ref", "HEAD", self.commit)
        self.patch = self.root / "change.patch"
        self.patch.write_text(
            "diff --git a/config.txt b/config.txt\n"
            "--- a/config.txt\n+++ b/config.txt\n@@ -1 +1 @@\n-before\n+after\n",
            encoding="utf-8", newline="\n")
        key = hashlib.sha256(self.patch.read_bytes()).hexdigest()[:16]
        self.copy = self.root / "copies" / f"kio-{self.commit}-{key}"

    @staticmethod
    def git(*arguments):
        return subprocess.run([GIT, *arguments], capture_output=True, text=True,
                              encoding="utf-8", errors="replace", check=True)

    def prepare(self, *extra_arguments):
        return subprocess.run(
            [CMAKE, f"-DPRISTINE={self.pristine}", f"-DCOMMIT={self.commit}",
             f"-DPATCH={self.patch}", f"-DWORKSPACE={self.root / 'copies'}",
             *extra_arguments, "-P", str(ROOT / "tests/cmake/prepare_kio_fixture.cmake")],
            capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)

    def assert_prepared(self):
        result = self.prepare()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_applies_once_and_keeps_original(self):
        self.assert_prepared()
        self.assert_prepared()
        self.assertEqual((self.pristine / "config.txt").read_text(), "before\n")
        self.assertEqual((self.copy / "config.txt").read_text(), "after\n")

    def test_generic_dependency_uses_separate_copy(self):
        self.assert_prepared()
        for _ in range(2):
            result = self.prepare("-DDEPENDENCY=kimap")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        other = self.copy.with_name(self.copy.name.replace("kio-", "kimap-", 1))
        self.assertEqual((other / "config.txt").read_text(), "after\n")
        self.assertEqual((self.pristine / "config.txt").read_text(), "before\n")

    def test_rejects_modified_tracked_file_without_overwriting(self):
        self.assert_prepared()
        changed = self.copy / "config.txt"
        changed.write_text("user change\n", encoding="utf-8")
        self.assertNotEqual(self.prepare().returncode, 0)
        self.assertEqual(changed.read_text(), "user change\n")

    def test_rejects_added_source_file(self):
        self.assert_prepared()
        added = self.copy / "extra.cpp"
        added.write_text("// unexpected source\n", encoding="utf-8")
        self.assertNotEqual(self.prepare().returncode, 0)
        self.assertTrue(added.exists())

    def test_rejects_staged_changes(self):
        self.assert_prepared()
        self.git("-C", str(self.copy), "add", "config.txt")
        self.assertNotEqual(self.prepare().returncode, 0)

    def test_receipt_is_data_not_executable_code(self):
        self.assert_prepared()
        marker = self.root / "must-not-exist"
        receipt = Path(str(self.copy) + ".receipt.cmake")
        receipt.write_text(f'file(WRITE "{marker.as_posix()}" "executed")\n', encoding="utf-8")
        self.assertNotEqual(self.prepare().returncode, 0)
        self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
