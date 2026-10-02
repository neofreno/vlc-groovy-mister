#!/bin/bash
# Compatibility entry point. Requires only an isolated build root;
# never reuse one object tree for all three variants or deploy via FTP here.
set -euo pipefail
exec bash "$(dirname "$0")/../build-all.sh" "$@"
