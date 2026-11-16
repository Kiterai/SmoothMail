# Cyrus SASL 2.1.28: explicit runtime paths

Modified by SmoothMail contributors on 2026-11-17.

The Windows configuration's absolute `C:\CMU\bin\sasl2` plugin and configuration
defaults are replaced with empty defaults. SmoothMail already calls
`sasl_set_path(SASL_PATH_TYPE_PLUGIN, ...)` with its application-local `sasl2`
directory before initializing client authentication. Client callers of this
modified library must supply a plugin path explicitly.

No authentication mechanisms, public interfaces or original copyright/license
terms are changed. The fixed upstream archive remains unmodified; the patch is
applied to a separately extracted build directory keyed by the patch digest.

This avoids a machine-path exception bound to a previous locally built DLL's
hash. The resulting DLL is subject to the ordinary strict path inspection.
Reassess/remove this patch if upstream supplies relocatable defaults or the
application stops providing the plugin path explicitly.
