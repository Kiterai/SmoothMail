"""Render review-only license notices from collected evidence, never approve them."""

import argparse
import re
import shutil
import subprocess
from pathlib import Path

from release_common import ROOT, digest, linked, new_output, read_json, regular, run, write_json
from stage_windows_release import crt_directory


KEYS = {"SOFTWARE_NAME", "DISTRIBUTION_URL", "BUILD_VERSION"}
BUILD_KEYS = {"SMOOTHMAIL_QT_ROOT", "SMOOTHMAIL_DEPENDENCY_ROOT",
              "SMOOTHMAIL_CRT_BIN", "SMOOTHMAIL_DOWNLOAD_CURL",
              "SMOOTHMAIL_SOURCE_ARCHIVES", "SMOOTHMAIL_SOURCE_COMMIT",
              "SMOOTHMAIL_ASSET_CACHE"}
OAUTH_KEYS = {"SMOOTHMAIL_GOOGLE_CLIENT_ID", "SMOOTHMAIL_GOOGLE_CLIENT_SECRET",
              "SMOOTHMAIL_MICROSOFT_CLIENT_ID"}
PLACEHOLDER = re.compile(r"@([A-Za-z_][A-Za-z0-9_]*)@")
ALLOWED = KEYS | {
    "EDITION", "COMPONENT_ROWS", "SOURCE_ROWS", "COMPONENT_NAME", "COMPONENT_VERSION",
    "BINARY_LIST", "LICENSE_LIST", "SOURCE_DETAILS", "NOTICE_STATUS_EN", "NOTICE_STATUS_JA",
    "REVIEW_BANNER_EN", "REVIEW_BANNER_JA"}


def read_env(path, include_build=False):
    if not path.is_file() or linked(path):
        raise ValueError("Specify an existing regular license env file; see .env.example")
    values = {}
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeError:
        raise ValueError("License environment file must be UTF-8") from None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r"([A-Z_]+)=(.*)", line)
        if not match:
            raise ValueError("Invalid license environment assignment")
        key, value = match.groups()
        if key not in KEYS | OAUTH_KEYS | BUILD_KEYS or key in values:
            raise ValueError("Unknown or duplicate license environment key")
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        if (not value and key in KEYS) or any(character in value for character in "\r\n\t;`<>\\#*[]@\"'") or "${" in value:
            raise ValueError("Unsafe license environment value; expansion is not supported")
        values[key] = value
    if not KEYS <= values.keys():
        raise ValueError("Required license environment key is missing")
    if not re.fullmatch(r"https://[A-Za-z0-9][A-Za-z0-9.-]*(:[0-9]+)?([/?][A-Za-z0-9._~/?&=%+-]*)?",
                        values["DISTRIBUTION_URL"]):
        raise ValueError("License distribution URL must be a plain HTTPS URL without credentials")
    # OAuth registration data must never enter notices or their metadata.
    return {key: value for key, value in values.items()
            if key in KEYS or (include_build and key in BUILD_KEYS)}


def render(text, values, final=False):
    values = {**values,
              "NOTICE_STATUS_EN": "" if final else "Draft for review. ",
              "NOTICE_STATUS_JA": "" if final else "審査用草案。",
              "REVIEW_BANNER_EN": "" if final else "**Draft for review; not approved for distribution.**",
              "REVIEW_BANNER_JA": "" if final else "**審査用草案。配布未承認。**"}
    def substitute(match):
        key = match[1]
        if key not in ALLOWED or key not in values:
            raise ValueError("Unknown or unset license template variable")
        return values[key]
    text = PLACEHOLDER.sub(substitute, text)
    if PLACEHOLDER.search(text):
        raise ValueError("Unresolved license template variable")
    return re.sub(r"\n([ \t]*\n)+", "\n\n", text.replace("\r\n", "\n"))


def check_runtime(evidence, stage, manifest, crt, verifier):
    if not stage.is_dir() or linked(stage):
        raise ValueError("Final license review requires a regular runtime stage")
    inventory, assignments = {}, []
    for row in (evidence / "runtime-components-sha256.txt").read_text(encoding="utf-8").splitlines():
        if row.startswith("SHA256 "):
            continue
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9_./-]+)  ([A-Za-z0-9-]+)", row)
        if not match:
            raise ValueError("Invalid runtime evidence inventory")
        expected, name, component = match.groups()
        if name in inventory or re.search(r"(^|/)\.\.?(/|$)", name):
            raise ValueError("Unsafe or duplicate runtime evidence entry")
        if digest(regular(stage, name)) != expected:
            raise ValueError("Runtime bytes changed since license evidence collection")
        inventory[name] = expected
        assignments.append((component, name))
    expected_assignments = [(item["name"], name) for item in manifest["components"] for name in item["binaries"]]
    if sorted(assignments) != sorted(expected_assignments):
        raise ValueError("Runtime inventory and license component assignments differ")
    actual = {path.relative_to(stage).as_posix() for path in stage.rglob("*") if path.is_file()}
    if set(inventory) != actual:
        raise ValueError("Runtime has missing or unclassified files")
    crt = crt_directory(crt)
    records, version = [], None
    for name in sorted(inventory):
        if not re.fullmatch(r"(msvcp|vcruntime|concrt|vccorlib).*\.dll", name):
            continue
        text = run(verifier, crt / name, stage / name, crt / "vcruntime140.dll", capture=True)
        match = re.fullmatch(r"version=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+) sha256=([0-9a-f]{64})", text)
        if not match or (version is not None and version != match[1]):
            raise ValueError("CRT provenance/version verification failed for final license review")
        version = match[1]
        records.append({"file": name, "version": version, "sha256": match[2]})
    if not records:
        raise ValueError("Missing verified runtime CRT version")
    return version, {"schema": 1, "review_only": True, "files": records}


