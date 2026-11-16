"""Assemble a local-only Debug runtime; compilation is owned by CMake."""

import os
import shutil
from pathlib import Path

from release_common import digest, linked, new_output, run
from stage_windows_release import SYSTEM, imports


PLUGINS = ("platforms/qwindowsd.dll", "sqldrivers/qsqlited.dll", "tls/qschannelbackendd.dll")
SASL = ("saslPLAIN.dll", "saslLOGIN.dll", "saslXOAuth2.dll")


def resolve(roots, directories, inspect=imports):
    queue, visited, resolved = list(roots), set(), {}
    while queue:
        binary = queue.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for name in sorted(inspect(binary)):
            if name in SYSTEM or name.startswith(("api-ms-", "ext-ms-")):
                continue
            candidates = [directory / name for directory in directories if (directory / name).is_file()]
            if len(candidates) != 1 or linked(candidates[0]):
                raise ValueError(f"Unresolved or conflicting Debug runtime import: {name}")
            path = candidates[0].resolve()
            if name not in resolved:
                resolved[name] = path
                queue.append(path)
    return sorted(set(resolved.values()))


def stage(app, qt, dependency_bin, native_bin, sasl_dir, destination):
    new_output(destination)
    redist = os.environ.get("VCToolsRedistDir")
    crt = list(Path(redist).glob("debug_nonredist/x64/Microsoft.VC*.DebugCRT")) if redist else []
    ucrt = Path(os.environ.get("UniversalCRTSdkDir", "")) / "bin" / os.environ.get("UCRTVersion", "") / "x64/ucrt"
    if len(crt) != 1 or not (ucrt / "ucrtbased.dll").is_file():
        raise ValueError("MSVC x64 developer environment with Debug CRT/UCRT required")
    plugins = [qt / "plugins" / name for name in PLUGINS]
    sasl = [sasl_dir / name for name in SASL]
    roots = [app, *plugins, *sasl]
    if any(not path.is_file() or linked(path) for path in roots):
        raise ValueError("Missing or linked Debug application/plugin")
    qt_bin = (qt / "bin").resolve()
    libraries = resolve(roots, [dependency_bin, native_bin, qt_bin, crt[0], ucrt])
    qt_libraries = [path for path in libraries if path.parent == qt_bin]
    if not qt_libraries:
        raise ValueError("No Debug Qt runtime libraries resolved")
    destination.mkdir(parents=True)
    for path in (app, *libraries):
        shutil.copyfile(path, destination / path.name)
    for name, path in zip(PLUGINS, plugins):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    (destination / "sasl2").mkdir()
    for path in sasl:
        shutil.copyfile(path, destination / "sasl2" / path.name)
    run(qt_bin / "windeployqt.exe", "--debug", "--no-compiler-runtime", "--no-translations",
        "--no-plugins", "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
        "--no-ffmpeg", "--no-patchqt", "--qtpaths", qt_bin / "qtpaths.exe",
        "--dir", destination, *qt_libraries)
    (destination / "qt.conf").write_bytes(b"[Paths]\nPrefix=.\nPlugins=.\n")
    (destination / "launch.cmd").write_bytes(
        b'@echo off\r\nsetlocal\r\nset "QT_QPA_PLATFORM=windows"\r\n'
        b'set "QT_PLUGIN_PATH=%~dp0"\r\n'
        b'set "QT_QPA_PLATFORM_PLUGIN_PATH=%~dp0platforms"\r\n'
        b'set "SMOOTHMAIL_SASL_PATH=%~dp0sasl2"\r\n'
        b'start "" /D "%~dp0" "%~dp0smoothmail.exe" %*\r\n')
    (destination / "DEVELOPMENT-ONLY.txt").write_text(
        "Local Debug runtime. Contains non-redistributable Debug CRT.\n"
        "Never distribute this directory. OAuth defaults are embedded at build time.\n", encoding="utf-8")
    (destination / "runtime-sha256.txt").write_text(
        "SHA256  relative path\n" + "".join(
            f"{digest(path)}  {path.relative_to(destination).as_posix()}\n"
            for path in sorted(destination.rglob("*")) if path.is_file()), encoding="utf-8")
