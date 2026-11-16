"""Collect a Release runtime without using CMake for deployment."""

import argparse
import re
import shutil
import subprocess
from pathlib import Path

from release_common import ROOT, linked, new_output, run, tools


CRT = re.compile(r"^(msvcp140(_[12]|_atomic_wait|_codecvt_ids)?|vcruntime140(_1|_threads)?|concrt140|vccorlib140)\.dll$")
CRT_FAMILY = re.compile(r"^(msvcp|vcruntime|concrt|vccorlib)[a-z0-9_]*\.dll$")
DEBUG = re.compile(r"^(qt6.*d|ucrtbased|msvcp.*d(_.*)?|vcruntime.*d|concrt.*d|vccorlib.*d)\.dll$")
SYSTEM = set("""advapi32.dll authz.dll bcrypt.dll cfgmgr32.dll comctl32.dll
comdlg32.dll crypt32.dll d2d1.dll d3d9.dll d3d11.dll d3d12.dll dbghelp.dll
dnsapi.dll dwmapi.dll dwrite.dll dxgi.dll gdi32.dll icuuc.dll imm32.dll
iphlpapi.dll kernel32.dll ktmw32.dll mpr.dll mscms.dll msimg32.dll mswsock.dll msvcrt.dll msvcp_win.dll
ncrypt.dll netapi32.dll normaliz.dll ntdll.dll ole32.dll oleaut32.dll opengl32.dll
powrprof.dll propsys.dll psapi.dll rpcrt4.dll runtimeobject.dll secur32.dll
setupapi.dll shcore.dll shell32.dll shlwapi.dll ucrtbase.dll uiautomationcore.dll
user32.dll userenv.dll uxtheme.dll version.dll windowscodecs.dll winhttp.dll
wininet.dll winmm.dll winspool.drv ws2_32.dll wsock32.dll wtsapi32.dll wldap32.dll""".split())
PLUGINS = ("platforms/qwindows.dll", "sqldrivers/qsqlite.dll", "tls/qschannelbackend.dll")
SASL = ("saslPLAIN.dll", "saslLOGIN.dll", "saslXOAuth2.dll")


def crt_directory(path):
    if linked(path) or not path.is_dir() or not re.search(
            r"/vc/redist/msvc/[^/]+/x64/microsoft\.vc143\.crt$", path.resolve().as_posix().lower()):
        raise ValueError("CRT_BIN must be the installed VS 2022 x64 Release VC143 redistributable directory")
    if not (path / "vcruntime140.dll").is_file():
        raise ValueError("Missing redistributable Release CRT")
    return path.resolve()


def imports(binary):
    # dumpbin emits OEM/localized prose; only ASCII DLL basenames are consumed.
    result = subprocess.run(["dumpbin", "/DEPENDENTS", str(binary)], check=True, stdout=subprocess.PIPE)
    return set(match.decode("ascii").lower() for match in re.findall(
        rb"(?:^|[\r\n])[ \t]+([a-zA-Z0-9_.-]+\.dll)(?=[ \t\r\n]|$)", result.stdout))


def resolve(roots, directories, crt, inspect=imports):
    directories = [path.resolve() for path in directories]
    crt = crt.resolve()
    queue, visited, resolved, crts = list(roots), set(), {}, set()
    while queue:
        binary = queue.pop()
        if binary in visited:
            continue
        visited.add(binary)
        for name in sorted(inspect(binary)):
            if DEBUG.fullmatch(name):
                raise ValueError(f"Debug dependency is not distributable: {name}")
            if name in SYSTEM or name.startswith(("api-ms-", "ext-ms-")):
                continue
            if CRT_FAMILY.fullmatch(name):
                if not CRT.fullmatch(name):
                    raise ValueError("Unreviewed MSVC runtime import")
                candidates = [crt / name]
                crts.add((crt / name).resolve())
            else:
                candidates = [directory / name for directory in directories if (directory / name).is_file()]
            if len(candidates) != 1:
                raise ValueError(f"Unresolved or conflicting Release runtime import: {name}")
            candidate = candidates[0]
            if not candidate.is_file() or linked(candidate) or candidate.resolve().parent != candidate.parent:
                raise ValueError("Runtime resolved outside explicit input directories")
            if name in resolved and resolved[name] != candidate:
                raise ValueError("Conflicting runtime import")
            if name not in resolved:
                # Keep the supplier's on-disk spelling in the ZIP/manifest,
                # even though Windows import lookup is case-insensitive.
                resolved[name] = candidate.resolve()
                queue.append(candidate.resolve())
    if not crts:
        raise ValueError("No required Release CRT imports found")
    return sorted(set(resolved.values())), sorted(crts)


def stage(app, qt_bin, dependency_bin, native_bin, sasl_dir, crt_bin, destination, verifier):
    new_output(destination)
    crt = crt_directory(crt_bin)
    qt_bin = qt_bin.resolve()
    plugin_files = [qt_bin.parent / "plugins" / name for name in PLUGINS]
    sasl_files = [sasl_dir / name for name in SASL]
    roots = [app, *plugin_files, *sasl_files]
    if any(not path.is_file() or linked(path) for path in roots):
        raise ValueError("Missing or linked Release application/plugin")
    libraries, crts = resolve(roots, [dependency_bin, native_bin, qt_bin, crt], crt)
    for library in crts:
        run(verifier, library, library, crt / "vcruntime140.dll")
    qt_libraries = [path for path in libraries if path.parent == qt_bin]
    if not qt_libraries:
        raise ValueError("No Release Qt runtime libraries resolved")
    destination.mkdir(parents=True)
    for path in [app, *libraries]:
        shutil.copyfile(path, destination / path.name)
    for name, path in zip(PLUGINS, plugin_files):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    (destination / "sasl2").mkdir()
    for path in sasl_files:
        shutil.copyfile(path, destination / "sasl2" / path.name)
    run(qt_bin / "windeployqt.exe", "--release", "--no-compiler-runtime", "--no-translations",
        "--no-plugins", "--no-opengl-sw", "--no-system-d3d-compiler", "--no-system-dxc-compiler",
        "--no-ffmpeg", "--qtpaths", qt_bin / "qtpaths.exe", "--dir", destination, *qt_libraries)
    for library in crts:
        run(verifier, library, destination / library.name, crt / "vcruntime140.dll")
    (destination / "qt.conf").write_bytes(b"[Paths]\nPrefix=.\nPlugins=.\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("app", "qt-bin", "dependency-bin", "native-bin", "sasl-dir", "crt-bin", "destination"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--dependency-root", type=Path, default=ROOT / "build/deps")
    args = parser.parse_args()
    try:
        new_output(args.destination)
        crt_directory(args.crt_bin)
        if not args.app.is_file() or linked(args.app):
            raise ValueError("Missing or linked Release application")
        _, verifier = tools(args.qt_bin.resolve().parent, args.dependency_root.resolve())
        stage(args.app.resolve(), args.qt_bin.resolve(), args.dependency_bin.resolve(),
              args.native_bin.resolve(), args.sasl_dir.resolve(), args.crt_bin,
              args.destination, verifier)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release staging failed: {error}\n")


if __name__ == "__main__":
    main()
