#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# StackChan K151 build helper. All generated files stay inside this checkout.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
fw="$here/firmware/esp32"
cmd=${1:?build-public|build|reconfig|flash|monitor|test}
case "$cmd" in
build-public|build|reconfig|flash) python3 "$here/scripts/check_assets.py" ;;
esac
B=build-k151
[ "$cmd" != build-public ] || B=build-k151-public
mkdir -p "$here/.cache/tmp"
export TMPDIR="$here/.cache/tmp"
export IDF_COMPONENT_CACHE_PATH="$here/.cache/components"
export PYTHONDONTWRITEBYTECODE=1

defaults="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-m5stack-stackchan"
if [ "$cmd" = build-public ]; then
    defaults="$defaults;devices/sdkconfig.public"
elif [ -f "$here/private/k151.local.cfg" ]; then
    defaults="$defaults;$here/private/k151.local.cfg"
fi

idf_env() {
    if ! command -v idf.py >/dev/null; then
        local activate=${IDF_EXPORT:-${IDF_PATH:+$IDF_PATH/export.sh}}
        [ -n "$activate" ] && [ -f "$activate" ] || {
            echo 'Activate ESP-IDF v6.0.1 first, or set IDF_EXPORT to its export.sh.' >&2; exit 1;
        }
        set +eu
        . "$activate" >/dev/null 2>&1
        local rc=$?
        set -eu
        [ "$rc" = 0 ] || { echo 'ESP-IDF activation failed.' >&2; exit 1; }
    fi
    local version
    version=$(idf.py --version)
    [[ "$version" = "ESP-IDF v6.0.1" ]] || {
        echo 'This checkout requires ESP-IDF v6.0.1.' >&2; exit 1;
    }
}

config_gate() {
    python3 - "$fw/$B/sdkconfig" "$cmd" <<'PY'
from pathlib import Path
import re,sys
p=Path(sys.argv[1])
if not p.exists(): sys.exit(0)
s=p.read_text()
unsafe=['NVS_ENCRYPTION','SECURE_BOOT','SECURE_FLASH_ENC_ENABLED','HOMEHUB_PAIRING_EFUSE_AUTH','HOMEHUB_OTA_ENABLED']
if any(re.search(r'^CONFIG_'+k+r'=y$',s,re.M) for k in unsafe):
    sys.exit('Unsafe development configuration; refusing build/flash.')
if sys.argv[2]=='build-public':
    names=['GADGET_SDK_TOKEN','MUSE_TTS_MINIMAX_KEY','HOMEHUB_WIFI_SSID','HOMEHUB_WIFI_PASSWORD']
    if any(re.search(r'^CONFIG_'+k+r'="[^"\n]+"',s,re.M) for k in names):
        sys.exit('Public build configuration contains credentials; use a fresh public build directory.')
PY
}

case "$cmd" in
build-public|build|reconfig)
    idf_env
    python "$here/scripts/check_dependency_lock.py"
    cd "$fw"
    if [ "$cmd" = reconfig ]; then rm -f "$B/sdkconfig"; fi
    config_gate
    rc=0
    idf.py -B "$B" -DIDF_TARGET=esp32s3 -DSDKCONFIG="$B/sdkconfig" \
        -DSDKCONFIG_DEFAULTS="$defaults" build >"$B.log" 2>&1 || rc=$?
    # Do not echo the build log: private overlays can be repeated in compiler errors.
    [ "$rc" = 0 ] || { echo "Build failed (exit $rc). Inspect firmware/esp32/$B.log locally." >&2; exit "$rc"; }
    python "$here/scripts/check_dependency_lock.py"
    config_gate
    echo "Build passed: firmware/esp32/$B (logs and binaries are ignored by Git)."
    ;;
flash)
    [[ "${K151_MAC:-}" =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]] || {
        echo 'Set K151_MAC to your own lowercase device MAC.' >&2; exit 4;
    }
    p=${2:?Pass the serial PORT explicitly}
    idf_env
    config_gate
    cd "$fw/$B"
    mac=$(python -m esptool -p "$p" --after no-reset read-mac 2>&1 | awk '/^MAC:/ && !m {m=tolower($2)} END{print m}')
    [ "$mac" = "$K151_MAC" ] || { echo 'Device identity mismatch; refusing flash.' >&2; exit 4; }
    python -m esptool --chip esp32s3 -p "$p" -b 921600 --before default-reset --after hard-reset write-flash '@flash_args'
    ;;
monitor)
    p=${2:?Pass the serial PORT explicitly}
    idf_env
    python "$fw/tools/muse/monitor.py" "$p" "${3:-30}"
    ;;
test)
    cd "$fw"
    python3 -m unittest discover -s tests -p 'test_*.py'
    ;;
*) echo 'Unknown command.' >&2; exit 2 ;;
esac
