#!/usr/bin/env bash
# Cross-builds wxWidgets for air/OS (Haiku) with the native wxHaiku port
# (wxUniversal widgets) as static libraries and installs it.
#
#   build/haiku/build.sh [x86_64|arm64] [ninja target...]
#
# Environment:
#   WX_BUILD_ROOT   build trees         (default /data1/scratch/bambustudio/wx-build)
#   WX_INSTALL_ROOT install prefixes    (default /data1/scratch/bambustudio/wx-install)
#   WX_SAMPLES=1    also build the samples (into <build>/samples)
#   WX_JOBS         parallel jobs       (default 16)
#   AIROS_SDK       SDK root            (default /data1/airos/sdk)
#   AIROS_DEPS      extra dev packages  (default /data1/airos/deps)
#
# Without targets everything is built and installed to
# $WX_INSTALL_ROOT/<arch>; wx-config is in its bin directory.
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ARCH=${1:-x86_64}
shift || true
BUILD=${WX_BUILD_ROOT:-/data1/scratch/bambustudio/wx-build}/$ARCH
PREFIX=${WX_INSTALL_ROOT:-/data1/scratch/bambustudio/wx-install}/$ARCH
SDK=${AIROS_SDK:-/data1/airos/sdk}/$ARCH
DEPS=${AIROS_DEPS:-/data1/airos/deps}/$ARCH/boot/system
JOBS=${WX_JOBS:-16}
SAMPLES=OFF
[[ -z ${WX_SAMPLES:-} ]] || SAMPLES=ALL
[[ -f $SDK/env.sh ]] || { echo "no air/OS SDK at $SDK" >&2; exit 1; }

if [[ ! -f $BUILD/build.ninja || $ROOT/build/haiku/build.sh -nt $BUILD/build.ninja ]]; then
	cmake -S "$ROOT" -B "$BUILD" -G Ninja \
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		-DCMAKE_TOOLCHAIN_FILE="$ROOT/build/haiku/toolchain.cmake" \
		-DHAIKU_ARCH="$ARCH" -DHAIKU_SDK="$SDK" -DHAIKU_DEPS="$DEPS" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DwxBUILD_TOOLKIT=haiku \
		-DwxBUILD_SHARED=OFF \
		-DwxBUILD_PRECOMP=OFF \
		-DwxBUILD_PIC=OFF \
		-DwxBUILD_SAMPLES="$SAMPLES" \
		-DwxBUILD_DEBUG_LEVEL=0 \
		-DwxBUILD_OPTIMISE=ON \
		-DwxUSE_UNICODE=ON \
		-DwxUSE_EXCEPTIONS=OFF \
		-DwxUSE_REGEX=builtin \
		-DwxUSE_ZLIB=sys -DwxUSE_EXPAT=sys \
		-DwxUSE_LIBPNG=sys -DwxUSE_LIBJPEG=sys -DwxUSE_LIBTIFF=sys \
		-DwxUSE_STC=OFF \
		-DwxUSE_PRIVATE_FONTS=ON \
		-DwxUSE_GRAPHICS_CONTEXT=ON \
		${WX_CMAKE_ARGS:-}
fi

if [[ $# -gt 0 ]]; then
	exec ninja -C "$BUILD" -j"$JOBS" "$@"
fi
# the libraries and wxrc, not the samples the tree may be configured with
mapfile -t LIBTARGETS < <(ninja -C "$BUILD" -t targets all |
	sed -n 's/^\(wx[a-z]*\): phony$/\1/p')
ninja -C "$BUILD" -j"$JOBS" "${LIBTARGETS[@]}"
# Others build against the prefix while we work: it is replaced in one step
# and only when the whole build passed.
rm -rf "$PREFIX.new" "$PREFIX.old"
cmake --install "$BUILD" --prefix "$PREFIX.new" > "$BUILD/install.log"
[[ ! -e $PREFIX ]] || mv "$PREFIX" "$PREFIX.old"
mv "$PREFIX.new" "$PREFIX"
rm -rf "$PREFIX.old"
echo "installed to $PREFIX ($("$PREFIX/bin/wx-config" --version 2>/dev/null || echo 'no wx-config'))"
