#!/bin/bash

SUBCOMMAND="$1"
START_EPOCH=$(date +%s)
START_HUMAN=$(date "+%Y-%m-%d %H:%M:%S")

mkdir -p logs
LOG_FILE="logs/fileops_$(date +%Y%m%d_%H%M%S).log"

finish_log() {
    EXIT_CODE=$?
    END_EPOCH=$(date +%s)
    END_HUMAN=$(date "+%Y-%m-%d %H:%M:%S")
    DURATION=$((END_EPOCH - START_EPOCH))

    {
        echo "subcommand: $SUBCOMMAND"
        echo "start: $START_HUMAN"
        echo "end: $END_HUMAN"
        echo "duration_seconds: $DURATION"
        echo "exit_code: $EXIT_CODE"
    } >> "$LOG_FILE"
}

trap finish_log EXIT

{
    echo "subcommand: $SUBCOMMAND"
    echo "start: $START_HUMAN"
} > "$LOG_FILE"

print_usage() {
    echo "Usage:"
    echo "  ./tools/fileops.sh init"
    echo "  ./tools/fileops.sh build [src_dir]"
    echo "  ./tools/fileops.sh run -- <executable> [args...]"
    echo "  ./tools/fileops.sh clean"
    echo "  ./tools/fileops.sh test"
}

init_project() {
    mkdir -p bin src include data logs reports tmp tests doc tools

    if ! command -v gcc >/dev/null 2>&1; then
        echo "Error: gcc is not installed or not found in PATH."
        return 1
    fi

    echo "Project structure created successfully."
    return 0
}

build_project() {
    SRC_DIR="src"

    if [ -n "$2" ]; then
        SRC_DIR="$2"
    fi

    if [ ! -d "$SRC_DIR" ]; then
        echo "Error: source directory '$SRC_DIR' does not exist."
        return 1
    fi

    if ! command -v gcc >/dev/null 2>&1; then
        echo "Error: gcc is not installed or not found in PATH."
        return 1
    fi

    mkdir -p tmp/obj
    mkdir -p bin

    INCLUDE_FLAGS=""
    if [ -d "include" ]; then
        INCLUDE_FLAGS="$INCLUDE_FLAGS -Iinclude"
    fi

    for inc_dir in $(find "$SRC_DIR" -type d -name include); do
        INCLUDE_FLAGS="$INCLUDE_FLAGS -I$inc_dir"
    done

    FOUND_C=0

    for SRC_FILE in $(find "$SRC_DIR" -type f -name "*.c"); do
        FOUND_C=1

        OBJ_FILE="tmp/obj/${SRC_FILE%.c}.o"
        OBJ_DIR=$(dirname "$OBJ_FILE")
        mkdir -p "$OBJ_DIR"

        if [ ! -f "$OBJ_FILE" ] || [ "$SRC_FILE" -nt "$OBJ_FILE" ]; then
            echo "Compiling $SRC_FILE -> $OBJ_FILE"
            gcc -c "$SRC_FILE" -o "$OBJ_FILE" $INCLUDE_FLAGS ${CFLAGS:- -std=c11 -Wall -Wextra}
            if [ $? -ne 0 ]; then
                echo "Error: compilation failed for $SRC_FILE"
                return 1
            fi
        else
            echo "Skipping $SRC_FILE (object file is up to date)"
        fi
    done

    if [ "$FOUND_C" -eq 0 ]; then
        echo "No .c files found in '$SRC_DIR'."
        return 0
    fi

    FOUND_MAIN=0

    for MAIN_FILE in $(find "$SRC_DIR" -type f -name "main_*.c"); do
        FOUND_MAIN=1

        MAIN_BASENAME=$(basename "$MAIN_FILE")
        EXEC_NAME="${MAIN_BASENAME#main_}"
        EXEC_NAME="${EXEC_NAME%.c}"
        EXEC_PATH="bin/$EXEC_NAME"

        MAIN_OBJ="tmp/obj/${MAIN_FILE%.c}.o"

        LINK_OBJECTS=""
        for OBJ in $(find tmp/obj -type f -name "*.o"); do
            OBJ_SRC="${OBJ#tmp/obj/}"
            OBJ_SRC="${OBJ_SRC%.o}.c"
            OBJ_BASE=$(basename "$OBJ_SRC")

            if [ "$OBJ_BASE" != "$MAIN_BASENAME" ]; then
                
                if [[ ! "$OBJ_BASE" == main_* ]]; then
                    LINK_OBJECTS="$LINK_OBJECTS $OBJ"
                fi
            fi
        done

        NEED_LINK=0
        if [ ! -f "$EXEC_PATH" ]; then
            NEED_LINK=1
        elif [ "$MAIN_OBJ" -nt "$EXEC_PATH" ]; then
            NEED_LINK=1
        else
            for OBJ in $LINK_OBJECTS; do
                if [ "$OBJ" -nt "$EXEC_PATH" ]; then
                    NEED_LINK=1
                    break
                fi
            done
        fi

        if [ "$NEED_LINK" -eq 1 ]; then
            echo "Linking $EXEC_PATH"
            gcc "$MAIN_OBJ" $LINK_OBJECTS -o "$EXEC_PATH"
            if [ $? -ne 0 ]; then
                echo "Error: linking failed for $EXEC_PATH"
                return 1
            fi
        else
            echo "Skipping link for $EXEC_PATH (executable is up to date)"
        fi
    done

    if [ "$FOUND_MAIN" -eq 0 ]; then
        echo "No main_*.c files found. Object files compiled successfully, but no executable was created."
    fi

    return 0
}

