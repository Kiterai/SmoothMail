"""Integrity checks for installed modified-library receipts (no SDK/network needed)."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
CMAKE = os.environ.get("SMOOTHMAIL_CMAKE", shutil.which("cmake"))


@unittest.skipUnless(CMAKE, "CMake is required")
class ReceiptTest(unittest.TestCase):
    def setUp(self):
        self.root = ROOT / "build/receipt-tests" / uuid.uuid4().hex
        self.root.mkdir(parents=True)
        self.artifact, self.qt, self.patch = (self.root / name for name in ("mail.dll", "qt.dll", "change.patch"))
        for file in (self.artifact, self.qt, self.patch):
            file.write_bytes(file.name.encode())
        digest = lambda file: hashlib.sha256(file.read_bytes()).hexdigest()
        self.receipt = self.root / "receipt.json"
        self.data = {"commit": "fixture-commit", "patch_sha256": digest(self.patch),
                     "artifact_sha256": digest(self.artifact), "qtcore_sha256": digest(self.qt), "configuration": "Debug"}
        self.save()

    def save(self):
        self.receipt.write_text(json.dumps(self.data), encoding="utf-8")

    def validate(self):
        return subprocess.run([CMAKE, f"-DRECEIPT={self.receipt}", f"-DARTIFACT={self.artifact}",
                               f"-DQTCORE={self.qt}", f"-DPATCH={self.patch}", "-P",
                               str(ROOT / "tests/cmake/modified_receipt_fixture.cmake")],
                              capture_output=True, check=False).returncode

    def test_accepts_matching_artifacts(self):
        self.assertEqual(self.validate(), 0)

    def test_rejects_replaced_library(self):
        self.artifact.write_bytes(b"unpatched")
        self.assertNotEqual(self.validate(), 0)

    def test_rejects_different_qt(self):
        self.qt.write_bytes(b"another SDK")
        self.assertNotEqual(self.validate(), 0)

    def test_rejects_different_patch(self):
        self.patch.write_bytes(b"new patch")
        self.assertNotEqual(self.validate(), 0)

    def test_rejects_configuration_and_commit_changes(self):
        for field in ("configuration", "commit"):
            old = self.data[field]
            self.data[field] = "unexpected"
            self.save()
            self.assertNotEqual(self.validate(), 0)
            self.data[field] = old

    def test_never_executes_receipt_contents(self):
        marker = self.root / "must-not-exist"
        self.receipt.write_text(f'file(WRITE "{marker.as_posix()}" "bad")', encoding="utf-8")
        self.assertNotEqual(self.validate(), 0)
        self.assertFalse(marker.exists())


if __name__ == "__main__":
    unittest.main()
