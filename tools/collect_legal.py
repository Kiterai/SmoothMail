"""Collect license evidence for a fixed Windows runtime, without approving release."""

import argparse
import json
import re
import shutil
import subprocess
from contextlib import ExitStack
from pathlib import Path

from collect_sources import QT, SourceInputs
from release_common import ROOT, digest, linked, new_output, read_json, regular, write_json
from stage_windows_release import CRT


KDE = dict(zip(("kcodecs", "kconfig", "kcoreaddons", "kcrash", "ki18n", "kio", "kmime", "kservice", "kimap", "ksmtp"),
               ("KF6Codecs.dll", "KF6ConfigCore.dll", "KF6CoreAddons.dll", "KF6Crash.dll", "KF6I18n.dll",
                "KF6KIOCore.dll", "KF6Mime.dll", "KF6Service.dll", "KPim6IMAP.dll", "KPim6SMTP.dll")))


def attribution_files(source, attribution, item):
    references = []
    for field in ("LicenseFile", "CopyrightFile", "LicenseFiles", "CopyrightFiles"):
        if field not in item:
            continue
        value = item[field]
        if field.endswith("Files"):
            if not isinstance(value, list) or any(not isinstance(name, str) for name in value):
                raise ValueError("Qt attribution file list must contain strings")
            references.extend(value)
        elif not isinstance(value, str):
            raise ValueError("Qt attribution file reference must be a string")
        elif value:
            references.append(value)
    files = set()
    for name in references:
        if re.search(r"[;\r\n:]|^[/\\]", name):
            raise ValueError("Unsafe Qt attribution file reference")
        candidate = attribution.parent / name
        if linked(candidate):
            raise ValueError("Linked Qt attribution file reference")
        resolved = candidate.resolve()
        if not resolved.is_relative_to(source.resolve()):
            raise ValueError("Unsafe Qt attribution file reference")
        # Relative '..' within the source is legitimate in Qt attributions.
        files.add(regular(source, resolved.relative_to(source.resolve()).as_posix()))
    return files


