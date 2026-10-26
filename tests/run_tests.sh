#!/bin/sh
# ci gate, phase 0 exit criterion: everything green or the commit does not go
set -e
cd "$(dirname "$0")/.."

echo "== build strict =="
make clean >/dev/null
make 2>&1 | tee /tmp/build.log
if grep -qE "warning|error" /tmp/build.log; then
    echo "FAIL: build has warnings" ; exit 1
fi
echo "PASS: clean build"

echo "== csv contract freeze =="
CONTRACT="t_s,vel_mm_s,curv_inv_mm,k_head,k_tail,ava,avb,aib,aiy,serotonin,head_x_mm,head_y_mm"
./elegans --headless -d 1 --seed 1 2>/dev/null | head -1 > /tmp/hdr.csv
if [ "$(cat /tmp/hdr.csv)" != "$CONTRACT" ]; then
    echo "FAIL: csv header drifted from the frozen contract" ; exit 1
fi
echo "PASS: csv header frozen"

echo "== determinism =="
./elegans --headless -d 10 --seed 7 --quiet 2>/dev/null >/tmp/det_a.csv
./elegans --headless -d 10 --seed 7 --quiet 2>/dev/null >/tmp/det_b.csv
cmp /tmp/det_a.csv /tmp/det_b.csv
echo "PASS: bitwise identical runs"

echo "== connectome smoke =="
for c in "" "--connectome varshney_full.csv" "--connectome herm_302.csv"; do
    # shellcheck disable=SC2086
    ./elegans --headless $c -d 5 --seed 1 --quiet >/dev/null 2>&1 || { echo "FAIL: $c"; exit 1; }
done
echo "PASS: all three connectomes run"

echo "== nan guard =="
./elegans --headless -d 20 --seed 3 2>/dev/null > /tmp/nan.csv
if grep -qi "nan\|inf" /tmp/nan.csv; then
    echo "FAIL: non finite values in csv" ; exit 1
fi
echo "PASS: finite values"

echo "== ubsan =="
cc -std=c99 -Wall -Wextra -pedantic -O1 -g -fsanitize=undefined -D_DEFAULT_SOURCE \
   -o /tmp/elegans_ubsan src/*.c -lm
/tmp/elegans_ubsan --headless -d 5 --seed 1 --quiet 2>/tmp/ub.log >/dev/null
if grep -qE "runtime error|warning" /tmp/ub.log; then
    echo "FAIL: ubsan flagged undefined behavior" ; cat /tmp/ub.log ; exit 1
fi
echo "PASS: ubsan clean"

echo ""
echo "ALL TESTS GREEN"