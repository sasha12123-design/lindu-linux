#!/usr/bin/env bash
# lindu linux — сборка ISO через Docker (Linux/WSL2/macOS)
# Windows: используйте powershell -ExecutionPolicy Bypass -File build/build-in-docker.ps1

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "==> Сборка образа сборщика"
docker build -f "$SCRIPT_DIR/Dockerfile" -t lindu-builder "$ROOT"

echo "==> Сборка ISO (10-20 минут)"
mkdir -p "$ROOT/out" "$ROOT/work"
docker run --rm --privileged \
    -v "$ROOT/out:/lindu-out" \
    -v "$ROOT/work:/lindu-work" \
    -e LINDU_OUT_DIR=/lindu-out \
    -e LINDU_WORK_DIR=/lindu-work \
    lindu-builder

echo
echo "Готово! ISO в: $ROOT/out"