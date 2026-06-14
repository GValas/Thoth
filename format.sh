#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# format.sh - Format all C++ sources with clang-format (style: .clang-format)
#
# Usage:
#   ./format.sh            # format every .cpp/.hpp under src/ and tests/ in place
#   ./format.sh --check    # report files that are not formatted (CI), no changes
# ---------------------------------------------------------------------------
set -euo pipefail

# Resolve project root (this script's directory) so it works from anywhere.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "error: clang-format not found (try: sudo apt-get install -y clang-format)" >&2
    exit 1
fi

# Collect all C++ sources.
mapfile -t FILES < <(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "no C++ files found under src/ or tests/" >&2
    exit 0
fi

if [[ "${1:-}" == "--check" ]]; then
    echo "==> Checking ${#FILES[@]} files against .clang-format"
    if clang-format --dry-run --Werror "${FILES[@]}"; then
        echo "OK: all files conform"
    else
        echo "FAIL: some files are not formatted (run ./format.sh to fix)" >&2
        exit 1
    fi
else
    echo "==> Formatting ${#FILES[@]} files in place"
    clang-format -i "${FILES[@]}"
    echo "done ($(clang-format --version))"
fi
