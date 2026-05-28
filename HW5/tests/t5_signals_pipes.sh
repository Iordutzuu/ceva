#!/bin/bash
set -u

echo "T5 signals and pipes test started"

ROOT="data/t5_tree"
DB="data/t5_inventory.db"
IPC="data/t5_ipc.mmap"
PIDFILE="data/t5_manager.pid"
LOG="reports/T5_signals_pipes.log"

rm -rf "$ROOT" "$DB" "$IPC" "$PIDFILE" "$LOG"
mkdir -p "$ROOT" reports data

# Arbore suficient de mare ca managerul sa fie inca activ cand trimitem semnale.
for d in $(seq 1 80); do
    mkdir -p "$ROOT/dir_$d"
    for f in $(seq 1 4); do
        printf "file %s %s\n" "$d" "$f" > "$ROOT/dir_$d/file_$f.txt"
    done
done

./tools/fileops.sh build >/dev/null || exit 1

./tools/fileops.sh run -- fileops_manager \
    --root "$ROOT" \
    --workers 3 \
    --ipc "$IPC" \
    --db "$DB" \
    --simulate-work-ms 80 \
    --graceful-timeout 2 \
    --pid-file "$PIDFILE" > "$LOG" 2>&1 &
WRAPPER_PID=$!

for _ in $(seq 1 100); do
    [ -s "$PIDFILE" ] && break
    sleep 0.05
done

if [ ! -s "$PIDFILE" ]; then
    echo "FAIL: pid-file missing" >&2
    cat "$LOG" >&2 || true
    kill "$WRAPPER_PID" 2>/dev/null || true
    exit 1
fi

MANAGER_PID=$(cat "$PIDFILE")
kill -USR1 "$MANAGER_PID"
sleep 0.2
kill -TERM "$MANAGER_PID"
wait "$WRAPPER_PID"

if ! grep -q '^STATUS ' "$LOG"; then
    echo "FAIL: STATUS line not found" >&2
    cat "$LOG" >&2
    exit 1
fi

if ! grep -q '^T5MSG type=' "$LOG"; then
    echo "FAIL: T5MSG messages not found" >&2
    cat "$LOG" >&2
    exit 1
fi

if [ ! -f "$DB" ]; then
    echo "FAIL: DB was not created" >&2
    cat "$LOG" >&2
    exit 1
fi

./tools/fileops.sh run -- fileops_manager --db "$DB" --verify >> "$LOG" 2>&1 || {
    echo "FAIL: --verify failed" >&2
    cat "$LOG" >&2
    exit 1
}

./tools/fileops.sh run -- fileops_manager --db "$DB" --dump > reports/T5_dump.txt 2>> "$LOG" || exit 1

if ! grep -q '^complete=0$' reports/T5_dump.txt; then
    echo "FAIL: expected complete=0" >&2
    cat reports/T5_dump.txt >&2
    cat "$LOG" >&2
    exit 1
fi

if pgrep -f "bin/fileops_worker" >/dev/null 2>&1; then
    echo "FAIL: worker process still alive" >&2
    pgrep -af "bin/fileops_worker" >&2 || true
    exit 1
fi

echo "T5 signals and pipes test passed"
