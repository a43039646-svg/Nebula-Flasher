#!/usr/bin/env bash
set -euo pipefail
writer=${1:-./nebula-flasher-writer}

expect_reject() {
  local description="$1"; shift
  local output rc
  set +e
  output=$("$writer" "$@" 2>&1)
  rc=$?
  set -e
  if [[ $rc -eq 0 ]]; then
    echo "FAIL: $description (unexpected success)" >&2
    exit 1
  fi
  if [[ "$output" != *"ERROR:"* ]]; then
    echo "FAIL: $description (no useful validation error): $output" >&2
    exit 1
  fi
  echo "PASS: $description"
}

expect_reject "rejects non-removable target devices" /etc/hosts /dev/null 1:3
expect_reject "rejects incorrect argument count" /etc/hosts /dev/null

echo "Writer validation tests passed. No disk was written."
