#!/usr/bin/env bash
# Compiles one of the test programs of this directory against the wx build
# tree (not the installed prefix) for x86_64:
#   build/haiku/tests/build-test.sh alphatest    -> <build>/lib/alphatest
#   build/haiku/tests/build-test.sh NAME path/a.cpp [path/b.cpp...]
#                                                 -> <build>/lib/NAME
# The second form links other sources (a wx sample) the same way: unlike the
# samples built by CMake the result is not stripped, so that crash reports
# have function names. Run it in the VM with build/haiku/vm/run.sh NAME.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
ARCH=${WX_ARCH:-x86_64}
BUILD=${WX_BUILD_ROOT:-/data1/scratch/bambustudio/wx-build}/$ARCH
SDK=${AIROS_SDK:-/data1/airos/sdk}/$ARCH
. "$SDK/env.sh"
NAME=$1
shift
SOURCES=("$@")
[[ ${#SOURCES[@]} -gt 0 ]] || SOURCES=("$HERE/$NAME.cpp")
# the libraries of the build tree, with the flags its own wx-config gives
CONFIG=$BUILD/wx-config
LIST=
for lib in webview gl aui richtext html propgrid ribbon xrc adv media core net xml base; do
	ls "$BUILD"/lib/libwx_*u_$lib-*.a "$BUILD"/lib/libwx_baseu-*.a > /dev/null 2>&1 || continue
	[[ $lib == base ]] || ls "$BUILD"/lib/libwx_*u_$lib-*.a > /dev/null 2>&1 || continue
	LIST+=${LIST:+,}$lib
done
# shellcheck disable=SC2046
exec "${CROSS}g++" --sysroot="$SYSROOT" -specs="$UNWIND_SPECS" -O2 -fPIC \
	$("$CONFIG" --cxxflags) \
	-I"$ROOT/samples" "${SOURCES[@]}" -o "$BUILD/lib/$NAME" \
	$("$CONFIG" --libs "$LIST")
