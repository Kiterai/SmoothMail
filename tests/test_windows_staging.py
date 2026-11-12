"""Non-destructive staging guards; these tests do not need Qt or MSVC."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
CMAKE = os.environ.get("SMOOTHMAIL_CMAKE", shutil.which("cmake"))


@unittest.skipUnless(CMAKE, "CMake is required")
class StagingGuardTest(unittest.TestCase):
    def setUp(self):
        self.directory = ROOT / "build/staging-guard-tests" / uuid.uuid4().hex
        self.directory.mkdir(parents=True)

    def invoke(self, configuration="Debug", destination=None, **overrides):
        paths = {name: self.directory for name in ("APP", "QT_BIN", "DEPENDENCY_BIN", "NATIVE_BIN", "SASL_DIR")}
        paths.update(overrides)
        paths["DESTINATION"] = destination or self.directory / "destination"
        paths["CONFIGURATION"] = configuration
        return subprocess.run([CMAKE, *(f"-D{key}={value}" for key, value in paths.items()),
                               "-P", str(ROOT / "cmake/StageWindowsRuntime.cmake")],
                              capture_output=True, text=True, check=False)

    def test_rejects_release_without_creating_destination(self):
        result = self.invoke(configuration="Release")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("supports Debug only", result.stderr)
        self.assertFalse((self.directory / "destination").exists())

    def test_preserves_existing_directory(self):
        marker = self.directory / "user-work.txt"
        marker.write_text("keep", encoding="utf-8")
        result = self.invoke(destination=self.directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must be empty", result.stderr)
        self.assertEqual(marker.read_text(encoding="utf-8"), "keep")

    def test_rejects_missing_input_before_writing(self):
        result = self.invoke(APP=self.directory / "missing.exe")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing APP", result.stderr)
        self.assertFalse((self.directory / "destination").exists())


if __name__ == "__main__":
    unittest.main()
