"""Collect fixed corresponding-source review artifacts (not distribution approval)."""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import tarfile
import tempfile
from contextlib import ExitStack, contextmanager
from pathlib import Path

from release_common import ROOT, digest, linked, new_output, read_json, regular, run, write_json


QT = {"qtbase": "025bdad181de241e81bf853c8a2d7bf3d19261a9",
      "qtsvg": "883a58532078d003cefef0a549bfe9f885ce41f5"}
DEPENDENCIES = ("kcodecs", "kconfig", "kcoreaddons", "kcrash", "ki18n", "kio", "kmime",
                "kservice", "kimap", "ksmtp", "qtkeychain", "cyrus-sasl-xoauth2",
                "extra-cmake-modules", "ninja", "vcpkg-recipes", "solid", "winflexbison")
APP_PATHS = ("CMakeLists.txt", "CMakePresets.json", "LICENSE", "THIRD_PARTY_NOTICES.md",
             ".env.example", "licenses", "src", "tests", "tools", "cmake", "patches", "docs", "spec")


def clean_revision(source, commit, untracked=False):
    if linked(source) or not source.is_dir():
        raise ValueError("Missing or linked source checkout")
    if run("git", "-C", source, "rev-parse", "HEAD", capture=True) != commit:
        raise ValueError("Source revision mismatch")
    run("git", "-C", source, "diff", "--quiet", "HEAD")
    if untracked and run("git", "-C", source, "status", "--porcelain", capture=True):
        raise ValueError("Source checkout has local changes")
    tree = run("git", "-C", source, "ls-tree", "-r", "HEAD", capture=True)
    if re.search(r"(^|\n)160000 ", tree):
        raise ValueError("Unresolved source submodules")


def tree_digest(directory):
    if linked(directory) or not directory.is_dir():
        raise ValueError("Missing or linked source tree")
    inventory = ""
    for path in sorted(directory.rglob("*"), key=lambda path: path.relative_to(directory).as_posix()):
        name = path.relative_to(directory).as_posix()
        if linked(path) or ".git" in path.parts or re.search(r"[;\r\n]", name):
            raise ValueError("Unsafe source tree entry")
        if path.is_file():
            inventory += name + " " + digest(path) + "\n"
    if not inventory:
        raise ValueError("Empty source tree")
    return hashlib.sha256(inventory.encode("utf-8")).hexdigest()


class SourceInputs:
    def __init__(self, dependencies, archives, stack):
        self.dependencies, self.archives, self.stack = dependencies, archives, stack
        self.pins = read_json(ROOT / "cmake/source-archives.json")
        self.manifest = read_json(ROOT / "cmake/dependencies.json")

    def source(self, component):
        commit = QT[component] if component in QT else self.manifest[component]["commit"]
        if self.archives is None:
            source = self.dependencies / ("qt-license-sources" if component in QT else "src") / component
            clean_revision(source, commit, untracked=component in QT)
            return source
        pin = self.pins[component]
        if pin["commit"] != commit:
            raise ValueError("Source archive pin does not match revision")
        archive = regular(self.archives, f"{component}-{commit}.tar")
        if digest(archive) != pin["sha256"]:
            raise ValueError("Source archive checksum differs from reviewed pin")
        cached = self.dependencies / "archive-src" / f"{component}-{commit}"
        if cached.exists() or linked(cached):
            if tree_digest(cached) != pin["tree_sha256"]:
                raise ValueError("Archived source tree was modified; choose a new source workspace")
            return cached.resolve()
        temporary = Path(self.stack.enter_context(tempfile.TemporaryDirectory(prefix="smoothmail-source-"))).resolve()
        with tarfile.open(archive) as tar:
            for member in tar:
                name = member.name
                # Reject links, devices, traversal and unexpected roots before writing.
                if ((not name.startswith(component + "/") and not (name == component and member.isdir()))
                        or re.search(r"[\\:;\r\n]|(^|/)\.\.(/|$)|(^|/)\.git(/|$)", name)
                        or not (member.isdir() or member.isfile())):
                    raise ValueError("Unsafe source archive layout")
                path = temporary / name
                if member.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with tar.extractfile(member) as stream, path.open("xb") as output:
                        shutil.copyfileobj(stream, output)
        source = temporary / component
        if tree_digest(source) != pin["tree_sha256"]:
            raise ValueError("Archived source tree differs from reviewed pin")
        return source


@contextmanager
def exclusive_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / ".smoothmail-source-lock"
    lock.mkdir()  # Fail closed if another collector owns the workspace.
    try:
        yield
    finally:
        lock.rmdir()


def fetch_qt(output):
    env = {**os.environ, "GIT_TERMINAL_PROMPT": "0", "GCM_INTERACTIVE": "never"}
    with exclusive_lock(output):
        for module, commit in QT.items():
            source = output / module
            if not source.exists() and not linked(source):
                run("git", "init", source, env=env)
                run("git", "-C", source, "remote", "add", "origin", f"https://code.qt.io/qt/{module}.git", env=env)
                run("git", "-c", "credential.helper=", "-C", source, "fetch", "--depth", "1", "origin", commit, env=env)
                run("git", "-C", source, "checkout", "--detach", "FETCH_HEAD", env=env)
            clean_revision(source, commit, untracked=True)
            print(f"Verified Qt source evidence: {module} {commit}")


