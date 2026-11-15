# Corresponding source and library replacement

**Draft for review. English authoritative version. [Japanese translation](SOURCE-AND-REPLACEMENT.ja.md).**

## Corresponding source

The corresponding source for the open-source components is to be supplied separately on the same download page as the matching SmoothMail binary distribution. It includes the applicable source revisions, modifications, and scripts needed to control compilation and installation.

GNU GPL v3, section 6(d), describes the source-access method as:

> offer equivalent access to the Corresponding Source in the same way through the same place at no further charge.

**Download page: to be specified before distribution.** The accompanying source list identifies the archives and SHA256 digests. Source access must be available when the binaries are distributed; this notice is not a written offer for later delivery.

Microsoft runtime binaries are not licensed under the open-source licenses of the other components. Their use and distribution are subject to the applicable Microsoft terms.

## Modification and replacement

GNU LGPL v2.1, section 6, refers to terms that permit:

> modification of the work for the customer's own use and reverse engineering for debugging such modifications.

GNU LGPL v3, section 4(d)(1), describes a suitable shared-library mechanism as one that:

> will operate properly with a modified version of the Library that is interface-compatible with the Linked Version.

SmoothMail uses shared libraries and imposes no additional prohibition on modification, replacement, or reverse engineering for debugging such modifications, as permitted by the applicable licenses. Compatible modified DLLs may replace the distributed libraries. Compilation and installation instructions are included in the corresponding source.

Where required by the applicable license and form of distribution, Installation Information must accompany the corresponding source. For a portable Windows distribution, replacement libraries are placed in the application's directory and applicable plugin subdirectories; no SmoothMail-specific authorization key is required.
