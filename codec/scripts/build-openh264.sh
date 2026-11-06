#!/usr/bin/env bash
set -euo pipefail

codec_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source_dir="$codec_dir/.deps/openh264"
prefix="$codec_dir/.deps/install"
revision=1bca62311e447fd9c39fc93de2c4b96cd5ce971c # Cisco v2.7.0

if [[ ! -d "$source_dir" ]]; then
    git clone --depth 1 --branch v2.7.0 https://github.com/cisco/openh264.git "$source_dir"
fi
if [[ "$(git -C "$source_dir" rev-parse HEAD)" != "$revision" ]]; then
    echo "Unexpected OpenH264 revision in $source_dir; expected $revision" >&2
    exit 1
fi

# Use Cisco's native Makefile; install only inside this project.
make -C "$source_dir" -j "${JOBS:-4}" install-static PREFIX="$prefix"
echo "OpenH264 installed in $prefix"
