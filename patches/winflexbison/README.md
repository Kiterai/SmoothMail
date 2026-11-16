# WinFlexBison build output isolation

The pinned WinFlexBison CMake project writes executables and copied Bison data
under its source `bin/Release` or `bin/Debug` directory, including for an
out-of-source build. This breaks the pristine source-tree digest on a second
archive-mode bootstrap.

`out-of-source-output.patch` changes only those two output directories to use
the binary directory. Upstream install rules already reference those variables.
The patch is applied to a separate working copy through PreparePatchedSource;
the pinned upstream archive and imported pristine tree remain unchanged.

Modified for SmoothMail on 2026-11-16. This is a host build-tool integration
change, not a change to the distributed application libraries.
