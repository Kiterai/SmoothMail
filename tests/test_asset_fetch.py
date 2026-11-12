"""Downloader unit tests: no network access or external executable is used."""

import hashlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("asset_fetch", ROOT / "tools/fetch-vcpkg-asset.py")
asset_fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(asset_fetch)


class AssetFetchTest(unittest.TestCase):
    def test_invalid_hash_does_not_execute(self):
        with mock.patch.object(sys, "argv", ["fetch", "curl", "https://example.com", "bad", "unused"]):
            with mock.patch.object(subprocess, "run") as execute:
                self.assertEqual(asset_fetch.main(), 2)
                execute.assert_not_called()

    def test_mirrors_tls_policy_and_hash(self):
        for original, expected in [
            ("https://ftpmirror.gnu.org/gnu/gettext/file.tar.gz",
             "https://ftp.gnu.org/gnu/gettext/file.tar.gz"),
            ("https://mirror.msys2.org/msys/x86_64/file.zst",
             "https://repo.msys2.org/msys/x86_64/file.zst"),
            ("https://github.com/vendor/project/file", "https://github.com/vendor/project/file"),
        ]:
            with self.subTest(url=original):
                destination = "asset-download.part"
                payload = b"known test payload"
                digest = hashlib.sha512(payload).hexdigest()

                def fake_curl(arguments, check):
                    self.assertFalse(check)
                    self.assertEqual(arguments[0], "curl-tool")
                    self.assertEqual(arguments[-1], expected)
                    self.assertIn("=https", arguments)
                    self.assertNotIn("--insecure", arguments)
                    return subprocess.CompletedProcess(arguments, 0)

                with mock.patch.object(sys, "argv", ["fetch", "curl-tool", original, digest, destination]):
                    with mock.patch.object(subprocess, "run", side_effect=fake_curl):
                        with mock.patch.object(Path, "open", return_value=io.BytesIO(payload)):
                            self.assertEqual(asset_fetch.main(), 0)

    def test_bad_download_is_rejected(self):
        with mock.patch.object(sys, "argv", ["fetch", "curl", "https://example.com", "0" * 128, "asset.part"]):
            with mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0)):
                with mock.patch.object(Path, "open", return_value=io.BytesIO(b"not the expected asset")):
                    self.assertEqual(asset_fetch.main(), 1)

    def test_curl_failure_is_returned(self):
        with mock.patch.object(sys, "argv", ["fetch", "curl", "https://example.com", "0" * 128, "unused"]):
            with mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 22)):
                self.assertEqual(asset_fetch.main(), 22)


if __name__ == "__main__":
    unittest.main()
