"""Shared, platform-independent release batch primitives (standard library only)."""

import hashlib
import json
import os
import re
import stat
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(*args, capture=False, env=None):
    result = subprocess.run([str(arg) for arg in args], check=True, env=env,
                            stdout=subprocess.PIPE if capture else None,
                            encoding="utf-8", errors="strict" if capture else "replace")
    return result.stdout.strip() if capture else None


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def linked(path):
    path = Path(path)
    if path.is_symlink():
        return True
    try:
        return bool(getattr(path.lstat(), "st_file_attributes", 0)
                    & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))
    except FileNotFoundError:
        return False


def regular(base, relative):
    if not relative or re.search(r"(^/|\\|:|(^|/)\.\.(/|$)|[;\r\n])", relative):
        raise ValueError("Unsafe evidence reference")
    base = Path(base).resolve()
    path = base / relative
    if not path.resolve().is_relative_to(base) or not path.is_file() or linked(path):
        raise ValueError("Missing or linked evidence reference")
    parent = path.parent
    while parent != base:
        if linked(parent):
            raise ValueError("Linked evidence directory")
        parent = parent.parent
    return path


def new_output(path):
    if path.exists() or linked(path):
        raise ValueError("Output already exists; it will not be overwritten")


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cmake_script(name, definitions):
    run("cmake", *(f"-D{key}={value}" for key, value in definitions.items() if value is not None),
        "-P", ROOT / "cmake" / name)


def tools(qt, dependencies, extra=None):
    definitions = {"Qt6_ROOT": qt, "DEPENDENCY_ROOT": dependencies, **(extra or {})}
    cmake_script("BuildDistributionTools.cmake", definitions)
    binary = dependencies / "build/tools/package"
    return binary / "smoothmail_package.exe", Path((binary / "crt-verifier.txt").read_text())


def qt_environment(qt):
    return {**os.environ, "PATH": str(qt / "bin") + os.pathsep + os.environ.get("PATH", "")}
