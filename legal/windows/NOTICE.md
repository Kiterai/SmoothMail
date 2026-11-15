# Copyright and license notices

**Draft for review. English authoritative version. [Japanese translation](NOTICE.ja.md).**

## SmoothMail

Copyright (c) 2026 SmoothMail contributors

SmoothMail's original code is licensed under the MIT License. The license states:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.

The complete license, including its disclaimer of warranty and limitation of liability, is in `licenses/SmoothMail/LICENSE`.

## Third-party software

Third-party software remains subject to its respective copyright notices and licenses. SmoothMail's MIT License does not relicense that software.

| Component | Version | License notice |
|---|---|---|
| Qt Base / Qt SVG | 6.12.0 | LGPL-3.0-only alternative selected for the Qt modules; bundled third-party code retains its own licenses |
| KCodecs / KConfig / KCoreAddons / KCrash / KI18n / KIO / KMime / KService | 6.31.0 | Applicable LGPL and other file-specific licenses, as identified in the accompanying notices and originals |
| KIMAP / KSMTP | 26.08.3 | Applicable LGPL and other file-specific licenses |
| QtKeychain | 0.15.0 | BSD-3-Clause |
| Cyrus SASL | 2.1.28 | Carnegie Mellon University license in `COPYING` |
| Cyrus SASL XOAUTH2 | Source revision identified in the component notice | MIT |
| GNU libiconv | 1.19 | LGPL-2.1-or-later and applicable file-specific notices |
| GNU gettext libintl | 0.22.5 | LGPL-2.1-or-later and applicable file-specific notices |
| Microsoft Visual C++ runtime | v14 | [Microsoft runtime conditions](MICROSOFT-RUNTIME.md) |

The component notices identify the distributed files and reproduce or reference their full license texts and copyright notices. License texts for alternative licenses or other upstream files do not, merely by their inclusion, apply to the entire application.

## Qt and bundled third-party code

Qt is used under the GNU Lesser General Public License, version 3. The LGPL v3 and the GNU GPL v3 to which it refers are included in `licenses/qtbase/LICENSES/` and `licenses/qtsvg/LICENSES/`.

The LGPL v3, section 4, permits a Combined Work under terms that, taken together:

> effectively do not restrict modification of the portions of the Library contained in the Combined Work and reverse engineering for debugging such modifications

SmoothMail imposes no additional restriction on those activities. Corresponding source and replacement information are described in [SOURCE-AND-REPLACEMENT.md](SOURCE-AND-REPLACEMENT.md).

Copyright notices and license texts for Qt's bundled third-party code are included in `licenses/qtbase/` and `licenses/qtsvg/`. For FreeType and its applicable rasterizer code, the FreeType License (FTL) alternative is selected. The required acknowledgment is:

> Portions of this software are copyright © 2026 The FreeType Project (www.freetype.org). All rights reserved.

## Cyrus SASL

Copyright (c) 1998-2003 Carnegie Mellon University. All rights reserved.

> This product includes software developed by Computing Services at Carnegie Mellon University (http://www.cmu.edu/computing/).

The complete conditions and disclaimer are included in `licenses/cyrus-sasl/COPYING`.

## Modified components

The notices of modifications are in [CHANGES.md](CHANGES.md). Original copyright notices and license terms are retained.
