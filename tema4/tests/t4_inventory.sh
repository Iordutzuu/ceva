#!/bin/bash

echo "T4 inventory test started"

mkdir -p data reports logs tmp

rm -rf tmp/t4_inventory_case
rm -f data/t4_ipc.mmap
rm -f data/t4_inventory.db
rm -f reports/T4_inventory.txt
rm -f reports/T4_dump.txt

mkdir -p tmp/t4_inventory_case/sub1
mkdir -p tmp/t4_inventory_case/sub2/nested

echo "alpha" > tmp/t4_inventory_case/a.txt
echo "beta" > tmp/t4_inventory_case/sub1/b.txt
echo "gamma" > tmp/t4_inventory_case/sub2/nested/c.txt

ln -s a.txt tmp/t4_inventory_case/link_to_a 2>/dev/null

./tools/fileops.sh build
if [ $? -ne 0 ]; then
    echo "FAIL: build failed" > reports/T4_inventory.txt
    exit 1
fi

if [ ! -x bin/fileops_manager ] || [ ! -x bin/fileops_worker ]; then
    echo "FAIL: required executables were not created" > reports/T4_inventory.txt
    exit 1
fi

./tools/fileops.sh run -- fileops_manager \
    --root tmp/t4_inventory_case \
    --workers 2 \
    --ipc data/t4_ipc.mmap \
    --db data/t4_inventory.db \
    --max-depth 10 \
    --simulate-work-ms 1

if [ $? -ne 0 ]; then
    echo "FAIL: inventory run failed" > reports/T4_inventory.txt
    exit 1
fi

if [ ! -f data/t4_inventory.db ]; then
    echo "FAIL: inventory DB was not created" > reports/T4_inventory.txt
    exit 1
fi

if [ ! -s data/t4_inventory.db ]; then
    echo "FAIL: inventory DB is empty" > reports/T4_inventory.txt
    exit 1
fi

./tools/fileops.sh run -- fileops_manager --db data/t4_inventory.db --verify
if [ $? -ne 0 ]; then
    echo "FAIL: verify failed" > reports/T4_inventory.txt
    exit 1
fi

./tools/fileops.sh run -- fileops_manager --db data/t4_inventory.db --dump > reports/T4_dump.txt
if [ $? -ne 0 ]; then
    echo "FAIL: dump failed" > reports/T4_inventory.txt
    exit 1
fi

grep "magic=INV4" reports/T4_dump.txt >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: dump missing magic" > reports/T4_inventory.txt
    exit 1
fi

grep "version=1" reports/T4_dump.txt >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: dump missing version" > reports/T4_inventory.txt
    exit 1
fi

grep "complete=1" reports/T4_dump.txt >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: dump missing complete flag" > reports/T4_inventory.txt
    exit 1
fi

grep "file_record_count=3" reports/T4_dump.txt >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: expected file_record_count=3" > reports/T4_inventory.txt
    cat reports/T4_dump.txt >> reports/T4_inventory.txt
    exit 1
fi

grep "worker_count=2" reports/T4_dump.txt >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: expected worker_count=2" > reports/T4_inventory.txt
    exit 1
fi

{
    echo "T4 inventory test"
    echo "PASS"
    echo "DB: data/t4_inventory.db"
    echo "Dump:"
    cat reports/T4_dump.txt
} > reports/T4_inventory.txt

echo "T4 inventory test passed"

exit 0