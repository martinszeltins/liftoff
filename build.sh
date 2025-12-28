#!/bin/bash
# Build and run script for Liftoff

set -e

echo "==> Setting up build directory..."
meson setup build --wipe

echo "==> Compiling..."
meson compile -C build

echo "==> Running Liftoff..."
./build/src/liftoff "$@"
