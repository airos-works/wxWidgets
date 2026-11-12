#!/usr/bin/env bash
# Runs a command in the wxHaiku test VM over SSH (port 2246). See run-vm.sh.
set -euo pipefail
DIR=${WX_VM_DIR:-/data1/scratch/bambustudio/vm-wx}
exec ssh -i "$DIR/id_ed25519" -p 2246 -o IdentitiesOnly=yes -o BatchMode=yes \
	-o UserKnownHostsFile="$DIR/known_hosts" -o StrictHostKeyChecking=accept-new \
	-o ConnectTimeout=10 -o ServerAliveInterval=30 user@127.0.0.1 "$@"
