"""Optional vcpkg asset downloader for environments requiring an external curl.

Usage: python fetch-vcpkg-asset.py CURL URL SHA512 DESTINATION
Does not disable TLS verification or change proxy settings.
"""

import hashlib
from pathlib import Path
import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    curl, url, expected, destination = sys.argv[1:]
    if not re.fullmatch(r"[0-9a-fA-F]{128}", expected):
        print("A SHA512 digest is required.", file=sys.stderr)
        return 2
    # Use GNU's canonical server rather than its redirecting mirror selector.
    mirror = "https://ftpmirror.gnu.org/gnu/"
    if url.startswith(mirror):
        url = "https://ftp.gnu.org/gnu/" + url[len(mirror):]
    msys_mirror = "https://mirror.msys2.org/"
    if url.startswith(msys_mirror):
        url = "https://repo.msys2.org/" + url[len(msys_mirror):]
    result = subprocess.run(
        [curl, "--fail", "--location", "--proto", "=https", "--proto-redir",
         "=https", "--output", destination, url],
        check=False,
    )
    if result.returncode:
        return result.returncode
    with Path(destination).open("rb") as asset:
        actual = hashlib.file_digest(asset, "sha512").hexdigest()
    if actual.lower() != expected.lower():
        print("Downloaded asset SHA512 mismatch.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
