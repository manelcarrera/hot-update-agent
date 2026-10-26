#!/usr/bin/env bash
# Demo: server + client running v1, then v2 is published and the client switches
# to it without restarting. Usage: scripts/demo.sh   (builds with the "dev" preset)
set -euo pipefail
cd "$(dirname "$0")/.."
cmake --preset dev >/dev/null && cmake --build --preset dev >/dev/null
B=build/dev
W=$(mktemp -d)
EXT=so; [[ "$(uname)" == Darwin ]] && EXT=dylib
PORT=47811

"$B/nx-server" --port $PORT --store "$W/store" 2>&1 | sed -u 's/^/  server | /' &
sleep 0.3
"$B/nx-client" --port $PORT --module "$B/functional_v1.$EXT" --cache "$W/client" 2>&1 | sed -u 's/^/  client | /' &
sleep 1
echo "  ------ publishing 2.0.0 ------"
"$B/nx-publish" --port $PORT --version 2.0.0 --file "$B/functional_v2.$EXT" | sed -u 's/^/  publish| /'
sleep 1
pkill -INT -f "nx-client --port $PORT" || true
pkill -INT -f "nx-server --port $PORT" || true
wait
rm -rf "$W"
