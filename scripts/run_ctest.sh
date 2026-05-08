#!/usr/bin/env bash
# 在 build 目录运行 ctest。示例：
#   ./scripts/run_ctest.sh -L fast
#   ./scripts/run_ctest.sh -L performance
#   ./scripts/run_ctest.sh -L unit -V
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/build"
exec ctest --test-dir . "$@"
