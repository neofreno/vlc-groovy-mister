#!/bin/bash
# Unified sources and isolated object tree per transport. No deployment.
set -euo pipefail
variant=${1:?Usage: build-receiver.sh standard|xdp|wifi|all isolated-build-root}
repo=$(cd "$(dirname "$0")/.." && pwd)
source_tree="$repo/hps_linux/src"
build_root=$(realpath -m "${2:?Pass a separate build root}")
case "$variant" in standard|xdp|wifi|all) ;; *) echo "Unknown variant: $variant" >&2; exit 1;; esac
case "$build_root/" in "$source_tree/"*|"$repo/"*) echo 'Use a build root outside source repositories' >&2; exit 1;; esac
test -f "$source_tree/hardware.h"
command -v rsync >/dev/null
command -v python3 >/dev/null
python3 "$repo/tests/test_core_cleanup_hooks.py" "$source_tree/fpga_io.cpp"

build_variant() {
    local selected=$1 artifact af_xdp=0 wifi=0
    case "$selected" in
        standard) artifact=MiSTer_groovy ;;
        xdp) artifact=MiSTer_groovy_XDP; af_xdp=1 ;;
        wifi) artifact=MiSTer_groovy_wifi; wifi=1 ;;
    esac
    local build_tree="$build_root/$selected"
    local build_source="$build_tree/hps_linux/src"
    mkdir -p "$build_source/support/groovy"
    # Refresh base sources on every build; never import objects from another
    # variant or use the stale fpga_io.cpp in a previous isolated build.
    rsync -a --exclude='/.git/' --exclude='/.vscode/' --exclude='/support/groovy/' \
        --exclude='/Makefile' --exclude='/MiSTer*' --exclude='*.o' --exclude='*.d' \
        --exclude='*.elf' "$source_tree/" "$build_source/"
    rsync -a "$repo/protocol/" "$build_tree/protocol/"
    if [ "$selected" = xdp ]; then
        test -f "$source_tree/support/groovy/kernel/usr/include/linux/if_xdp.h" || {
            echo 'Missing bundled AF_XDP kernel headers in hps_linux/src/support/groovy/kernel/usr/include' >&2; return 1;
        }
        mkdir -p "$build_source/support/groovy/kernel/usr/include"
        rsync -a "$source_tree/support/groovy/kernel/usr/include/" "$build_source/support/groovy/kernel/usr/include/"
    fi
    cp "$repo/hps_linux/src/support/groovy/"*.cpp "$repo/hps_linux/src/support/groovy/"*.h "$build_source/support/groovy/"
    cp "$repo/hps_linux/src/Makefile" "$build_source/Makefile"
    python3 "$repo/tests/test_core_cleanup_hooks.py" "$build_source/fpga_io.cpp"
    make -C "$build_source" -j"${GROOVY_BUILD_JOBS:-4}" _AF_XDP="$af_xdp" _WIFI_MODE="$wifi" "$artifact"
    cp "$build_source/$artifact" "$repo/hps_linux/$artifact"
    echo "Built $repo/hps_linux/$artifact (isolated: $build_source)"
}

if [ "$variant" = all ]; then
    for selected in standard xdp wifi; do build_variant "$selected"; done
else
    build_variant "$variant"
fi
