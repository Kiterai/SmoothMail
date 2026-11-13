"""Build and package Windows Release; never downloads or builds dependencies."""
import argparse
import fnmatch
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import struct
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEBUG_DLL = re.compile(r"^(?:qt6.*d|ucrtbased|msvcp.*d(?:_.*)?|vcruntime.*d|concrt.*d)\.dll$", re.I)
# Require readable path components rather than treating random PE machine bytes
# such as b'x:/\x90' as a path. Explicit input roots below also catch Unicode.
PATH_PART = r"[a-z0-9_. ()~$@+\-]"
PRIVATE_PATH = re.compile(rf"(?:[a-z]:[\\/](?![\\/])(?:{PATH_PART}{{2,100}}[\\/])+{PATH_PART}{{3,100}}|[a-z]:[\\/]{PATH_PART}{{3,100}}\.(?:pdb|cpp|h|obj|lib|dll|exe)|/(?:home|Users)/[^/\x00-\x20]+/|\\\\[a-z0-9_.-]{{2,}}\\[a-z0-9$_.-]{{2,}})", re.I)


def relative(value):
    if not isinstance(value, str):
        raise ValueError("Legal paths must be strings")
    path = PurePosixPath(value)
    if not value or path.is_absolute() or ".." in path.parts or "\\" in value or ":" in value:
        raise ValueError("Legal manifest requires safe relative paths")
    return path


def legal_manifest(directory):
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or set(manifest) != {"schema", "components"} or manifest.get("schema") != 1 or not isinstance(manifest.get("components"), list) or not manifest["components"]:
        raise ValueError("Legal manifest schema/components missing")
    files = set()
    for component in manifest["components"]:
        if not isinstance(component, dict) or set(component) - {"name", "version", "binaries", "license_files", "source_notice"}:
            raise ValueError("Unexpected legal manifest fields")
        if "version" in component and not isinstance(component["version"], str):
            raise ValueError("Legal component version must be a string")
        if not isinstance(component.get("name"), str) or not component["name"] or not isinstance(component.get("binaries"), list) or not component["binaries"] or not isinstance(component.get("license_files"), list) or not component["license_files"] or not component.get("source_notice"):
            raise ValueError("Each component needs binary patterns, licenses and a source notice")
        for pattern in component["binaries"]:
            relative(pattern)
        for name in [*component["license_files"], component["source_notice"]]:
            relative(name)
            source = directory / name
            if source.is_symlink() or not source.resolve().is_relative_to(directory.resolve()) or not source.is_file() or not source.stat().st_size:
                raise ValueError("Legal input missing, empty, or a symlink")
            # Legal input is reviewable text, not opaque archives or executables.
            source.read_text(encoding="utf-8")
            files.add(name)
    return manifest, files


def audit_file(path, name, private_roots=()):
    if path.is_symlink() or path.suffix.lower() in {".pdb", ".ilk", ".sqlite", ".db", ".log", ".ini"} or DEBUG_DLL.fullmatch(path.name):
        raise ValueError(f"Non-distributable file: {name}")
    data = path.read_bytes()
    if path.suffix.lower() in {".dll", ".exe"}:
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError(f"Invalid PE binary: {name}")
        offset = struct.unpack_from("<I", data, 60)[0]
        if offset + 26 > len(data) or data[offset:offset + 4] != b"PE\0\0" or struct.unpack_from("<H", data, offset + 4)[0] != 0x8664 or struct.unpack_from("<H", data, offset + 24)[0] != 0x20b:
            raise ValueError(f"Distribution requires x64 PE32+ binaries: {name}")
    texts = [data.decode("latin-1"), data.decode("utf-16-le", errors="ignore"), data[1:].decode("utf-16-le", errors="ignore")]
    for text in texts:
        normalized = text.replace("\\", "/").casefold()
        if PRIVATE_PATH.search(text) or any(str(root).replace("\\", "/").casefold() in normalized for root in private_roots if str(root)):
            # Do not echo the actual private path/content into diagnostics.
            raise ValueError(f"Embedded machine/user path detected: {name}")


