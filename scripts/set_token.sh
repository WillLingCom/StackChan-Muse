#!/bin/bash
# Saves the Muse SDK token into private/k151.local.cfg without echoing it.
# Run it yourself in Terminal:  ./scripts/set_token.sh
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
cfg=$here/private/k151.local.cfg
mkdir -p "$here/private"; chmod 700 "$here/private"

printf '粘贴 Muse SDK token（以 mgst_ 开头，输入时屏幕不会显示），然后按回车: '
IFS= read -rs tok; echo
tok=$(printf '%s' "$tok" | tr -d '[:space:]')
n=$(printf '%s' "$tok" | grep -o 'mgst_' | wc -l | tr -d ' ')
if [ "$n" -gt 1 ]; then echo "✗ 检测到粘贴了 $n 次，未保存。请只按一次 Command+V 再回车。" >&2; exit 1; fi
# Same rule as firmware/esp32/cmake/validate_config.cmake: mgst_ + 43 base64url chars.
if ! printf '%s' "$tok" | grep -Eq '^mgst_[A-Za-z0-9_-]{42}[AEIMQUYcgkosw048]$'; then
    echo "✗ 格式不对（应为 mgst_ 开头、共 48 个字符，实际 ${#tok} 个），未保存。请重新复制。" >&2; exit 1
fi
if printf '%s' "$tok" | grep -q '["\\]'; then echo "✗ token 含有引号或反斜杠，未保存。" >&2; exit 1; fi

touch "$cfg"; chmod 600 "$cfg"
tmp=$(mktemp "$here/private/.cfg.XXXXXX")
grep -v '^#\{0,1\}CONFIG_GADGET_SDK_TOKEN=' "$cfg" > "$tmp" || true
printf 'CONFIG_GADGET_SDK_TOKEN="%s"\n' "$tok" >> "$tmp"
mv "$tmp" "$cfg"; chmod 600 "$cfg"
echo "✓ 已保存到 private/k151.local.cfg（长度 ${#tok}，仅本机可读）。运行 scripts/k151.sh reconfig 以应用新配置。"
