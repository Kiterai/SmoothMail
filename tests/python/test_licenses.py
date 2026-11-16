import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import generate_licenses as licenses
from release_common import digest, read_json, write_json


class LicenseTests(unittest.TestCase):
    def test_environment_guards_and_no_value_disclosure(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "env"
            good = "SOFTWARE_NAME=SmoothMail\nDISTRIBUTION_URL=https://download.invalid/mail\nBUILD_VERSION=1\n"
            path.write_text(good, encoding="utf-8")
            self.assertEqual(licenses.read_env(path)["SOFTWARE_NAME"], "SmoothMail")
            path.write_text(good + "SMOOTHMAIL_GOOGLE_CLIENT_ID=fixture-client\n"
                            "SMOOTHMAIL_GOOGLE_CLIENT_SECRET=not-to-be-printed\n"
                            "SMOOTHMAIL_MICROSOFT_CLIENT_ID=\n", encoding="utf-8")
            self.assertEqual(set(licenses.read_env(path)), licenses.KEYS)
            for text in (good + "UNKNOWN=not-to-be-printed\n", good + "BUILD_VERSION=2\n",
                         good.replace("https://download.invalid", "https://user:secret@host.invalid"),
                         good.replace("SmoothMail", "${SECRET}"), good.replace("SmoothMail", "a;b"),
                         good.replace("BUILD_VERSION=1", "BUILD_VERSION="), good.replace("https:", "http:"),
                         good + "SMOOTHMAIL_GOOGLE_CLIENT_ID=a\nSMOOTHMAIL_GOOGLE_CLIENT_ID=b\n"):
                path.write_text(text, encoding="utf-8")
                with self.assertRaises(ValueError) as caught:
                    licenses.read_env(path)
                self.assertNotIn("not-to-be-printed", str(caught.exception))
                self.assertNotIn("user:secret", str(caught.exception))

    def test_templates(self):
        self.assertEqual(licenses.render("a\n\n \n\nb", {}), "a\n\nb")
        self.assertEqual(licenses.render("@NOTICE_STATUS_JA@", {}), "審査用草案。")
        self.assertEqual(licenses.render("@NOTICE_STATUS_JA@", {}, True), "")
        for text in ("@UNAPPROVED@", "@SOFTWARE_NAME@"):
            with self.assertRaises(ValueError):
                licenses.render(text, {})

    def test_final_runtime_hash_assignment_and_file_coverage(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage, evidence = root / "runtime", root / "evidence"
            stage.mkdir()
            evidence.mkdir()
            name = "vcruntime140.dll"
            (stage / name).write_bytes(b"CRT fixture")
            hashed = digest(stage / name)
            inventory = evidence / "runtime-components-sha256.txt"
            inventory.write_text(f"SHA256  runtime-relative-file  component\n{hashed}  {name}  msvc-crt\n")
            manifest = {"components": [{"name": "msvc-crt", "binaries": [name]}]}
            with patch.object(licenses, "crt_directory", return_value=root), patch.object(
                    licenses, "run", return_value="version=14.44.35211.0 sha256=" + hashed):
                version, records = licenses.check_runtime(evidence, stage, manifest, root, root / "verifier")
                self.assertEqual(version, "14.44.35211.0")
                self.assertTrue(records["review_only"])
                (stage / "unclassified.dll").write_bytes(b"extra")
                with self.assertRaisesRegex(ValueError, "unclassified"):
                    licenses.check_runtime(evidence, stage, manifest, root, root / "verifier")
                (stage / "unclassified.dll").unlink()
                (stage / name).write_bytes(b"changed")
                with self.assertRaisesRegex(ValueError, "changed"):
                    licenses.check_runtime(evidence, stage, manifest, root, root / "verifier")

    def test_complete_draft_and_original_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            evidence, source, output = root / "evidence", root / "source", root / "output"
            evidence.mkdir()
            source.mkdir()
            components = []
            archives = []
            for index, component in enumerate(["SmoothMail", "msvc-crt", *[f"c{i}" for i in range(17)]]):
                license_path = f"licenses/{component}/original.txt"
                path = evidence / license_path
                path.parent.mkdir(parents=True)
                path.write_bytes(b"Original\r\n\r\n\r\nbytes\r\n")
                notice = f"notices/{component}.md"
                path = evidence / notice
                path.parent.mkdir(exist_ok=True)
                path.write_text("Review", encoding="utf-8")
                binaries = [f"file-{index}-{i}.dll" for i in range(16 if index == 0 else 1)]
                components.append({"name": component, "version": "1", "license_files": [license_path],
                                   "source_notice": notice, "binaries": binaries})
                if component != "msvc-crt":
                    archives.append({"component": component, "file": component + ".tar"})
            archives.extend({"component": f"extra{i}", "file": f"extra{i}.tar"} for i in range(5))
            for item in archives:
                (source / item["file"]).write_bytes(b"source fixture")
                item["sha256"] = digest(source / item["file"])
            write_json(evidence / "manifest.review.json", {"schema": 1, "review_only": True, "components": components})
            (evidence / "runtime-components-sha256.txt").write_text("SHA256  runtime-relative-file  component\n")
            write_json(source / "source-manifest.review.json", {"schema": 1, "review_only": True, "archives": archives})
            env = root / "env"
            env.write_text("SOFTWARE_NAME=SmoothMail\nDISTRIBUTION_URL=https://host.invalid/mail\nBUILD_VERSION=1\n")
            args = SimpleNamespace(output=output, env_file=env, edition="windows-x64",
                                   evidence=evidence, source_review=source, final_review=False, qt_sbom_review=None)
            licenses.generate(args)
            self.assertFalse((output / "manifest.json").exists())
            self.assertTrue(read_json(output / "manifest.review.json")["review_only"])
            self.assertEqual((output / "originals/SmoothMail/original.txt").read_bytes(),
                             b"Original\r\n\r\n\r\nbytes\r\n")
            self.assertNotRegex((output / "README.md").read_text(encoding="utf-8"), licenses.PLACEHOLDER)
            # Distribution assembly is a separate, checked operation, not a
            # rename of a draft or an assertion of human legal approval.
            args.output = root / "distribution"
            args.distribution = True
            with self.assertRaisesRegex(ValueError, "requires final"):
                licenses.generate(args)
            args.final_review = True
            args.application_build = root / "app"
            args.application_build.mkdir()
            (args.application_build / "CMakeCache.txt").write_text("SMOOTHMAIL_SOURCE_COMMIT:STRING=abc123\n")
            archives[0]["commit"] = "abc123"
            write_json(source / "source-manifest.review.json",
                       {"schema": 1, "review_only": True, "archives": archives, "missing": []})
            args.stage = root / "runtime"
            args.crt_bin = root / "crt"
            args.crt_verifier = root / "verifier"
            env.write_text("SOFTWARE_NAME=SmoothMail\nDISTRIBUTION_URL=https://downloads.company.test/mail\nBUILD_VERSION=1\n")
            with patch.object(licenses, "check_runtime", return_value=("14.44.35211.0", {"review_only": True})):
                licenses.generate(args)
            self.assertEqual(set(read_json(args.output / "manifest.json")), {"schema", "components"})
            self.assertFalse((args.output / "manifest.review.json").exists())
            self.assertTrue(read_json(args.output / "evidence/manifest.review.json")["review_only"])
            self.assertNotIn("Draft for review", (args.output / "README.md").read_text())
            with self.assertRaisesRegex(ValueError, "already exists"):
                licenses.generate(args)
            # Source tampering fails before creating any output.
            args.output = root / "rejected"
            (source / archives[0]["file"]).write_bytes(b"tampered")
            with patch.object(licenses, "check_runtime", return_value=("14.44.35211.0", {})):
                with self.assertRaisesRegex(ValueError, "differs"):
                    licenses.generate(args)
            self.assertFalse(args.output.exists())


if __name__ == "__main__":
    unittest.main()
