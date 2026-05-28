#!/bin/bash

# ruleaza init
./tools/fileops.sh init

# lista de directoare care trebuie sa existe
DIRS="bin src include data logs reports tmp tests doc tools"

for d in $DIRS; do
    if [ ! -d "$d" ]; then
        echo "Missing directory: $d"
        exit 1
    fi
done

echo "All directories exist."
exit 0