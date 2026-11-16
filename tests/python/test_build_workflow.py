import json
import os
import re
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import build
import generate_licenses
import stage_windows_debug
from release_common import write_json


class BuildWorkflowTests(unittest.TestCase):
    def test_documented_command_budgets_and_explicit_directories(self):
        content = (ROOT / "docs/dev/build.md").read_text(encoding="utf-8")
        presets = json.loads((ROOT / "CMakePresets.json").read_text())["configurePresets"]
        for section, preset in (("デバッグビルド", "debug"), ("リリースビルド", "release")):
            instructions = content.split("## " + section + "\n")[1].split("\n## ")[0].strip()
            lines = re.fullmatch(r"```bat\n([^\n]+)\n([^\n]+)\n```", instructions).groups()
            self.assertLessEqual(len(lines[0]), 50)
            self.assertLessEqual(len(lines[1]), 30)
            self.assertIn("--preset " + preset, lines[0])
            directory = lines[0].split(" -B ")[1]
            self.assertEqual(lines[1], "cmake --build " + directory)
            self.assertTrue(next(item for item in presets if item["name"] == preset)[
                "cacheVariables"]["SMOOTHMAIL_WORKFLOW"] == "ON")
        runs = re.findall(r"```bat\n([^\n]+)\n```", (ROOT / "docs/dev/run.md").read_text(encoding="utf-8"))
        self.assertEqual(len(runs), 2)
        self.assertTrue(all(len(line) <= 30 for line in runs))
        dist = re.findall(r"```bat\n([^\n]+)\n```",
                          (ROOT / "docs/dev/distribution.md").read_text(encoding="utf-8"))
        self.assertEqual(dist, ["python tools/build.py dist"])
        self.assertLessEqual(len(dist[0]), 50)

    def test_build_settings_are_not_notice_or_oauth_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "Qt SDK/lib/cmake/Qt6/Qt6Config.cmake"
            config.parent.mkdir(parents=True)
            config.write_text("# fixture")
            env = root / ".env"
            env.write_text(
                "SOFTWARE_NAME=SmoothMail\nDISTRIBUTION_URL=https://downloads.company.test/mail\n"
                "BUILD_VERSION=1\nSMOOTHMAIL_QT_ROOT='Qt SDK'\n"
                "SMOOTHMAIL_DEPENDENCY_ROOT=dependencies\nSMOOTHMAIL_GOOGLE_CLIENT_SECRET=notices-must-not-contain-this\n")
            self.assertEqual(set(generate_licenses.read_env(env)), generate_licenses.KEYS)
            with patch.object(build, "ROOT", root):
                qt, dependencies, crt, extra, commit = build.settings()
            self.assertEqual(qt, root / "Qt SDK")
            self.assertEqual(dependencies, root / "dependencies")
            self.assertIsNone(crt)
            self.assertIsNone(commit)
            self.assertTrue(all(value is None for value in extra.values()))
            env.write_text(env.read_text() + "SMOOTHMAIL_QT_ROOT=duplicate\n")
            with self.assertRaises(ValueError):
                generate_licenses.read_env(env)

    def test_package_bytes_materialize_identical_launchable_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "app.zip"
            files = {"SmoothMail/launch.cmd": b"launcher",
                     "SmoothMail/runtime/smoothmail.exe": b"binary",
                     "SmoothMail/licenses/original.txt": b"Original\r\n\r\nbytes"}
            with zipfile.ZipFile(archive, "w") as bundle:
                for name, data in files.items():
                    bundle.writestr(name, data)
            output = build.extract_verified(archive, root / "unpacked")
            for name, data in files.items():
                self.assertEqual((output.parent / name).read_bytes(), data)
            with self.assertRaisesRegex(ValueError, "already exists"):
                build.extract_verified(archive, root / "unpacked")

    def test_unsafe_zip_and_existing_output_fail_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for index, name in enumerate(("../escape", "SmoothMail/../escape", "Other/app.exe",
                                          "SmoothMail/C:/file", "SmoothMail/a\\b")):
                archive = root / f"{index}.zip"
                with zipfile.ZipFile(archive, "w") as bundle:
                    info = zipfile.ZipInfo("placeholder")
                    info.filename = name
                    bundle.writestr(info, b"bad")
                destination = root / f"out-{index}"
                with self.assertRaisesRegex(ValueError, "layout"):
                    build.extract_verified(archive, destination)
                self.assertFalse(destination.exists())

    def test_output_rotation_and_failed_promotion_restore_previous_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old, new = root / "run", root / "new"
            old.mkdir()
            new.mkdir()
            (old / "marker").write_text("old")
            (new / "marker").write_text("new")
            build.publish(new, old)
            self.assertEqual((old / "marker").read_text(), "new")
            self.assertEqual(len(list(root.glob("run-previous-*"))), 1)
            with self.assertRaises(OSError):
                build.publish(root / "missing", old)
            self.assertEqual((old / "marker").read_text(), "new")

    def test_debug_runtime_resolves_transitive_crt_without_global_path(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "Qt6Cored.dll").write_bytes(b"qt")
            (root / "vcruntime140d.dll").write_bytes(b"crt")
            def inspect(path):
                return {"Qt6Cored.dll"} if path.name == "app.exe" else (
                    {"vcruntime140d.dll", "kernel32.dll"} if path.name == "Qt6Cored.dll" else set())
            libraries = stage_windows_debug.resolve([root / "app.exe"], [root], inspect)
            self.assertEqual({path.name for path in libraries}, {"Qt6Cored.dll", "vcruntime140d.dll"})
            with self.assertRaisesRegex(ValueError, "Unresolved"):
                stage_windows_debug.resolve([root / "app.exe"], [], inspect)

    def test_prepare_only_assembles_data_without_compiling_application(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extra = {"SMOOTHMAIL_SOURCE_ARCHIVES": root / "originals"}
            with patch.object(build, "settings", return_value=(root / "qt", root / "deps", root / "crt", extra, None)), \
                    patch.object(build, "release_crt", return_value=root / "crt"), \
                    patch.object(build, "run", return_value="abc123") as run, \
                    patch.object(build, "clean_revision") as clean, \
                    patch.object(build, "package_tools", return_value=(root / "helper", root / "verifier")), \
                    patch.object(build, "stage"), patch.object(build, "fetch_qt") as fetch, \
                    patch.object(build, "collect_sources"), patch.object(build, "collect_legal"), \
                    patch.object(build, "generate") as generate:
                build.prepare(root, root / "job")
            clean.assert_called_once()
            fetch.assert_not_called()
            args = generate.call_args.args[0]
            self.assertTrue(args.distribution and args.final_review)
            self.assertEqual(args.application_build, root / "app")
            self.assertFalse(any("--build" in call.args for call in run.call_args_list))

    def test_debug_staging_is_complete_and_keeps_sdk_bytes_unchanged(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            qt, deps, native, sasl = (root / name for name in ("qt", "deps", "native", "sasl"))
            crt = root / "redist/debug_nonredist/x64/Microsoft.VC143.DebugCRT"
            ucrt = root / "sdk/bin/1/x64/ucrt"
            app = root / "smoothmail.exe"
            app.write_bytes(b"app")
            inputs = [qt / "bin/Qt6Cored.dll", crt / "vcruntime140d.dll", ucrt / "ucrtbased.dll",
                      *[qt / "plugins" / name for name in stage_windows_debug.PLUGINS],
                      *[sasl / name for name in stage_windows_debug.SASL]]
            for path in inputs:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"original fixture")
            def inspect(path):
                return {"Qt6Cored.dll", "vcruntime140d.dll", "ucrtbased.dll"} if path == app else set()
            environment = {"VCToolsRedistDir": str(root / "redist"),
                           "UniversalCRTSdkDir": str(root / "sdk"), "UCRTVersion": "1"}
            resolve = stage_windows_debug.resolve
            with patch.dict(os.environ, environment), patch.object(stage_windows_debug, "run") as run, \
                    patch.object(stage_windows_debug, "resolve",
                                 side_effect=lambda roots, dirs: resolve(roots, dirs, inspect)):
                stage_windows_debug.stage(app, qt, deps, native, sasl, root / "run")
            self.assertEqual((root / "run/smoothmail.exe").read_bytes(), b"app")
            for relative in ("Qt6Cored.dll", "vcruntime140d.dll", "ucrtbased.dll",
                             *stage_windows_debug.PLUGINS, *["sasl2/" + name for name in stage_windows_debug.SASL]):
                self.assertTrue((root / "run" / relative).is_file(), relative)
            self.assertIn("non-redistributable", (root / "run/DEVELOPMENT-ONLY.txt").read_text())
            self.assertIn("SMOOTHMAIL_SASL_PATH", (root / "run/launch.cmd").read_text())
            self.assertTrue(all(path.read_bytes() == b"original fixture" for path in inputs))
            self.assertIn("--no-patchqt", run.call_args.args)

    def test_final_package_revalidates_source_and_publishes_hashes_and_runtime(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            job = root / "job"
            artifacts = job / "artifacts"
            (artifacts / "sources").mkdir(parents=True)
            write_json(artifacts / "sources/source-manifest.review.json",
                       {"archives": [{"component": "SmoothMail", "commit": "abc123"}]})
            def legal(stage, evidence, dependencies, archives):
                evidence.mkdir()
                write_json(evidence / "manifest.review.json", {"components": []})
            def run(*args, **kwargs):
                self.assertEqual(args[1], "--package-with-policy")
                with zipfile.ZipFile(args[5], "w") as bundle:
                    bundle.writestr("SmoothMail/launch.cmd", b"launcher")
                    bundle.writestr("SmoothMail/runtime/smoothmail.exe", b"final binary")
            with patch.object(build, "package_tools", return_value=(root / "helper", root / "verifier")), \
                    patch.object(build, "clean_revision") as clean, patch.object(build, "stage"), \
                    patch.object(build, "collect_legal", side_effect=legal), \
                    patch.object(build, "check_runtime") as check, patch.object(build, "run", side_effect=run):
                unpacked = build.package_release(root, job, root / "qt", root / "deps",
                                                 root / "prefix", root / "native", root / "crt",
                                                 {"SMOOTHMAIL_SOURCE_ARCHIVES": None}, root / "app")
            clean.assert_called_once_with(build.ROOT, "abc123")
            check.assert_called_once()
            self.assertEqual((unpacked / "runtime/smoothmail.exe").read_bytes(), b"final binary")
            self.assertTrue((root / "artifacts/SmoothMail-windows-x64.zip").is_file())
            hashes = (root / "artifacts/SHA256SUMS.txt").read_text()
            self.assertIn("SmoothMail-windows-x64.zip", hashes)
            self.assertIn("sources/source-manifest.review.json", hashes)
            self.assertIn("AUDIT-REQUIRED.txt", hashes)


if __name__ == "__main__":
    unittest.main()
