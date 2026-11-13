#!/usr/bin/env bash
# Compiles one of the test programs of this directory against the INSTALLED
# wx with the flags its wx-config gives, which this also tests:
#   build/haiku/tests/build-config-test.sh basetest [base]
#   build/haiku/tests/build-config-test.sh misctest std,media,webview
# The second argument is the wx-config library list (default: std). The
# program is written to <build>/lib/<name>; run it in the VM with
# build/haiku/vm/run.sh <name>.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${WX_ARCH:-x86_64}
BUILD=${WX_BUILD_ROOT:-/data1/scratch/bambustudio/wx-build}/$ARCH
PREFIX=${WX_INSTALL_ROOT:-/data1/scratch/bambustudio/wx-install}/$ARCH
SDK=${AIROS_SDK:-/data1/airos/sdk}/$ARCH
. "$SDK/env.sh"
NAME=$1
LIBS=${2:-std}
CONFIG=$PREFIX/bin/wx-config
# shellcheck disable=SC2046
exec "$("$CONFIG" --cxx)" --sysroot="$SYSROOT" -specs="$UNWIND_SPECS" -O2 -fPIC \
	$("$CONFIG" --cxxflags "$LIBS") \
	"$HERE/$NAME.cpp" -o "$BUILD/lib/$NAME" \
	$("$CONFIG" --libs "$LIBS")
