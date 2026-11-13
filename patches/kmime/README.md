# KMime 6.31.0: safe byte arguments to C character classification

SmoothMail contributors, 2026-11-13. Upstream code, copyright notices and
LGPL-2.0-or-later licensing remain unchanged. This is a local modification,
not an upstream release. See `THIRD_PARTY_NOTICES.md` and the pinned commit
in `cmake/dependencies.json`.

`isdigit`, `isspace` and `toupper` require EOF or an unsigned-char value.
KMime passed signed bytes from untrusted mail. Bytes 0x80..0xfe can trigger
MSVC Debug CRT assertions; on other runtimes this is undefined behavior.
Convert to unsigned char at each byte-oriented call in header parsing,
uuencode/yEnc detection and the Windows strcasestr fallback. Do not disable
assertions, weaken authentication, change the installed Qt SDK, or rewrite
the user's raw messages.

The build uses an independent patched source copy. Install the upstream
LICENSES, this patch and change notice, plus a commit/patch/DLL/Qt/config
receipt. Configure rejects a missing or mismatched receipt. Corresponding
source and library-replacement obligations remain applicable to distribution.
