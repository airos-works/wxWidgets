# Cross toolchain for air/OS (Haiku) from Linux with the airos-ci SDK:
#   cmake -S . -B <build> -DCMAKE_TOOLCHAIN_FILE=build/haiku/toolchain.cmake \
#         -DHAIKU_ARCH=x86_64|arm64 [-DHAIKU_SDK=/data1/airos/sdk/<arch>]
# build/haiku/build.sh does this for you.
set(CMAKE_SYSTEM_NAME Haiku)
if(NOT HAIKU_ARCH)
    set(HAIKU_ARCH "$ENV{HAIKU_ARCH}")
endif()
if(NOT HAIKU_ARCH)
    set(HAIKU_ARCH x86_64)
endif()
if(NOT HAIKU_SDK)
    set(HAIKU_SDK "$ENV{HAIKU_SDK}")
endif()
if(NOT HAIKU_SDK)
    set(HAIKU_SDK /data1/airos/sdk/${HAIKU_ARCH})
endif()
if(NOT HAIKU_DEPS)
    set(HAIKU_DEPS "$ENV{HAIKU_DEPS}")
endif()
if(NOT HAIKU_DEPS)
    set(HAIKU_DEPS /data1/airos/deps/${HAIKU_ARCH}/boot/system)
endif()
# Install prefixes of things built for the target outside the SDK which the
# project being configured uses (an installed wx, cross-built dependencies):
#   -DHAIKU_PREFIXES="/prefix/one;/prefix/two"
# They become find roots: find_package(), find_library() and find_file() look
# in them and accept paths inside them (CMake's FindwxWidgets checks the
# directories wx-config names, which it cannot outside the find roots).
if(NOT HAIKU_PREFIXES)
    set(HAIKU_PREFIXES "$ENV{HAIKU_PREFIXES}")
endif()
if(HAIKU_ARCH STREQUAL "arm64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
endif()
file(STRINGS "${HAIKU_SDK}/env.sh" _cross REGEX "^CROSS=")
string(REGEX REPLACE "^CROSS=" "" HAIKU_CROSS "${_cross}")
# try_compile projects see the same settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES HAIKU_ARCH HAIKU_SDK HAIKU_DEPS
    HAIKU_PREFIXES)
set(CMAKE_SYSROOT ${HAIKU_SDK}/sysroot)
set(CMAKE_C_COMPILER ${HAIKU_CROSS}gcc)
set(CMAKE_CXX_COMPILER ${HAIKU_CROSS}g++)
# The cross compiler links only libgcc.a, whose private unwinder copy breaks
# C++ exceptions across libstdc++; the SDK's specs link libgcc_s first.
if(EXISTS ${HAIKU_SDK}/shared-unwinder.specs)
    set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${HAIKU_SDK}/shared-unwinder.specs")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "-specs=${HAIKU_SDK}/shared-unwinder.specs")
endif()
# The sysroot's own packages, then the extra dev packages (arm64 has png,
# jpeg, tiff... only there).
set(CMAKE_FIND_ROOT_PATH
    ${CMAKE_SYSROOT}/boot/system ${CMAKE_SYSROOT}/boot/system/develop
    ${HAIKU_DEPS} ${HAIKU_DEPS}/develop
    ${HAIKU_PREFIXES})
set(CMAKE_SYSTEM_INCLUDE_PATH /boot/system/develop/headers /develop/headers /headers)
set(CMAKE_SYSTEM_LIBRARY_PATH /boot/system/develop/lib /develop/lib /lib)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# Haiku executables are position independent and linked like shared objects:
# -fPIE is not enough for them
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")
