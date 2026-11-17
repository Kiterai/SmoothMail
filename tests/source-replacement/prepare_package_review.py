"""Prepare disposable baseline/modified copies; never alter the release ZIP."""

import argparse
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from build import extract_verified
from release_common import digest, new_output, write_json

TESTS = ("tst_aboutdialog", "tst_mimeregression", "tst_messagecomposer",
         "tst_mimeparser", "tst_messageviewer", "tst_accountrepository",
         "tst_connectionpolicy", "tst_sasl")


def prepare(args):
    if digest(args.package) != args.expected_sha256:
        raise ValueError("Release ZIP identity mismatch")
    new_output(args.output)
    args.output.mkdir(parents=True)
    baseline = extract_verified(args.package, args.output / "work/baseline")
    runtime = baseline / "runtime"
    extras = [(args.review_root / "probe/source_replacement_probe.exe", runtime / "source_replacement_probe.exe"),
              (args.qt_root / "bin/Qt6Test.dll", runtime / "Qt6Test.dll"),
              (args.qt_root / "plugins/platforms/qoffscreen.dll", runtime / "platforms/qoffscreen.dll")]
    extras.extend((args.application_build / (name + ".exe"), runtime / (name + ".exe")) for name in TESTS)
    for source, destination in extras:
        if destination.exists():
            raise ValueError("Test fixture would overwrite a release file")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
    replacement = args.output / "work/replacement/SmoothMail"
    shutil.copytree(baseline, replacement)
    for name in ("Qt6Svg.dll", "KF6Codecs.dll"):
        shutil.copyfile(args.review_root / "modified/install/bin" / name, replacement / "runtime" / name)
    files = sorted(path.relative_to(baseline).as_posix() for path in baseline.rglob("*") if path.is_file())
    changed = [name for name in files if digest(baseline / name) != digest(replacement / name)]
    if changed != ["runtime/KF6Codecs.dll", "runtime/Qt6Svg.dll"]:
        raise ValueError("Unexpected differences between test variants")
    original_exe = zipfile.ZipFile(args.package)
    with original_exe:
        if original_exe.read("SmoothMail/runtime/Smoothmail.exe") != (runtime / "Smoothmail.exe").read_bytes():
            raise ValueError("Product EXE changed")
    guest_input = args.output / "input"
    guest_input.mkdir()
    records = {}
    for variant, tree in (("baseline", baseline), ("replacement", replacement)):
        with zipfile.ZipFile(guest_input / (variant + ".zip"), "x", zipfile.ZIP_DEFLATED) as archive:
            for name in files:
                archive.write(tree / name, "SmoothMail/" + name)
        (guest_input / (variant + "-sha256.txt")).write_text(
            "".join(f"{digest(tree / name)}  SmoothMail/{name}\n" for name in files), encoding="utf-8")
        records[variant] = {"archive_sha256": digest(guest_input / (variant + ".zip")),
                            "files": len(files),
                            "application_sha256": digest(tree / "runtime/Smoothmail.exe"),
                            "libraries": {name: digest(tree / name) for name in changed}}
    (guest_input / "tests.txt").write_text("\n".join(TESTS) + "\n", encoding="utf-8")
    shutil.copyfile(Path(__file__).with_name("run-package-guest.ps1"), guest_input / "run-package-guest.ps1")
    write_json(args.output / "preparation.json", {
        "test_only": True, "release_zip_sha256": args.expected_sha256,
        "changed_files": changed, "variants": records})
    (args.output / "output").mkdir()
    print(f"Prepared {len(files)} files per variant; only QtSvg/KCodecs differ; product EXE unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("package", "review-root", "application-build", "qt-root", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-sha256", required=True)
    prepare(parser.parse_args())
