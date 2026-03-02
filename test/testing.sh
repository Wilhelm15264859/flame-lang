#!/bin/bash

COMPILER="./flame"
TEST_DIR="./test"
LOG_DIR="./test/logs"
PASS=0
FAIL=0
LOGFILE="$LOG_DIR/run_$(date +%Y%m%d_%H%M%S).log"

mkdir -p "$LOG_DIR"

run_test() {
    local name=$1
    local file=$2
    local expected=$3

    echo -n "[$name] "
    echo "=== [$name] ===" >> "$LOGFILE"

    $COMPILER -c "$TEST_DIR/$file" >> "$LOGFILE" 2>&1
    if [ $? -ne 0 ]; then
        echo "FAIL (compile error)"
        echo "RESULT: FAIL (compile error)" >> "$LOGFILE"
        FAIL=$((FAIL+1))
        return
    fi

    output=$("$TEST_DIR/$file" 2>>"$LOGFILE")
    echo "OUTPUT: $output" >> "$LOGFILE"

    if echo "$output" | grep -qF "$expected"; then
        echo "PASS"
        echo "RESULT: PASS" >> "$LOGFILE"
        PASS=$((PASS+1))
    else
        echo "FAIL"
        echo "RESULT: FAIL (expected '$expected')" >> "$LOGFILE"
        echo "  expected: '$expected'"
        echo "  got:      '$output'"
        FAIL=$((FAIL+1))
    fi
}

run_test "basic/add"         "test_basic"    "add(3,4) = 7"
run_test "basic/factorial"   "test_basic"    "factorial(10) = 3628800"
run_test "basic/fibonacci"   "test_basic"    "fibonacci(10) = 55"
run_test "pointers/array"    "test_pointers" "sum = 150"
run_test "pointers/deref"    "test_pointers" "x after *p=99: 99"
run_test "structs/fields"    "test_structs"  "p.x=3 p.y=4"
run_test "structs/func"      "test_structs"  "point_sum = 7"
run_test "classes/counter"   "test_classes"  "counter = 10"
run_test "classes/ownership" "test_classes"  "c2 = 11"
run_test "classes/dtor"      "test_classes"  "Counter destroyed"
run_test "loops/for"         "test_loops"    "for sum 0..4 = 10"
run_test "loops/dowhile"     "test_loops"    "do-while: n = 128"
run_test "loops/autodel"     "test_loops"    "Box(0) destroyed"
run_test "ops/bitwise"       "test_ops"      "& = 15"
run_test "ops/compound"      "test_ops"      "+=5: 15"
run_test "ops/logical"       "test_ops"      "&& = 1"
run_test "exception/check_ok"        "test_spec"  "10/2 = 5"
run_test "exception/check_zero_warn" "test_spec"  "div_check: division by zero"
run_test "exception/replace_log"     "test_spec"  "log_div: 20 / 4"
run_test "exception/replace_result"  "test_spec"  "20/4 = 5"
run_test "exception/multi_log"       "test_spec"  "log_add: 3 + 4"
run_test "exception/multi_result"    "test_spec"  "sum = 7"
run_test "autodel/explicit_val"      "test_spec"  "b1 = 10"
run_test "autodel/explicit_dtor"     "test_spec"  "Box(10) freed"
run_test "autodel/ownership_val"     "test_spec"  "b2 = 20"
run_test "autodel/ownership_dtor"    "test_spec"  "Box(20) freed"

echo ""
echo "Results: $PASS passed, $FAIL failed"
echo "" >> "$LOGFILE"
echo "Results: $PASS passed, $FAIL failed" >> "$LOGFILE"

[ $FAIL -eq 0 ] && exit 0 || exit 1