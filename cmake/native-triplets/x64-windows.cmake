# Privacy profile v1. Keep the stock Windows x64 dynamic CRT/library ABI.
# Changing this profile requires a fresh native install root.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_PROVIDED_FORTRAN ON)

# Applies through vcpkg-make's MSVC linker wrapper, unlike the parent CMake flags.
set(VCPKG_LINKER_FLAGS "/PDBALTPATH:smoothmail-native.pdb")
if(PORT STREQUAL "gettext-libintl")
    # Logical target prefixes only. vcpkg-make installs through DESTDIR into
    # CURRENT_PACKAGES_DIR; no files are installed at the machine's root.
    # Keep --enable-relocatable and the stock relative bin/lib/include layout.
    # Do not let MSYS turn these logical constants into the MSYS installation
    # directory, and do not form //share/... (a UNC path on Windows).
    set(ENV{MSYS2_ARG_CONV_EXCL} "-DLOCALEDIR=;-DINSTALLDIR=;/DLOCALEDIR=;/DINSTALLDIR=")
    set(VCPKG_MAKE_CONFIGURE_OPTIONS_RELEASE
        "--prefix=/" "--libdir=/lib" "--datarootdir=/share/gettext-libintl")
    set(VCPKG_MAKE_CONFIGURE_OPTIONS_DEBUG
        "--prefix=/debug" "--libdir=/debug/lib" "--datarootdir=/debug/share/gettext-libintl")
endif()
