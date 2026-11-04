#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Known-answer tests: mayhem/harness/kat_probe.cpp (built by build.sh against the instrumented
# in-tree engine) runs fixed SQL through the embedded engine and prints "<id> <result>" lines;
# each line is compared with the value Firebird must produce.
PROBE="$SRC/kat_probe"
[ -x "$PROBE" ] || { echo "missing $PROBE — mayhem/build.sh did not build it" >&2; emit_ctrf "firebird-kat" 0 1 0; exit 1; }

declare -A EXPECT=(
  [arith]="42"
  [string]="FIREBIRD-bcd"
  [date]="2020-02-29"
  [numeric]="3.33"
  [recursive]="55"
  [agg]="3|6|10.25"
  [trigger]="ONE;TWO;THREE"
  [join]="3|6;2|4"
  [unique]="ERR 335544665"      # isc_unique_key_violation
  [syntax]="ERR 335544569"      # isc_dsql_error
)

export ASAN_OPTIONS="${ASAN_OPTIONS:-}:detect_leaks=0"
OUT="$("$PROBE" 2>/tmp/kat.err)"; rc=$?
echo "$OUT"
[ "$rc" -eq 0 ] || { echo "kat_probe exited $rc"; head -20 /tmp/kat.err; }

passed=0; failed=0
for id in "${!EXPECT[@]}"; do
  got="$(printf '%s\n' "$OUT" | sed -n "s/^$id //p")"
  if [ "$got" = "${EXPECT[$id]}" ]; then passed=$((passed + 1)); else failed=$((failed + 1)); echo "FAIL $id: expected '${EXPECT[$id]}' got '$got'"; fi
done
[ "$rc" -eq 0 ] || failed=$((failed + 1))

emit_ctrf "firebird-kat" "$passed" "$failed" 0
