# @SOFTWARE_NAME@ — notices of modifications

**@NOTICE_STATUS_EN@English authoritative version. [Japanese translation](CHANGES.ja.md).**

The following libraries have been modified by **SmoothMail contributors**. The original copyright notices and license terms are retained. The corresponding source includes the modifications.

| Library | Modification date | Modification |
|---|---|---|
| KCodecs 6.31.0 | 2026-11-13 | Corrected character classification in RFC 2047 decoding by converting input bytes to unsigned char |
| KMime 6.31.0 | 2026-11-13 | Corrected byte arguments to C character-classification and conversion functions |
| KIMAP 26.08.3 | 2026-11-13 | Corrected empty SASL challenge handling and authentication-error reporting and cleanup |
| KSMTP 26.08.3 | 2026-11-13 | Cleared previously advertised server capabilities at the start of EHLO |
| KConfig 6.31.0 | 2026-11-14 | Disabled external configuration migration through kconf_update on Windows |
| KCrash 6.31.0 | 2026-11-14 | Removed the original build-prefix fallback from Windows DrKonqi lookup |
| KIO 6.31.0 | 2026-11-12; 2026-11-14 | Excluded favicon-cache implementation; favicon lookup returns an empty string. Removed original build-prefix helper lookup on Windows and disabled the Windows admin worker |

GNU libiconv 1.19 and GNU gettext libintl 0.22.5 incorporate the patches of the supplied vcpkg recipes and the supplied Windows build configuration. Those patches and configuration are included in the corresponding source.

These are modified distributions, not unmodified upstream releases. This notice does not alter the licenses of the affected files.
