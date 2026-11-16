"""Build, audit and publish a Windows ZIP. Run from an MSVC x64 environment."""

import argparse
import os
import subprocess
import uuid
from pathlib import Path

from release_common import ROOT, cmake_script, linked, new_output, qt_environment, run, tools
from stage_windows_release import crt_directory, stage


def preflight(qt, legal, output, application, crt):
    if output.suffix.lower() != ".zip":
        raise ValueError("Distribution output must be a ZIP")
    new_output(output)
    manifest = legal / "manifest.json"
    if (not legal.is_dir() or linked(legal) or not manifest.is_file()
            or linked(manifest) or manifest.stat().st_size == 0):
        raise ValueError("LEGAL_DIR must contain an existing regular nonempty release manifest.json")
    if (not application.is_dir() or linked(application)
            or not (application / "CMakeLists.txt").is_file() or not (application / "LICENSE").is_file()):
        raise ValueError("APPLICATION_SOURCE_DIR must contain the application CMakeLists.txt and LICENSE")
    crt_directory(crt)


def package(args):
    if os.name != "nt":
        raise ValueError("Windows/MSVC x64 developer environment required")
    qt = args.qt_root.absolute()
    legal, output = args.legal_dir.absolute(), args.output.absolute()
    application = args.application_source_dir.absolute()
    crt = args.crt_bin
    if crt is None:
        redist = os.environ.get("VCToolsRedistDir")
        candidates = list(Path(redist).glob("x64/Microsoft.VC*.CRT")) if redist else []
        if len(candidates) != 1:
            raise ValueError("Cannot uniquely locate MSVC Release CRT; specify --crt-bin")
        crt = candidates[0]
    preflight(qt, legal, output, application, crt)
    dependencies = args.dependency_root.resolve()
    extra = {"SMOOTHMAIL_SOURCE_ARCHIVES": args.source_archives,
             "SMOOTHMAIL_SOURCE_COMMIT": args.source_commit,
             "SMOOTHMAIL_ASSET_CACHE": args.asset_cache,
             "SMOOTHMAIL_DOWNLOAD_CURL": args.download_curl}
    helper, verifier = tools(qt, dependencies, extra)
    env = qt_environment(qt)
    run(helper, "--check-legal", legal, env=env)
    job = ROOT / "build/package-windows" / ("release-" + uuid.uuid4().hex)
    job.mkdir(parents=True)
    build, runtime = job / "build", job / "runtime"
    cmake_script("BuildWindows.cmake", {
        **extra, "Qt6_ROOT": qt, "DEPENDENCY_ROOT": dependencies,
        "CMAKE_BUILD_TYPE": "Release", "APPLICATION_SOURCE_DIR": application,
        "APPLICATION_BUILD": build, "BUILD_TESTING": "OFF",
        "SMOOTHMAIL_DISTRIBUTION": "ON", "SMOOTHMAIL_LEGAL_DIR": legal})
    prefix = dependencies / "install/Release/bin"
    native = dependencies / "native/gnu-runtime-v1/x64-windows/bin"
    stage(build / "smoothmail.exe", qt / "bin", prefix, native, prefix / "sasl2", crt, runtime, verifier)
    # Retain the independently tested native binary/privacy/license auditor and
    # its exclusive, deterministic ZIP writer; Python owns batch orchestration.
    run(helper, "--package-with-policy", runtime, legal, application, output,
        ROOT / "cmake/runtime-path-policy.json", ROOT, application, dependencies, job, qt, crt, legal,
        *(os.environ.get(key, "") for key in ("USERPROFILE", "TEMP", "USERNAME", "COMPUTERNAME")),
        env=env)
    print("Release ZIP published; privacy and coverage checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--legal-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--crt-bin", type=Path)
    parser.add_argument("--dependency-root", type=Path, default=ROOT / "build/deps")
    parser.add_argument("--application-source-dir", type=Path, default=ROOT)
    parser.add_argument("--source-archives", type=Path)
    parser.add_argument("--source-commit")
    parser.add_argument("--asset-cache", type=Path)
    parser.add_argument("--download-curl", type=Path)
    args = parser.parse_args()
    try:
        package(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")


if __name__ == "__main__":
    main()
