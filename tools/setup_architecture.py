"""Install an isolated, binary-only documentation toolchain; no SDK/compiler needed."""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path, PurePosixPath

from release_common import linked

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOLS = ROOT / ".cache/architecture"
UV_VERSION = "0.9.5"
PYTHON_VERSION = "3.13.9"
DOXYGEN_VERSION = "1.18.0"
ASSETS = {
    ("Windows", "x86_64"): (
        "uv-x86_64-pc-windows-msvc.zip", "515dc53d7553f1357d0abc1f70acd921fbb9e30230b1d9a08737236daa6ee920",
        "doxygen-1.18.0.windows.x64.bin.zip", "e84f54cfd49ef06b0b16536056dbec0c496323de28abcce53a4269463de35eaf"),
    ("Linux", "x86_64"): (
        "uv-x86_64-unknown-linux-gnu.tar.gz", "2cf10babba653310606f8b49876cfb679928669e7ddaa1fb41fb00ce73e64f66",
        "doxygen-1.18.0.linux.bin.tar.gz", "14fa81bdc34171edb5f1f02b1d60e74802f0439b77fa44e592565d517d72df90"),
    ("Darwin", "arm64"): (
        "uv-aarch64-apple-darwin.tar.gz", "dc098ff224d78ed418e121fd374f655949d2c7031a70f6f6604eaf016a130433",
        "doxygen-1.18.0-mac-arm.zip", "31d1c74467a9f6f456b4e6a4eff0c03e93d1ffa92da5fdf2d53a2f14ebaabbd7"),
    ("Darwin", "x86_64"): (
        "uv-x86_64-apple-darwin.tar.gz", "58b1d4a25aa8ff99147c2550b33dcf730207fe7e0f9a0d5d36a1bbf36b845aca",
        "doxygen-1.18.0-mac-intel.zip", "8045d72f6f900fd430042339c1493d6eefcc85a7f990361fb57685682f16a2f9"),
}


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def download(url, destination, expected):
    if not url.startswith("https://"):
        raise ValueError("Tool downloads require HTTPS")
    if linked(destination):
        raise ValueError("Linked download cache")
    if destination.exists():
        if sha256(destination) != expected:
            raise ValueError("Tool cache digest mismatch; choose a new tool directory")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
        temporary = Path(output.name)
        try:
            with urllib.request.urlopen(url, timeout=180) as response:
                if not response.url.startswith("https://"):
                    raise ValueError("Insecure tool download redirect")
                shutil.copyfileobj(response, output)
        except BaseException:
            output.close()
            temporary.unlink()
            raise
    try:
        if sha256(temporary) != expected:
            raise ValueError("Tool download digest mismatch")
        # Exclusive publication, including a concurrently created cache file.
        os.link(temporary, destination)
    finally:
        temporary.unlink()


def archive_path(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or "\\" in name or ":" in name:
        raise ValueError("Unsafe tool archive path")
    return path


def unpack(archive, destination):
    """Only regular files/directories; no links or privileged permission bits."""
    if destination.exists() or linked(destination):
        raise ValueError("Choose a new extraction directory")
    destination.mkdir(parents=True)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as bundle:
            for item in bundle.infolist():
                name = archive_path(item.filename)
                if (item.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError("Linked tool archive member")
                target = destination / name
                if item.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.open(item) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output)
                    target.chmod(0o755)
    else:
        with tarfile.open(archive) as bundle:
            for item in bundle:
                name = archive_path(item.name)
                target = destination / name
                if item.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                elif item.isfile():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with bundle.extractfile(item) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output)
                    target.chmod(item.mode & 0o755)
                else:
                    raise ValueError("Nonregular tool archive member")


