#!/bin/sh
# Explicit-profile alternative to Xcode Personal Team installation. See docs/mobile.md.
set -eu
cd "$(dirname "$0")/.."
exec python3 tools/ios/deploy.py "$@"
