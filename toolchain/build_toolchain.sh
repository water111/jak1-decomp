#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# Set up build container
podman build --arch 386 -t musl-builder -f Dockerfile .

mkdir -p bin

podman run --rm --arch 386 \
  --userns=keep-id \
  -v "$PWD/bin:/out" \
  musl-builder