def generate(args):
    distribution = getattr(args, "distribution", False)
    if distribution and not args.final_review:
        raise ValueError("Distribution assembly requires final runtime/source checks")
    new_output(args.output)
    values = read_env(args.env_file)
    if args.edition != "windows-x64":
        raise ValueError("No reviewed component profile for the requested license edition")
    values["EDITION"] = args.edition
    if args.final_review and re.match(
            r"https://([^/]*\.invalid|example\.(com|org|net)|localhost)([:/]|$)", values["DISTRIBUTION_URL"]):
        raise ValueError("Final license review cannot use an example distribution URL")
    manifest = read_json(regular(args.evidence, "manifest.review.json"))
    sources = read_json(regular(args.source_review, "source-manifest.review.json"))
    if sources.get("missing"):
        raise ValueError("Corresponding source collection is incomplete")
    if manifest.get("review_only") is not True or (args.evidence / "manifest.json").exists():
        raise ValueError("Expected unapproved review evidence")
    if len(manifest["components"]) != 19 or len(sources["archives"]) != 23:
        raise ValueError("Component/source profile changed; review the draft first")
    crt_version, crt_records = None, None
    if args.final_review:
        if any(value is None for value in (args.stage, args.crt_bin, args.application_build, args.crt_verifier)):
            raise ValueError("Final review requires stage, crt-bin, application-build and crt-verifier")
        cache = (args.application_build / "CMakeCache.txt").read_text(encoding="utf-8")
        commit = re.search(r"^SMOOTHMAIL_SOURCE_COMMIT:STRING=([0-9a-f]+)$", cache, re.M)
        app_sources = [item for item in sources["archives"] if item["component"] == "SmoothMail"]
        if not commit or len(app_sources) != 1 or commit[1] != app_sources[0].get("commit"):
            raise ValueError("Application source identity and corresponding source differ")
        crt_version, crt_records = check_runtime(
            args.evidence, args.stage, manifest, args.crt_bin, args.crt_verifier)
    values["SOURCE_ROWS"] = ""
    for item in sources["archives"]:
        name = item["file"]
        if digest(regular(args.source_review, name)) != item["sha256"]:
            raise ValueError("Source archive differs from supplied inventory")
        values["SOURCE_ROWS"] += f'| {item["component"]} | `{name}` | `{item["sha256"]}` |\n'
    for item in manifest["components"]:
        if not re.fullmatch(r"[A-Za-z0-9-]+", item["name"]):
            raise ValueError("Unsafe component identifier")
        regular(args.evidence, item["source_notice"])
        for name in item["license_files"]:
            regular(args.evidence, name)
        if not item["license_files"] and item["name"] != "msvc-crt":
            raise ValueError("Missing license originals")
    # Prepare all generated text and validate references before creating output.
    documents, assigned, components, components_ja = {}, set(), "", ""
    common = ROOT / "licenses/common"
    edition = ROOT / "licenses/editions" / args.edition
    def document(template, name, variables):
        documents[name] = render(template.read_text(encoding="utf-8"), variables, args.final_review)
    for name in ("README", "NOTICE", "SOURCE-AND-REPLACEMENT", "SOURCES"):
        for suffix in (".md", ".ja.md"):
            document(common / (name + suffix), name + suffix, values)
    for name in ("THIRD-PARTY", "CHANGES", "MICROSOFT-RUNTIME"):
        for suffix in (".md", ".ja.md"):
            document(edition / (name + suffix), name + suffix, values)
    for item in manifest["components"]:
        component = item["name"]
        if component == "SmoothMail":
            item["version"] = values["BUILD_VERSION"]
        elif component == "msvc-crt" and args.final_review:
            item["version"] = crt_version
        originals = []
        for name in item["license_files"]:
            if not name.startswith("licenses/"):
                raise ValueError("Unexpected original license location")
            originals.append("originals/" + name[len("licenses/"):])
        item["license_files"] = originals
        binaries = ""
        for name in item["binaries"]:
            if name in assigned:
                raise ValueError("Duplicate runtime assignment")
            assigned.add(name)
            binaries += f"- `{name}`\n"
        source_id = "gettext" if component == "gettext-libintl" else component
        details, details_ja = "", ""
        for source in sources["archives"]:
            if source["component"] == source_id:
                details += f'Source archive: `{source["file"]}`. SHA256: [source list](../SOURCES.md).\n'
                details_ja += f'ソースアーカイブ：`{source["file"]}`。SHA256：[ソース一覧](../SOURCES.ja.md)。\n'
        if component == "msvc-crt":
            details = ("Microsoft binaries are subject to the [Microsoft runtime conditions](../MICROSOFT-RUNTIME.md), "
                       "not the open-source licenses of the other components.\n")
            details_ja = ("Microsoftバイナリには[Microsoftランタイム適用条件](../MICROSOFT-RUNTIME.ja.md)を適用し、"
                          "他のコンポーネントのオープンソースライセンスは適用しない。\n")
            item["license_files"] = ["MICROSOFT-RUNTIME.md", "MICROSOFT-RUNTIME.ja.md"]
        elif not details:
            raise ValueError("Missing corresponding source component")
        variables = {**values, "COMPONENT_NAME": component, "COMPONENT_VERSION": item["version"],
                     "BINARY_LIST": binaries,
                     "LICENSE_LIST": "".join(f"- [{name}](../{name})\n" for name in item["license_files"])}
        for suffix, source_details in ((".md", details), (".ja.md", details_ja)):
            document(common / ("COMPONENT-NOTICE" + suffix), f"notices/{component}{suffix}",
                     {**variables, "SOURCE_DETAILS": source_details})
        item["source_notice"] = f"notices/{component}.md"
        item["license_files"].append(f"notices/{component}.ja.md")
        if component == "SmoothMail":
            for name in ("README", "NOTICE", "SOURCE-AND-REPLACEMENT", "THIRD-PARTY", "CHANGES",
                         "MICROSOFT-RUNTIME", "COMPONENTS", "SOURCES"):
                item["license_files"].extend([name + ".md", name + ".ja.md"])
        components += f'## {component} {item["version"]}\n\n{binaries}\n[License and source notices](notices/{component}.md)\n\n'
        components_ja += f'## {component} {item["version"]}\n\n{binaries}\n[ライセンス及びソース通知](notices/{component}.ja.md)\n\n'
    if len(assigned) != 34:
        raise ValueError("Runtime profile changed; do not use this draft unchanged")
    for suffix, rows in ((".md", components), (".ja.md", components_ja)):
        document(common / ("COMPONENTS" + suffix), "COMPONENTS" + suffix, {**values, "COMPONENT_ROWS": rows})
    if args.qt_sbom_review:
        sbom = read_json(args.qt_sbom_review)
        if sbom.get("review_only") is not True or sbom.get("unresolved") != []:
            raise ValueError("Expected a resolved review-only Qt SBOM closure")
    originals_root = args.evidence / "licenses"
    if linked(originals_root) or not originals_root.is_dir():
        raise ValueError("Missing or linked original license directory")
    for path in originals_root.rglob("*"):
        if linked(path):
            raise ValueError("Linked original license evidence")
        if path.is_file():
            regular(args.evidence, path.relative_to(args.evidence).as_posix())
    args.output.mkdir(parents=True)
    shutil.copytree(args.evidence / "licenses", args.output / "originals")
    evidence = args.output / "evidence"
    evidence.mkdir()
    for name in ("manifest.review.json", "runtime-components-sha256.txt"):
        shutil.copyfile(regular(args.evidence, name), evidence / name)
    shutil.copyfile(regular(args.source_review, "source-manifest.review.json"), evidence / "source-manifest.review.json")
    if crt_records:
        write_json(evidence / "crt-versions.review.json", crt_records)
    if args.qt_sbom_review:
        shutil.copyfile(args.qt_sbom_review, evidence / "qt-sbom.review.json")
    for name, text in documents.items():
        path = args.output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))
    if distribution:
        # Assembly is not approval. Review evidence remains separate; never
        # promote a supplied draft merely by renaming its manifest.
        write_json(args.output / "manifest.json",
                   {"schema": 1, "components": manifest["components"]})
        print("Distribution notices assembled; distributor audit still required")
    else:
        write_json(args.output / "manifest.review.json", manifest)
        print("Legal draft assembled: 34 files / 19 components; no manifest.json or approval")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("evidence", "source-review", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--env-file", type=Path, default=ROOT / ".env")
    parser.add_argument("--edition", default="windows-x64")
    parser.add_argument("--final-review", action="store_true")
    parser.add_argument("--distribution", action="store_true",
                        help="Assemble distribution notices, not legal approval (requires --final-review)")
    for name in ("stage", "crt-bin", "application-build", "crt-verifier", "qt-sbom-review"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    try:
        generate(args)
    except (ValueError, KeyError, TypeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"License generation failed: {error}\n")


if __name__ == "__main__":
    main()
