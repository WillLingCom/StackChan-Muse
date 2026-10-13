#!/bin/bash
# Saves the MiniMax API key into private/k151.local.cfg without echoing it.
# Run it yourself in Terminal:  ./scripts/set_tts_key.sh
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
cfg=$here/private/k151.local.cfg
mkdir -p "$here/private"; chmod 700 "$here/private"

printf '粘贴 MiniMax API Key（只按一次 Command+V，输入时屏幕不会显示），然后按回车: '
IFS= read -rs key; echo
key=$(printf '%s' "$key" | tr -d '[:space:]')

# One paste only: a key pasted twice reads as the same string repeated.
period=$(printf '%s' "$key" | python3 -c 'import sys
s=sys.stdin.read()
i=(s+s).find(s,1)
print(i if 0<i<len(s) else 0)')
if [ "$period" != 0 ]; then
    echo "✗ 看起来粘贴了 $(( ${#key} / period )) 次，未保存。请只按一次 Command+V 再回车。" >&2; exit 1
fi
if [ ${#key} -lt 20 ] || [ ${#key} -gt 1023 ]; then
    echo "✗ 长度 ${#key} 不像 MiniMax Key，未保存。请从 platform.minimaxi.com 重新复制。" >&2; exit 1
fi
if printf '%s' "$key" | grep -q '["\\]'; then echo "✗ 含有引号或反斜杠，未保存。" >&2; exit 1; fi

touch "$cfg"; chmod 600 "$cfg"
tmp=$(mktemp "$here/private/.cfg.XXXXXX")
grep -v '^#\{0,1\}CONFIG_MUSE_TTS_MINIMAX_KEY=' "$cfg" > "$tmp" || true
printf 'CONFIG_MUSE_TTS_MINIMAX_KEY="%s"\n' "$key" >> "$tmp"
mv "$tmp" "$cfg"; chmod 600 "$cfg"
echo "✓ 已保存到 private/k151.local.cfg（长度 ${#key}，仅本机可读）。运行 scripts/k151.sh reconfig 以应用新配置。"