def verify_unpacked(archive, directory):
    """Bind reusable executables/DLLs to the already hash-verified archive."""
    if linked(directory):
        raise ValueError("Linked tool directory")
    expected = set()
    def check(name, stream):
        relative = archive_path(name).as_posix()
        path = directory / relative
        if linked(path) or not path.is_file() or sha256(path) != hashlib.file_digest(stream, "sha256").hexdigest():
            raise ValueError("Extracted tool bytes differ from the pinned archive")
        expected.add(relative)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as bundle:
            for item in bundle.infolist():
                if not item.is_dir():
                    with bundle.open(item) as stream:
                        check(item.filename, stream)
    else:
        with tarfile.open(archive) as bundle:
            for item in bundle:
                if item.isfile():
                    with bundle.extractfile(item) as stream:
                        check(item.name, stream)
    actual = set()
    for path in directory.rglob("*"):
        if linked(path):
            raise ValueError("Linked extracted tool entry")
        if path.is_file() and path.name != ".archive-sha256":
            actual.add(path.relative_to(directory).as_posix())
    if actual != expected:
        raise ValueError("Unexpected extracted tool files; choose a new tool directory")


def executable(directory, name):
    matches = list(directory.rglob(name + (".exe" if os.name == "nt" else "")))
    if len(matches) != 1 or not matches[0].is_file():
        raise ValueError(f"Cannot uniquely locate {name}")
    return matches[0].resolve()


def setup(directory):
    if not shutil.which("cmake"):
        raise ValueError("Install CMake and put it on PATH")
    machine = platform.machine().lower()
    machine = {"amd64": "x86_64", "aarch64": "arm64"}.get(machine, machine)
    assets = ASSETS.get((platform.system(), machine))
    if assets is None:
        raise ValueError("No pinned binary documentation toolchain for this platform")
    directory = directory.absolute()
    directory.mkdir(parents=True, exist_ok=True)
    lock = directory / "setup.lock"
    lock.mkdir()  # Another setup must not modify the same private environment.
    try:
        for name, expected, origin, subdir in (
            (*assets[:2], f"https://github.com/astral-sh/uv/releases/download/{UV_VERSION}/", "uv"),
            (*assets[2:], f"https://github.com/doxygen/doxygen/releases/download/Release_{DOXYGEN_VERSION.replace('.', '_')}/", "doxygen"),
        ):
            archive = directory / "downloads" / name
            download(origin + name, archive, expected)
            unpacked = directory / subdir
            receipt = unpacked / ".archive-sha256"
            if not receipt.exists():
                unpack(archive, unpacked)
                receipt.write_text(expected, encoding="utf-8")
            elif receipt.read_text(encoding="utf-8") != expected:
                raise ValueError("Extracted tool identity differs; choose a new tool directory")
            verify_unpacked(archive, unpacked)
        uv = executable(directory / "uv", "uv")
        environment = {**os.environ, "UV_CACHE_DIR": str(directory / "uv-cache"),
                       "UV_PYTHON_INSTALL_DIR": str(directory / "python"),
                       "UV_PYTHON_PREFERENCE": "only-managed", "UV_NO_PROGRESS": "1"}
        venv = directory / "environment"
        python = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        if not python.exists():
            subprocess.run([str(uv), "--native-tls", "--no-config", "venv", "--python", PYTHON_VERSION, str(venv)],
                           check=True, env=environment)
        subprocess.run([str(uv), "--native-tls", "--no-config", "pip", "install", "--python", str(python),
                        "--only-binary", ":all:", "-r", str(ROOT / "tools/architecture-requirements.txt")],
                       check=True, env=environment)
        subprocess.run([str(python), "-c", "import tree_sitter,tree_sitter_cpp,pygraphviz,ninja"], check=True)
        doxygen = executable(directory / "doxygen", "doxygen")
        subprocess.run([str(doxygen), "--version"], check=True)
        (directory / "toolchain.json").write_text(json.dumps({
            "schema": 1, "python": str(python), "doxygen": str(doxygen),
            "requirements_sha256": sha256(ROOT / "tools/architecture-requirements.txt"),
        }, indent=2) + "\n", encoding="utf-8")
    finally:
        lock.rmdir()
    print("Architecture toolchain ready")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tools-root", type=Path, default=DEFAULT_TOOLS)
    args = parser.parse_args()
    try:
        setup(args.tools_root)
    except (ValueError, OSError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Architecture setup failed: {error}\n")


if __name__ == "__main__":
    main()
