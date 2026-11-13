#!/usr/bin/env bash
# Compiles one of the test programs of this directory against the wx build
# tree (not the installed prefix) for x86_64:
#   build/haiku/tests/build-test.sh alphatest    -> <build>/lib/alphatest
# Run it in the VM with build/haiku/vm/run.sh alphatest.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
ARCH=${WX_ARCH:-x86_64}
BUILD=${WX_BUILD_ROOT:-/data1/scratch/bambustudio/wx-build}/$ARCH
SDK=${AIROS_SDK:-/data1/airos/sdk}/$ARCH
. "$SDK/env.sh"
NAME=$1
SETUP=$(ls -d "$BUILD"/lib/wx/include/haikuuniv-unicode-static-*)
LIBS=()
for lib in gl aui html adv core net xml; do
	f=$(ls "$BUILD"/lib/libwx_haikuunivu_$lib-*.a "$BUILD"/lib/libwx_baseu_$lib-*.a 2>/dev/null | head -1 || true)
	[[ -z $f ]] || LIBS+=("$f")
done
LIBS+=("$(ls "$BUILD"/lib/libwx_baseu-*.a)" "$(ls "$BUILD"/lib/libwxregexu-*.a)")
exec "${CROSS}g++" --sysroot="$SYSROOT" -specs="$UNWIND_SPECS" -O2 -fPIC \
	-D_UNICODE -D__WXHAIKU__ -D__WXUNIVERSAL__ -DwxDEBUG_LEVEL=0 \
	-I"$SETUP" -I"$ROOT/include" \
	"$HERE/$NAME.cpp" -o "$BUILD/lib/$NAME" \
	"${LIBS[@]}" "${LIBS[@]}" \
	-lbe -ltranslation -ltracker -lnetwork -lGL -lpng -ljpeg -ltiff -lz -lexpat