def collect(dependencies, output, archives=None):
    new_output(output)
    pins = read_json(ROOT / "cmake/source-archives.json")
    manifest = read_json(ROOT / "cmake/dependencies.json")
    output.mkdir(parents=True)
    inventory = {"schema": 1, "review_only": True, "archives": [], "missing": []}
    commit = run("git", "-C", ROOT, "rev-parse", "HEAD", capture=True)
    clean_revision(ROOT, commit)
    def git_archive(component, source, revision, paths=()):
        clean_revision(source, revision)
        filename = f"{component}-{revision}.tar"
        run("git", "-C", source, "archive", "--format=tar", f"--prefix={component}/",
            f"--output={output / filename}", revision, *paths)
        hashed = digest(output / filename)
        if component != "SmoothMail" and hashed != pins[component]["sha256"]:
            raise ValueError("Generated source archive differs from reviewed pin")
        inventory["archives"].append({"component": component, "commit": revision, "file": filename, "sha256": hashed})
    git_archive("SmoothMail", ROOT, commit, APP_PATHS)
    for component in (*DEPENDENCIES, *QT):
        revision = QT[component] if component in QT else manifest[component]["commit"]
        if archives:
            source = regular(archives, f"{component}-{revision}.tar")
            if digest(source) != pins[component]["sha256"]:
                raise ValueError("Source archive checksum differs from reviewed pin")
            shutil.copyfile(source, output / source.name)
            inventory["archives"].append({"component": component, "commit": revision,
                                         "file": source.name, "sha256": digest(source)})
        else:
            source = dependencies / ("qt-license-sources" if component in QT else "src") / component
            if component in QT and not (source / "LICENSES").is_dir():
                inventory["missing"].append(f"{component}: fixed source not available")
            else:
                git_archive(component, source, revision)
    originals = (
        ("cyrus-sasl", "cyrus-sasl-2.1.28.tar.gz", "downloads", "sha256",
         "7ccfc6abd01ed67c1a0924b353e526f1b766b21f42d4562ee635a8ebfc5bb38c"),
        ("libiconv", "libiconv-1.19.tar.gz", "src/vcpkg-recipes/downloads", "sha512",
         "1e8150f9bca907579330cd9c44ebbee46a260271fbe8f50d5ee24a39ef29c8d254505e85c3409324f7440596da711a8bd49e89f848a6be0cb3238a58c24aaecd"),
        ("gettext", "gettext-0.22.5.tar.gz", "src/vcpkg-recipes/downloads", "sha512",
         "d8b22d7fba10052a2045f477f0a5b684d932513bdb3b295c22fbd9dfc2a9d8fccd9aefd90692136c62897149aa2f7d1145ce6618aa1f0be787cb88eba5bc09be"))
    for component, name, directory, algorithm, expected in originals:
        source = regular(archives if archives else dependencies / directory, name)
        with source.open("rb") as stream:
            if hashlib.file_digest(stream, algorithm).hexdigest() != expected:
                raise ValueError("Original source archive digest mismatch")
        shutil.copyfile(source, output / name)
        inventory["archives"].append({"component": component, "file": name, "sha256": digest(source)})
    write_json(output / "source-manifest.review.json", inventory)
    (output / "README.txt").write_text(
        "SOURCE REVIEW ONLY - NOT APPROVED AS COMPLETE CORRESPONDING SOURCE.\n\n"
        "Publish reviewed source artifacts on the SAME page as matching binary downloads.\n"
        f"SmoothMail commit: {commit}\n"
        "Extract SmoothMail into a new directory; do not fabricate dependency Git repositories.\n"
        "Rebuild with CMake using SMOOTHMAIL_SOURCE_ARCHIVES and SMOOTHMAIL_SOURCE_COMMIT.\n"
        "See docs/dev/build.md and docs/dev/distribution.md in the SmoothMail archive.\n"
        "Fixed upstream archives, SmoothMail patches and build settings must be retained together.\n"
        "Use a separate prefix for optional Qt source rebuilds; never replace the installed SDK.\n"
        f"Missing modules: {len(inventory['missing'])}. Zero does not replace legal review or ABI/replacement tests.\n",
        encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fetch-qt", action="store_true")
    parser.add_argument("--dependency-root", type=Path, default=ROOT / "build/deps")
    parser.add_argument("--source-archives", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.fetch_qt:
            fetch_qt(args.output.absolute())
        else:
            collect(args.dependency_root.absolute(), args.output.absolute(), args.source_archives)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError, tarfile.TarError) as error:
        parser.exit(1, f"Source collection failed: {error}\n")


if __name__ == "__main__":
    main()
