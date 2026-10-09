#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
ROM="${1:-inputs/ps4built.bin}"
if [[ ! -f "$ROM" ]]; then
  echo "P142 resume: missing PSIV ROM: $ROM" >&2
  echo "Pass the verified ps4built.bin path as argument 1." >&2
  exit 2
fi
exec ./build_p142_pscdeepfast.sh "$ROM"
