#!/usr/bin/env sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PORT=${1:-8080}

echo "WiepView: http://127.0.0.1:${PORT}/"
exec python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$SCRIPT_DIR"
