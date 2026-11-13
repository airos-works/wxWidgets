#!/usr/bin/env bash
# Starts each of the given samples in the test VM, takes a screenshot after a
# few seconds and kills it again; reports the ones which died or left a crash
# report. Run it inside session.sh:
#   build/haiku/vm/session.sh ./batch.sh OUTDIR sample...
# Screenshots go to OUTDIR/batch-<sample>.png.
set -uo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
OUT=$1; shift
mkdir -p "$OUT"
"$HERE/haiku.sh" 'rm -f ~/Desktop/*.report' || true
for s in "$@"; do
	WX_RUN_WAIT=${WX_BATCH_WAIT:-3} "$HERE/run.sh" "$s" > "$OUT/batch-$s.log" 2>&1
	"$HERE/vm.py" screenshot "$OUT/batch-$s.png" > /dev/null
	alive=$("$HERE/haiku.sh" "ps | grep -c \"^\\./$s \"" 2>/dev/null || echo 0)
	"$HERE/haiku.sh" "kill -9 $s 2>/dev/null; true"
	echo "$s: alive=$alive"
done
"$HERE/haiku.sh" 'ls ~/Desktop | grep report' || true
