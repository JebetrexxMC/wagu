#!/usr/bin/env bash
# Installs the MPNetWire plugin into RaspAP (default /var/www/html) with its sudoers rules.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
WEB=${RASPAP_DIR:-/var/www/html}
SRC=$(cd "$(dirname "$0")" && pwd)/MPNetWire
[ -d "$WEB/plugins" ] || mkdir -p "$WEB/plugins"
rm -rf "$WEB/plugins/MPNetWire" && cp -r "$SRC" "$WEB/plugins/MPNetWire" && chown -R www-data:www-data "$WEB/plugins/MPNetWire"
python3 - "$SRC/manifest.json" > /etc/sudoers.d/090_mpnetwire <<'PY'
import json, sys
print("\n".join(json.load(open(sys.argv[1]))["sudoers"]))
PY
chmod 440 /etc/sudoers.d/090_mpnetwire && visudo -cf /etc/sudoers.d/090_mpnetwire
echo "MPNetWire installed: open RaspAP -> MP NetWire"
