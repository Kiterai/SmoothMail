# KCodecs 6.31.0: safe RFC 2047 byte classification

SmoothMail contributors, 2026-11-13. Local modification, not an upstream
release. Preserve original copyright and LGPL-2.0-or-later notices.

Pinned upstream commit: `220095a518c36c1b7aa6b5f49996177617979031` at
https://invent.kde.org/frameworks/kcodecs .

`decodeRFC2047String` passed signed input bytes to `isspace`, whose argument
must be EOF or an unsigned-char value. Cast the byte to unsigned char.
This fixes Debug CRT assertions and undefined behavior without changing
whitespace rules, rewriting raw mail, or disabling assertions.

Build from an independent patched copy; install upstream LICENSES, patch,
change notice and the commit/patch/DLL/Qt/config receipt. Original licenses
and corresponding-source/library-replacement obligations still apply.
