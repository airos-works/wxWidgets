#!/usr/bin/env bash
# Runs a command with exclusive use of the wxHaiku test VM: several people
# (agents) share its one screen, mouse and keyboard. Everything that looks at
# the screen or sends input belongs in one session:
#   build/haiku/vm/session.sh bash -c './run.sh minimal; ./vm.py click 96 100 1280 800; ./vm.py screenshot /tmp/x.png; ./haiku.sh "kill -9 minimal"'
# The command runs in this directory. Keep sessions short (they block the
# others) and close the windows you opened before the session ends.
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DIR=${WX_VM_DIR:-/data1/scratch/bambustudio/vm-wx}
cd "$HERE"
exec flock -w "${WX_VM_LOCK_WAIT:-900}" "$DIR/vm.lock" "$@"
