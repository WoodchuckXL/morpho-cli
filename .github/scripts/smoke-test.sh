#!/usr/bin/env bash
# Smoke tests for morpho6 CLI (install / PR gate).
set -euo pipefail

MORPHO6="${MORPHO6:-morpho6}"

expect_ok() {
  local desc="$1"
  shift
  echo "==> $desc"
  "$@"
}

expect_fail() {
  local desc="$1"
  shift
  echo "==> $desc (expect failure)"
  if "$@"; then
    echo "expected failure, but succeeded: $*" >&2
    exit 1
  fi
}

expect_contains() {
  local desc="$1"
  local needle="$2"
  shift 2
  echo "==> $desc"
  local out
  out="$("$@" 2>&1)" || true
  if ! grep -Fq -- "$needle" <<<"$out"; then
    echo "expected output to contain: $needle" >&2
    echo "got:" >&2
    echo "$out" >&2
    exit 1
  fi
}

# Basic eval / version / help
expect_ok "version" "$MORPHO6" --version
expect_ok "eval" "$MORPHO6" -e 'print(1)'
expect_ok "multiple -e preamble" "$MORPHO6" -e 'var x = 1' -e 'print(x)'

# Help queries
expect_contains "CLI usage" "Usage: morpho6" "$MORPHO6" --help
expect_contains "help index topic" "Topics:" "$MORPHO6" --no-color --help help
expect_contains "help Matrix" "Matrix" "$MORPHO6" --no-color --help Matrix
expect_fail "unknown help topic" "$MORPHO6" --no-color --help NotARealTopicXYZ123

# Option parsing
expect_ok "workers" "$MORPHO6" --workers 2 -e 'print(1)'
expect_ok "disassemble only" "$MORPHO6" -D -e 'print(1)'
expect_contains "disassemble with source" ">>>    1 :" "$MORPHO6" --no-color -dl -e 'print(1)'

# -D must not execute (disassembly may still mention constants)
out="$("$MORPHO6" -D -e 'print("EXECUTED")' 2>&1)" || true
if grep -Eq '^EXECUTED$' <<<"$out"; then
  echo "-D should not execute the program" >&2
  echo "$out" >&2
  exit 1
fi

# Failure exit codes
expect_fail "missing list file" "$MORPHO6" -l /no/such/file.morpho
expect_fail "bad eval" "$MORPHO6" -e 'var x ='
expect_fail "unknown option" "$MORPHO6" --not-a-real-option

# File + preamble
tmp="$(mktemp /tmp/morpho-smoke.XXXXXX.morpho)"
trap 'rm -f "$tmp"' EXIT
printf 'print(x)\n' >"$tmp"
expect_ok "eval preamble + file" "$MORPHO6" -e 'var x = 7' "$tmp"

# Piped stdin + preamble
expect_ok "eval preamble + stdin" bash -c "printf 'print(x)\n' | $MORPHO6 -e 'var x = 3'"

# NO_COLOR / --no-color should not emit CSI sequences for help
out="$(NO_COLOR=1 "$MORPHO6" --help Matrix 2>&1)" || true
if grep -Eq $'\x1B\[' <<<"$out"; then
  echo "NO_COLOR=1 still produced ANSI escapes" >&2
  exit 1
fi

echo "All smoke tests passed."
