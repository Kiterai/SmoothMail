"""Short Windows build entry; assembly never substitutes for distributor audit."""

import argparse
import os
import shutil
import subprocess
import uuid
import zipfile
from pathlib import Path
from types import SimpleNamespace

from collect_legal import collect as collect_legal
from collect_sources import clean_revision, collect as collect_sources, fetch_qt
from generate_licenses import check_runtime, generate, read_env
from release_common import (ROOT, digest, linked, new_output,
                            qt_environment, read_json, run)
from stage_windows_debug import stage as stage_debug
from stage_windows_release import crt_directory, stage


def settings():
    values = read_env(ROOT / ".env", include_build=True)
    def path(key, default=None):
        value = values.get(key)
        return (ROOT / value).absolute() if value else default
    qt = path("SMOOTHMAIL_QT_ROOT")
    if qt is None or not (qt / "lib/cmake/Qt6/Qt6Config.cmake").is_file():
        raise ValueError("Set SMOOTHMAIL_QT_ROOT in .env to an installed MSVC Qt SDK")
    dependencies = path("SMOOTHMAIL_DEPENDENCY_ROOT", ROOT / "build/deps")
    extra = {key: path(key) for key in (
        "SMOOTHMAIL_SOURCE_ARCHIVES", "SMOOTHMAIL_ASSET_CACHE", "SMOOTHMAIL_DOWNLOAD_CURL")}
    commit = values.get("SMOOTHMAIL_SOURCE_COMMIT") or None
    return qt, dependencies, path("SMOOTHMAIL_CRT_BIN"), extra, commit


def release_crt(explicit):
    if explicit is not None:
        return crt_directory(explicit)
    redist = os.environ.get("VCToolsRedistDir")
    candidates = list(Path(redist).glob("x64/Microsoft.VC143.CRT")) if redist else []
    if len(candidates) != 1:
        raise ValueError("Cannot locate MSVC Release CRT; set SMOOTHMAIL_CRT_BIN in .env")
    return crt_directory(candidates[0])


def publish(source, destination):
    """Keep previous successful output, including on a failed rebuild."""
    if linked(destination):
        raise ValueError("Refusing to replace linked build output")
    previous = None
    if destination.exists():
        previous = destination.with_name(destination.name + "-previous-" + uuid.uuid4().hex)
        destination.rename(previous)
    try:
        source.rename(destination)
    except OSError:
        if previous is not None:
            previous.rename(destination)
        raise


def extract_verified(archive, destination):
    """Materialize precisely the package bytes, not a second deployment."""
    new_output(destination)
    with zipfile.ZipFile(archive) as bundle:
        # ZipInfo.filename normalizes host separators on Windows; validate the
        # original spelling so a stored backslash cannot evade the layout check.
        names = [info.orig_filename for info in bundle.infolist()]
        if len(names) != len({name.casefold() for name in names}):
            raise ValueError("Duplicate ZIP entry")
        for name in names:
            parts = name.split("/")
            if (not name.startswith("SmoothMail/") or "\\" in name or ":" in name
                    or any(part in (".", "..", "") for part in parts)):
                raise ValueError("Unexpected ZIP layout")
        if not {"smoothmail/launch.cmd", "smoothmail/runtime/smoothmail.exe"} <= {
                name.casefold() for name in names}:
            raise ValueError("ZIP lacks the launchable application")
        bundle.extractall(destination)
        for name in names:
            if (destination / name).read_bytes() != bundle.read(name):
                raise ValueError("Unpacked runtime differs from ZIP")
    return destination / "SmoothMail"


def prepare(directory, job):
    qt, dependencies, crt, extra, commit = settings()
    crt = release_crt(crt)
    commit = run("git", "-C", ROOT, "rev-parse", "HEAD", capture=True)
    clean_revision(ROOT, commit)
    application = directory / "app"
    prefix = dependencies / "install/Release/bin"
    native = dependencies / "native/gnu-runtime-v1/x64-windows"
    verifier = package_tools(dependencies)[1]
    preliminary = job / "preliminary"
    stage(application / "smoothmail.exe", qt / "bin", prefix, native / "bin",
          prefix / "sasl2", crt, preliminary, verifier)
    if not extra["SMOOTHMAIL_SOURCE_ARCHIVES"]:
        fetch_qt(dependencies / "qt-license-sources")
    artifacts = job / "artifacts"
    sources = artifacts / "sources"
    collect_sources(dependencies, sources, extra["SMOOTHMAIL_SOURCE_ARCHIVES"])
    run("cmake", f"-DREVIEW={sources}", "-P", ROOT / "tests/cmake/verify_source_review.cmake")
    evidence, legal = job / "evidence", job / "licenses"
    collect_legal(preliminary, evidence, dependencies, extra["SMOOTHMAIL_SOURCE_ARCHIVES"])
    generate(SimpleNamespace(
        output=legal, env_file=ROOT / ".env", edition="windows-x64",
        evidence=evidence, source_review=sources, final_review=True, distribution=True,
        stage=preliminary, crt_bin=crt, application_build=application,
        crt_verifier=verifier, qt_sbom_review=None))
    # The CMake workflow now embeds these notices and performs the final link.