def prepare_legal(stage, directory):
    manifest, files = legal_manifest(directory)
    binaries = [path.relative_to(stage).as_posix() for path in stage.rglob("*.dll")]
    for name in binaries:
        if not any(fnmatch.fnmatchcase(name.casefold(), pattern.casefold()) for component in manifest["components"] for pattern in component["binaries"]):
            raise ValueError(f"Missing license/source coverage: {name}")
    legal = stage / "legal"
    legal.mkdir()
    for name in sorted(files):
        destination = legal / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(directory / name, destination)
    (legal / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copyfile(ROOT / name, stage / name)


def make_archive(stage, output, private_roots=()):
    if output.exists():
        raise ValueError("Output already exists; it will not be overwritten")
    if (stage / "runtime-sha256.txt").exists():
        raise ValueError("Stage already has an inventory; use a fresh stage")
    files = sorted(path for path in stage.rglob("*") if path.is_file() or path.is_symlink())
    inventory = []
    for path in files:
        name = path.relative_to(stage).as_posix()
        audit_file(path, name, private_roots)
        inventory.append(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {name}\n")
    (stage / "runtime-sha256.txt").write_text("".join(inventory), encoding="utf-8")
    files.append(stage / "runtime-sha256.txt")
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=".smoothmail-", suffix=".zip", dir=output.parent)
    os.close(descriptor)
    temporary = Path(temporary)
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(files):
                info = zipfile.ZipInfo("SmoothMail/" + path.relative_to(stage).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, path.read_bytes())
        # Exclusive atomic publication; never replace an existing user artifact.
        os.link(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def preflight(args):
    if args.output.suffix.lower() != ".zip":
        raise ValueError("Distribution output must be a ZIP file")
    required = [args.qt_root / "bin/Qt6Core.dll", args.qt_root / "bin/Qt6Widgets.dll",
                args.dependency_prefix / "bin/KPim6IMAP.dll", args.dependency_prefix / "bin/KPim6SMTP.dll",
                args.dependency_prefix / "bin/KF6Mime.dll", args.dependency_prefix / "bin/qt6keychain.dll",
                args.native_bin / "intl-8.dll", args.crt_bin / "vcruntime140.dll"]
    required += [args.dependency_prefix / f"bin/sasl2/sasl{mechanism}.dll" for mechanism in ("PLAIN", "LOGIN", "XOAuth2")]
    for path in required:
        if not path.is_file() or any(part.lower() == "debug" for part in path.parts):
            raise ValueError(f"Required Release dependency missing or Debug selected: {path.name}; collect Release inputs manually")
    legal_manifest(args.legal_dir)
    if args.output.exists():
        raise ValueError("Output already exists; it will not be overwritten")


def build_package(args):
    preflight(args)
    jobs = ROOT / "build/package-windows"
    jobs.mkdir(parents=True, exist_ok=True)
    job = Path(tempfile.mkdtemp(prefix="release-", dir=jobs))
    build, stage = job / "build", job / "runtime"
    # All commands are argument arrays, never a shell command constructed from paths.
    subprocess.run([args.cmake, "-S", str(ROOT), "-B", str(build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                    "-DBUILD_TESTING=OFF", "-DSMOOTHMAIL_DISTRIBUTION=ON", f"-DQt6_ROOT={args.qt_root}",
                    f"-DCMAKE_PREFIX_PATH={args.dependency_prefix}", f"-DSMOOTHMAIL_SASL_PLUGIN_DIR={args.dependency_prefix / 'bin/sasl2'}"], check=True)
    subprocess.run([args.cmake, "--build", str(build), "--config", "Release", "--target", "smoothmail"], check=True)
    values = {"APP": build / "smoothmail.exe", "QT_BIN": args.qt_root / "bin", "DEPENDENCY_BIN": args.dependency_prefix / "bin",
              "NATIVE_BIN": args.native_bin, "SASL_DIR": args.dependency_prefix / "bin/sasl2", "CRT_BIN": args.crt_bin, "DESTINATION": stage, "CONFIGURATION": "Release"}
    subprocess.run([args.cmake, *(f"-D{key}={value}" for key, value in values.items()), "-P", str(ROOT / "cmake/StageWindowsRelease.cmake")], check=True)
    prepare_legal(stage, args.legal_dir)
    roots = [ROOT, job, args.qt_root, args.dependency_prefix, args.native_bin, args.crt_bin, args.legal_dir]
    roots += [Path(os.environ[key]) for key in ("USERPROFILE", "TEMP") if os.environ.get(key)]
    make_archive(stage, args.output, roots)
    print(f"Created {args.output.name} (Release, privacy/coverage checks passed)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("qt-root", "dependency-prefix", "native-bin", "crt-bin", "legal-dir", "output"):
        parser.add_argument("--" + name, type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("Windows/MSVC x64 developer environment required")
    try:
        build_package(args)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        # Inputs may include private local paths; keep the final diagnostic generic.
        detail = str(error) if type(error) is ValueError else type(error).__name__
        parser.exit(1, f"Packaging failed: {detail}. No distributable was published.\n")


if __name__ == "__main__":
    main()
