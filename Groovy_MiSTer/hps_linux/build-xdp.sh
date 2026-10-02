#!/bin/bash
set -euo pipefail
exec bash "$(dirname "$0")/build-receiver.sh" xdp "$@"
