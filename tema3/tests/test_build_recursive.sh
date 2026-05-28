#!/bin/bash

# curata orice build anterior
./tools/fileops.sh clean

# creeaza structura temporara pentru test
mkdir -p tmp/scenariu_test_src/app
mkdir -p tmp/scenariu_test_src/lib/include

# creeaza fisier header
cat > tmp/scenariu_test_src/lib/include/util.h <<EOF
int util_add(int a, int b);
EOF

# creeaza implementare
cat > tmp/scenariu_test_src/lib/util.c <<EOF
#include "util.h"

int util_add(int a, int b) {
    return a + b;
}
EOF

# creeaza main
cat > tmp/scenariu_test_src/app/main_demo.c <<EOF
#include <stdio.h>
#include "util.h"

int main() {
    int result = util_add(2, 3);
    printf("%d\n", result);
    return 0;
}
EOF

# seteaza include path + flags
export CFLAGS="-I tmp/scenariu_test_src/lib/include -std=c11 -Wall -Wextra"

# ruleaza build pe directorul temporar
./tools/fileops.sh build tmp/scenariu_test_src

# verifica daca executabilul exista
if [ ! -x "bin/demo" ]; then
    echo "Executable not created."
    exit 1
fi

# ruleaza executabilul
OUTPUT=$(./bin/demo)

# verifica output
if [ "$OUTPUT" != "5" ]; then
    echo "Wrong output: $OUTPUT"
    exit 1
fi

echo "Build recursive test passed."
exit 0