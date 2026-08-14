#!/usr/bin/env bash
LOG=/tmp/bracelet-snapshot-log.txt
export PYTHONUNBUFFERED=1
function print_log_later() {
    sleep 15
    echo "SNAPSHOT LOG PREFIX"
    cat "$LOG"
    echo "FINISHED SNAPSHOT LOG PREFIX"
}
print_log_later &
/opt/bracelet-llvm/bin/snapshot.py 2>&1 | tee "$LOG"