def package_tools(dependencies):
    binary = dependencies / "build/tools/package"
    return binary / "smoothmail_package.exe", Path((binary / "crt-verifier.txt").read_text())


def finish(directory, job, configuration):
    qt, dependencies, crt, extra, _ = settings()
    application = directory / "app"
    prefix = dependencies / "install" / configuration / "bin"
    native = dependencies / "native/gnu-runtime-v1/x64-windows"
    if configuration == "Debug":
        runtime = job / "run"
        stage_debug(application / "smoothmail.exe", qt, prefix, native / "debug/bin",
                    prefix / "sasl2", runtime)
    else:
        runtime = package_release(directory, job, qt, dependencies, prefix, native,
                                  release_crt(crt), extra, application)
    publish(runtime, directory / "run")
    print(f"Ready: {directory / 'run/launch.cmd'}")


def package_release(directory, job, qt, dependencies, prefix, native, crt, extra, application):
    helper, verifier = package_tools(dependencies)
    artifacts, legal = job / "artifacts", job / "licenses"
    source_manifest = read_json(artifacts / "sources/source-manifest.review.json")
    app_sources = [item for item in source_manifest["archives"] if item["component"] == "SmoothMail"]
    if len(app_sources) != 1:
        raise ValueError("Missing unique application source identity")
    clean_revision(ROOT, app_sources[0]["commit"])
    runtime = job / "runtime"
    stage(application / "smoothmail.exe", qt / "bin", prefix, native / "bin",
          prefix / "sasl2", crt, runtime, verifier)
    final_evidence = job / "final-evidence"
    collect_legal(runtime, final_evidence, dependencies, extra["SMOOTHMAIL_SOURCE_ARCHIVES"])
    check_runtime(final_evidence, runtime,
                  read_json(final_evidence / "manifest.review.json"), crt, verifier)
    archive = artifacts / "SmoothMail-windows-x64.zip"
    run(helper, "--package-with-policy", runtime, legal, ROOT, archive,
        ROOT / "cmake/runtime-path-policy.json", ROOT, ROOT, dependencies, job, qt, crt, legal,
        *(os.environ.get(key, "") for key in ("USERPROFILE", "TEMP", "USERNAME", "COMPUTERNAME")),
        env=qt_environment(qt))
    unpacked = extract_verified(archive, job / "unpacked")
    (artifacts / "AUDIT-REQUIRED.txt").write_text(
        "Generated, NOT legally approved. Before publishing: perform functional, privacy,\n"
        "license, corresponding-source rebuild and DLL replacement checks.\n"
        "Publish the binary ZIP, all sources, hashes and notices together.\n"
        "See docs/dev/distribution.md in the SmoothMail source archive.\n", encoding="utf-8")
    (artifacts / "SHA256SUMS.txt").write_text(
        "".join(f"{digest(path)}  {path.relative_to(artifacts).as_posix()}\n"
                for path in sorted(artifacts.rglob("*")) if path.is_file()),
        encoding="utf-8")
    publish(artifacts, directory / "artifacts")
    return unpacked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "finish", "dist"))
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--job", type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    args = parser.parse_args()
    try:
        if args.action == "dist":
            directory = (args.directory or ROOT / "build/rel").absolute()
            run("cmake", "--preset", "release", "-B", directory)
            run("cmake", "--build", directory)
        else:
            if args.directory is None or args.job is None:
                raise ValueError("Internal assembly requires --directory and --job")
            if args.action == "prepare":
                prepare(args.directory.absolute(), args.job.absolute())
            else:
                finish(args.directory.absolute(), args.job.absolute(), args.configuration)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Build failed: {error}\n")


if __name__ == "__main__":
    main()
