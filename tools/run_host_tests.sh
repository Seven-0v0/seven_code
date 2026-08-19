#!/usr/bin/env bash
#
# Host-side test runner for the firmware logic test harness.
#
# Configures tests/firmware/ as a standalone CMake project (host compiler,
# no embedded toolchain) into build-host-tests/ at the repo root, builds it,
# and runs ctest. build-host-tests/ is already covered by the repo's
# `build-*/` .gitignore rule, so no generated files are ever committed.
#
# Usage:
#   bash tools/run_host_tests.sh
#
set -euo pipefail

# Resolve the real script location even through symlinks, then find the
# project root as the parent of tools/.
SCRIPT_SOURCE="${BASH_SOURCE[0]}"
while [ -h "$SCRIPT_SOURCE" ]; do
    SCRIPT_DIR="$(cd -P "$(dirname "$SCRIPT_SOURCE")" && pwd)"
    SCRIPT_SOURCE="$(readlink "$SCRIPT_SOURCE")"
    case "$SCRIPT_SOURCE" in
        /*) ;;
        *) SCRIPT_SOURCE="$SCRIPT_DIR/$SCRIPT_SOURCE" ;;
    esac
done
SCRIPT_DIR="$(cd -P "$(dirname "$SCRIPT_SOURCE")" && pwd)"
PROJECT_ROOT="$(cd -P "$SCRIPT_DIR/.." && pwd)"

TEST_SRC_DIR="$PROJECT_ROOT/tests/firmware"
BUILD_DIR="$PROJECT_ROOT/build-host-tests"

if [ ! -f "$TEST_SRC_DIR/CMakeLists.txt" ]; then
    echo "[ERR] test harness not found: $TEST_SRC_DIR/CMakeLists.txt"
    exit 1
fi

echo "[INFO] Configuring host test harness with the host compiler..."
cmake -S "$TEST_SRC_DIR" -B "$BUILD_DIR" -G Ninja

echo "[INFO] Building host test harness..."
cmake --build "$BUILD_DIR"

echo "[INFO] Running ctest..."
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "[OK] Host firmware tests passed"