def collect(stage, output, dependencies, archives=None, native_root=None):
    new_output(output)
    if not stage.is_dir():
        raise ValueError("Choose an existing runtime")
    manifest = read_json(ROOT / "cmake/dependencies.json")
    native = (native_root or dependencies / "native/gnu-runtime-v1") / "x64-windows"
    assigned, inventory, components = set(), "SHA256  runtime-relative-file  component\n", []
    # An incomplete output remains review-only and cannot be used for release.
    output.mkdir(parents=True)
    def component(name, version, binaries, source):
        nonlocal inventory
        for binary in binaries:
            if binary in assigned:
                raise ValueError("Missing or multiply assigned runtime file")
            path = regular(stage, binary)
            assigned.add(binary)
            inventory += f"{digest(path)}  {binary}  {name}\n"
        notice = f"notices/{name}.md"
        path = output / notice
        path.parent.mkdir(exist_ok=True)
        path.write_text(
            f"# {name} {version}\n\nREVIEW ONLY - NOT APPROVED FOR DISTRIBUTION.\n\nSource: {source}\n\n"
            "Source delivery policy: corresponding source is offered separately on the SAME page as the binary download. "
            "No publication URL is invented by this draft.\n\n"
            "See REVIEW.md for unresolved source evidence and redistribution checks.\n", encoding="utf-8")
        item = {"name": name, "version": version, "binaries": binaries, "license_files": [], "source_notice": notice}
        components.append(item)
        return item

    def original(item, source, name):
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, path)
        item["license_files"].append(name)

    def append_notice(item, text):
        with (output / item["source_notice"]).open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(text)

    version = re.search(r"project\(SmoothMail VERSION ([0-9]+\.[0-9]+\.[0-9]+)", (ROOT / "CMakeLists.txt").read_text())
    if not version:
        raise ValueError("Cannot determine application version")
    item = component("SmoothMail", version[1], ["smoothmail.exe", "qt.conf"],
                     "This repository; LICENSE and license templates. Complete release source and build scripts must accompany the selected delivery method.")
    original(item, ROOT / "LICENSE", "licenses/SmoothMail/LICENSE")
    with ExitStack() as stack:
        inputs = SourceInputs(dependencies, archives, stack)
        for name, binary in KDE.items():
            info = manifest[name]
            source = inputs.source(name)
            item = component(name, info["tag"], [binary],
                             f'{info["repository"]} at {info["commit"]}; SmoothMail changes, if any, are copied alongside this notice.')
            licenses = sorted((source / "LICENSES").glob("*"))
            if not licenses:
                raise ValueError("Missing KDE license set")
            for path in licenses:
                original(item, path, f"licenses/{name}/{path.name}")
            for path in sorted((ROOT / "patches" / name).glob("*")):
                if path.suffix == ".patch" or path.name == "README.md":
                    shutil.copyfile(path, output / "notices" / f"{name}-{path.name}")
            copyrights = (f"# Upstream source copyright evidence - {name}\n\n"
                          "Repository-wide src scan; compiled-file attribution review remains required.\n")
            for path in sorted((source / "src").rglob("*")):
                if path.suffix not in (".cpp", ".h", ".c"):
                    continue
                lines = [line for line in path.read_text(encoding="utf-8", errors="replace").splitlines()
                         if re.search(r"SPDX-FileCopyrightText:|[Cc]opyright.*[0-9]", line)]
                if lines:
                    copyrights += "\n" + path.relative_to(source).as_posix() + "\n" + "\n".join(lines) + "\n"
            name_in_output = f"licenses/{name}/copyright-evidence.txt"
            (output / name_in_output).write_text(copyrights, encoding="utf-8")
            item["license_files"].append(name_in_output)
        prefix = dependencies / "install/Release/share/licenses"
        item = component("qtkeychain", "0.15.0", ["qt6keychain.dll"],
                         "https://github.com/frankosterfeld/qtkeychain at ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7; unchanged.")
        for name in ("COPYING", "keychain.h"):
            original(item, regular(prefix, "qtkeychain/" + name), "licenses/qtkeychain/" + name)
        item = component("cyrus-sasl", "2.1.28", ["libsasl.dll", "sasl2/saslPLAIN.dll", "sasl2/saslLOGIN.dll"],
                         "https://github.com/cyrusimap/cyrus-sasl/releases/tag/cyrus-sasl-2.1.28; "
                         "tar.gz SHA256 7ccfc6abd01ed67c1a0924b353e526f1b766b21f42d4562ee635a8ebfc5bb38c. "
                         "Client mechanisms only; see cmake/BuildSaslDependencies.cmake.")
        original(item, regular(prefix, "cyrus-sasl/COPYING"), "licenses/cyrus-sasl/COPYING")
        append_notice(item, "\nThis product includes software developed by Computing Services at Carnegie Mellon University (http://www.cmu.edu/computing/).\n")
        item = component("cyrus-sasl-xoauth2", "36aabca", ["sasl2/saslXOAuth2.dll"],
                         "https://github.com/moriyoshi/cyrus-sasl-xoauth2 at 36aabca54fd65c8fa7a707cb4936751599967904; "
                         "build integration: cmake/BuildXOAuth2Dependency.cmake.")
        original(item, regular(prefix, "cyrus-sasl-xoauth2/COPYING"), "licenses/cyrus-sasl-xoauth2/COPYING")
        for name, version, binary, url, headers in (
                ("libiconv", "1.19", "iconv-2.dll", "https://ftp.gnu.org/gnu/libiconv/libiconv-1.19.tar.gz", ("iconv.h", "localcharset.h")),
                ("gettext-libintl", "0.22.5", "intl-8.dll", "https://ftp.gnu.org/gnu/gettext/gettext-0.22.5.tar.gz", ("libintl.h",))):
            item = component(name, version, [binary], url + "; fixed vcpkg commit 177e8175121536d414f405ed8b3099b325a42638, "
                             "its port patches, and cmake/native-triplets/x64-windows.cmake are required build inputs. "
                             "No new GNU source patch was added by SmoothMail.")
            original(item, regular(native, f"share/{name}/copyright"), f"licenses/{name}/copyright")
            for header in headers:
                original(item, regular(native, "include/" + header), f"licenses/{name}/{header}")
        for module in QT:
            binaries = (["Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Sql.dll", "Qt6Widgets.dll", "Qt6Xml.dll",
                         "platforms/qwindows.dll", "sqldrivers/qsqlite.dll", "tls/qschannelbackend.dll"]
                        if module == "qtbase" else ["Qt6Svg.dll"])
            item = component(module, "6.12.0", binaries,
                             f"https://code.qt.io/qt/{module}.git at {QT[module]} per SDK SBOM. "
                             "LGPL-3.0-only open-source alternative selected; bundled third-party code retains its own licenses.")
            source = inputs.source(module)
            files = set()
            for path in source.rglob("*"):
                if not path.is_file():
                    continue
                relative = path.relative_to(source).as_posix()
                if ((relative.startswith("LICENSES/") and path.suffix == ".txt")
                        or (relative.startswith("src/") and (re.fullmatch(
                            r"(LICENSE|LICENCE|COPYING|COPYRIGHT)S?([._-].*)?", path.name.upper())
                            or path.name == "qt_attribution.json"))):
                    files.add(path)
                if relative.startswith("src/") and path.name == "qt_attribution.json":
                    # Qt attribution metadata permits literal newlines in prose
                    # strings (as did CMake's JSON parser). Keep original bytes
                    # unchanged; only this upstream metadata uses relaxed strings.
                    data = json.loads(path.read_text(encoding="utf-8"), strict=False)
                    for attribution in data if isinstance(data, list) else [data]:
                        files.update(attribution_files(source, path, attribution))
            if not files:
                raise ValueError("Missing Qt license evidence")
            for path in sorted(files):
                relative = path.relative_to(source).as_posix()
                original(item, regular(source, relative), f"licenses/{module}/{relative}")
            append_notice(item, "\nModule license choice: LGPL-3.0-only (open-source), plus the applicable bundled third-party licenses. "
                          "Collected repository-wide evidence still needs mapping to the shipped modules.\n")
            if module == "qtbase":
                append_notice(item, "\nFor the FreeType alternative used in bundled Qt GUI, select FTL rather than GPL-2.0-only. "
                              "Portions of this software are copyright (C) 2026 The FreeType Project (www.freetype.org). All rights reserved.\n")
    crts = sorted(path.name for path in stage.glob("*.dll") if re.match(r"(msvcp|vcruntime|concrt|vccorlib)", path.name))
    if not crts or any(not CRT.fullmatch(name) for name in crts):
        raise ValueError("Missing or unreviewed CRT component")
    item = component("msvc-crt", "MSVC v14", crts,
                     "Microsoft Visual Studio 2022 Release CRT, required DLLs distributed unmodified in the portable ZIP. "
                     "The distribution operator is responsible for lawful MSVC 2022 use and compliance with the applicable "
                     "Microsoft terms/REDIST list. Build checks verify technical provenance, not legal entitlement.")
    append_notice(item, "\nMicrosoft runtime code is not relicensed under SmoothMail's MIT license. Preserve applicable Microsoft notices and conditions. "
                  "Source: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution and "
                  "https://visualstudio.microsoft.com/license-terms/vs2022-cruntime/ . App-local CRT security updates must be delivered by the application distributor.\n")
    actual = {path.relative_to(stage).as_posix() for path in stage.rglob("*") if path.is_file()}
    if actual != assigned:
        raise ValueError("Runtime inventory differs from component mapping")
    write_json(output / "manifest.review.json", {"schema": 1, "review_only": True, "components": components})
    (output / "runtime-components-sha256.txt").write_bytes(inventory.encode("utf-8"))
    shutil.copyfile(ROOT / "THIRD_PARTY_NOTICES.md", output / "THIRD_PARTY_NOTICES.md")
    shutil.copyfile(ROOT / "docs/dev/distribution.md", output / "REVIEW.md")
    print(f"Collected {len(components)} components. REVIEW ONLY: no manifest.json, no release approval")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--dependency-root", type=Path, default=ROOT / "build/deps")
    parser.add_argument("--source-archives", type=Path)
    parser.add_argument("--native-root", type=Path)
    args = parser.parse_args()
    try:
        collect(args.stage.absolute(), args.output.absolute(), args.dependency_root.absolute(),
                args.source_archives, args.native_root)
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Legal collection failed: {error}\n")


if __name__ == "__main__":
    main()
