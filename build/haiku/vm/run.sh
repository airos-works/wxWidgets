#!/usr/bin/env bash
# Copies a cross-built program (default: a sample from the x86_64 build tree)
# into the wxHaiku test VM and starts it there; its output goes to
# /Data/wx/<name>.log in the VM and is printed after a few seconds.
#   build/haiku/vm/run.sh <sample name or path> [VAR=value...] [-- args]
# WX_RUN_WAIT sets the seconds to wait before printing the log (default 3).
# WX_RUN_FILES lists data files (relative to the directory of the program)
# to copy next to it.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
BUILD=${WX_BUILD_ROOT:-/data1/scratch/bambustudio/wx-build}/x86_64
BIN=$1; shift
[[ -f $BIN ]] || BIN=$BUILD/lib/$BIN
NAME=$(basename "$BIN")
ENVS=()
while [[ $# -gt 0 && $1 != -- ]]; do ENVS+=("$1"); shift; done
[[ ${1:-} == -- ]] && shift
VM="$HERE/haiku.sh"
"$VM" "kill -9 $NAME 2>/dev/null; mkdir -p /Data/wx; rm -f /Data/wx/$NAME" || true
"$VM" "cat > /Data/wx/$NAME && chmod 755 /Data/wx/$NAME" < "$BIN"
if [[ -n ${WX_RUN_FILES:-} ]]; then
	# shellcheck disable=SC2086
	tar -C "$(dirname "$BIN")" -cf - $WX_RUN_FILES | "$VM" "tar -C /Data/wx -xf -"
fi
"$VM" "cd /Data/wx && (env ${ENVS[*]:-} ./$NAME $* > $NAME.log 2>&1 &); sleep ${WX_RUN_WAIT:-3}; cat $NAME.log; ps | grep -c \"[/]$NAME\" | sed 's/^/running instances: /'"