run_executable() {
    if [ "$2" != "--" ]; then
        echo "Error: use run like this:"
        echo "./tools/fileops.sh run -- <executable> [args...]"
        return 1
    fi

    if [ -z "$3" ]; then
        echo "Error: missing executable name."
        return 1
    fi

    EXEC_NAME="$3"
    shift 3

    EXEC_PATH="bin/$EXEC_NAME"

    if [ ! -x "$EXEC_PATH" ]; then
        echo "Error: executable '$EXEC_PATH' not found or not executable."
        return 1
    fi

    "$EXEC_PATH" "$@"
    return $?
}

clean_build() {
    if [ -d "tmp/obj" ]; then
        rm -rf tmp/obj
    fi

    if [ -d "bin" ]; then
        find bin -type f -delete
    fi

    echo "Build artifacts removed."
    return 0
}

run_tests() {
    mkdir -p reports

    REPORT_FILE="reports/T2_tests.txt"
    : > "$REPORT_FILE"

    FOUND_TEST=0
    HAS_FAIL=0

    for TEST_FILE in $(find tests -type f -name "*.sh"); do
        FOUND_TEST=1

        bash "$TEST_FILE"
        TEST_EXIT=$?

        TEST_NAME=$(basename "$TEST_FILE")

        if [ "$TEST_EXIT" -eq 0 ]; then
            echo "$TEST_NAME PASS" >> "$REPORT_FILE"
        else
            echo "$TEST_NAME FAIL" >> "$REPORT_FILE"
            HAS_FAIL=1
        fi
    done

    if [ "$FOUND_TEST" -eq 0 ]; then
        echo "No test scripts found in tests/." >> "$REPORT_FILE"
        echo "No test scripts found in tests/."
        return 1
    fi

    if [ "$HAS_FAIL" -ne 0 ]; then
        echo "At least one test failed."
        return 1
    fi

    echo "All tests passed."
    return 0
}

if [ $# -lt 1 ]; then
    print_usage
    exit 1
fi

case "$1" in
    init)
        init_project
        ;;
    build)
        build_project "$@"
        ;;
    run)
        run_executable "$@"
        ;;
    clean)
        clean_build
        ;;
    test)
        run_tests
        ;;
    *)
        echo "Error: unknown subcommand '$1'"
        print_usage
        exit 1
        ;;
esac
