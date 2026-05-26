#!/bin/bash
set -eo pipefail

BUILD_DIR="${BUILD_DIR:-build}"

cd "${BUILD_DIR}"

echo "=== Running unit tests..."
ctest --output-on-failure
