#!/usr/bin/env bash
#
# mayhem/test.sh — run the known-answer test (build/kat, built by mayhem/build.sh) and report CTRF.
# kat.cpp checks fixed inputs against values CLK is known to produce (ZX Spectrum and Commodore tape
# pulse timings, Atari 2600 paging detection, ROM segment contents, SSD/D64 geometry and track flux).
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
cd "$SRC"

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
    "$tool" "$tests" "$passed" "$failed" "$skipped" "$pending" "$other"
  [ "$failed" -eq 0 ]
}

KAT="$SRC/build/kat"
if [ ! -x "$KAT" ]; then
  echo "missing $KAT — mayhem/build.sh did not produce it" >&2
  emit_ctrf "clk-kat" 0 1 0
  exit 1
fi

out=$("$KAT" 2>&1)
echo "$out"
summary=$(printf '%s\n' "$out" | grep -E '^KAT_SUMMARY: [0-9]+ run; [0-9]+ failed$' | tail -n 1)
if [ -z "$summary" ]; then
  echo "no KAT_SUMMARY line from $KAT" >&2
  emit_ctrf "clk-kat" 0 1 0
  exit 1
fi
run=$(printf '%s\n' "$summary" | sed -E 's/^KAT_SUMMARY: ([0-9]+) run; ([0-9]+) failed$/\1/')
bad=$(printf '%s\n' "$summary" | sed -E 's/^KAT_SUMMARY: ([0-9]+) run; ([0-9]+) failed$/\2/')
# The program lists every assertion, so a summary that disagrees with the PASS/FAIL lines is a lie.
pass_lines=$(printf '%s\n' "$out" | grep -c '^PASS ')
fail_lines=$(printf '%s\n' "$out" | grep -c '^FAIL ')
if [ "$run" -lt 30 ] || [ "$pass_lines" -ne $((run - bad)) ] || [ "$fail_lines" -ne "$bad" ]; then
  echo "KAT summary ($run run, $bad failed) disagrees with its PASS/FAIL lines ($pass_lines/$fail_lines), or too few checks" >&2
  emit_ctrf "clk-kat" 0 1 0
  exit 1
fi
emit_ctrf "clk-kat" "$((run - bad))" "$bad" 0
