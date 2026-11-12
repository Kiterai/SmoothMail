cmake_minimum_required(VERSION 3.30)

# Windows-focused first stage. KIO/I18n/SASL are not bootstrapped here yet.
include("${CMAKE_CURRENT_LIST_DIR}/BuildMimeDependencies.cmake")
smoothmail_build_dependency(kcoreaddons -DKCOREADDONS_USE_QML=OFF -DBUILD_PYTHON_BINDINGS=OFF)
smoothmail_build_dependency(kconfig -DKCONFIG_USE_QML=OFF)
smoothmail_build_dependency(kcrash)
message(STATUS "Base Frameworks prefix: ${prefix}")
message(STATUS "KIMAP/KSMTP still require KI18n, KIO and Cyrus SASL.")
