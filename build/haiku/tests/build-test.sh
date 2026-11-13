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
SETUP=$(ls -d "$BUILD"/lib/wx/include/haikuuniv-unicode-static-*)
LIBS=()
for lib in gl aui richtext html propgrid ribbon xrc adv core net xml; do
	f=$(ls "$BUILD"/lib/libwx_haikuunivu_$lib-*.a "$BUILD"/lib/libwx_baseu_$lib-*.a 2>/dev/null | head -1 || true)
	[[ -z $f ]] || LIBS+=("$f")
done
LIBS+=("$(ls "$BUILD"/lib/libwx_baseu-*.a)" "$(ls "$BUILD"/lib/libwxregexu-*.a)")
exec "${CROSS}g++" --sysroot="$SYSROOT" -specs="$UNWIND_SPECS" -O2 -fPIC \
	-D_UNICODE -D__WXHAIKU__ -D__WXUNIVERSAL__ -DwxDEBUG_LEVEL=0 \
	-I"$SETUP" -I"$ROOT/include" \
	-I"$ROOT/samples" "${SOURCES[@]}" -o "$BUILD/lib/$NAME" \
	"${LIBS[@]}" "${LIBS[@]}" \
	-lbe -ltranslation -ltracker -lnetwork -lGL -lpng -ljpeg -ltiff -lz -lexpat
