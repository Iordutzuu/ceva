#!/bin/bash

echo "T3 concurrency test started"

mkdir -p data reports logs tmp

# TEST 1: proc_snapshot
rm -f data/proc_concurrency.db
rm -f reports/T3_concurrency.txt

./tools/fileops.sh run -- proc_snapshot --db data/proc_concurrency.db &
PID1=$!
./tools/fileops.sh run -- proc_snapshot --db data/proc_concurrency.db &
PID2=$!
./tools/fileops.sh run -- proc_snapshot --db data/proc_concurrency.db &
PID3=$!

wait $PID1; R1=$?
wait $PID2; R2=$?
wait $PID3; R3=$?

{
    echo "T3 concurrency test - proc_snapshot"
    echo "proc_snapshot instance 1 exit code: $R1"
    echo "proc_snapshot instance 2 exit code: $R2"
    echo "proc_snapshot instance 3 exit code: $R3"
} > reports/T3_concurrency.txt

if [ "$R1" -gt 1 ] || [ "$R2" -gt 1 ] || [ "$R3" -gt 1 ]; then
    echo "FAIL: at least one proc_snapshot instance crashed abnormally" >> reports/T3_concurrency.txt
else
    if [ ! -s data/proc_concurrency.db ]; then
        echo "FAIL: data/proc_concurrency.db is empty or missing" >> reports/T3_concurrency.txt
    else
        echo "PASS: proc_snapshot concurrency test passed" >> reports/T3_concurrency.txt
    fi
fi

# TEST 2: fileops_indexer
rm -f data/index_concurrency.db

./tools/fileops.sh run -- fileops_indexer --root src --db data/index_concurrency.db &
PID4=$!
./tools/fileops.sh run -- fileops_indexer --root src --db data/index_concurrency.db &
PID5=$!
./tools/fileops.sh run -- fileops_indexer --root src --db data/index_concurrency.db &
PID6=$!

wait $PID4; R4=$?
wait $PID5; R5=$?
wait $PID6; R6=$?

{
    echo ""
    echo "T3 concurrency test - fileops_indexer"
    echo "fileops_indexer instance 1 exit code: $R4"
    echo "fileops_indexer instance 2 exit code: $R5"
    echo "fileops_indexer instance 3 exit code: $R6"
} >> reports/T3_concurrency.txt

if [ "$R4" -gt 1 ] || [ "$R5" -gt 1 ] || [ "$R6" -gt 1 ]; then
    echo "FAIL: at least one fileops_indexer instance crashed abnormally" >> reports/T3_concurrency.txt
else
    if [ ! -s data/index_concurrency.db ]; then
        echo "FAIL: data/index_concurrency.db is empty or missing" >> reports/T3_concurrency.txt
    else
        echo "PASS: fileops_indexer concurrency test passed" >> reports/T3_concurrency.txt
    fi
fi

echo "T3 concurrency tests finished. Check reports/T3_concurrency.txt"

# TEST 3: Generare rapoarte db_diff
echo "Generare rapoarte db_diff..."

rm -f data/index_old.db data/index_new.db data/proc_old.db data/proc_new.db

./tools/fileops.sh run -- fileops_indexer --root src --db data/index_old.db
touch src/dummy_test_file.txt
./tools/fileops.sh run -- fileops_indexer --root src --db data/index_new.db
./tools/fileops.sh run -- db_diff --old data/index_old.db --new data/index_new.db --out reports/T3_filediff.txt
rm -f src/dummy_test_file.txt

./tools/fileops.sh run -- proc_snapshot --db data/proc_old.db
sleep 1
./tools/fileops.sh run -- proc_snapshot --db data/proc_new.db
./tools/fileops.sh run -- db_diff --old data/proc_old.db --new data/proc_new.db --out reports/T3_procdiff.txt

echo "Rapoartele db_diff au fost generate in directorul reports/."

exit 0
