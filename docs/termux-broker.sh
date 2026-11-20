#!/data/data/com.termux/files/usr/bin/bash
# MP region broker on an Android phone (Termux), 24/7.
#   curl -fsSL https://raw.githubusercontent.com/JebetrexxMC/wagu/main/docs/termux-broker.sh | bash
#   curl -fsSL .../termux-broker.sh | bash -s -- DUCKDNS_TOKEN   (also points the 4 duckdns names at this phone's IP)
#   ~/mp-broker.sh status | stop | start
# Router: forward TCP 1883 and 8883 to this phone's Wi-Fi IP (Settings > Wi-Fi > IP address).
set -e
P="${PREFIX:-/data/data/com.termux/files/usr}"
CONF="$P/etc/mosquitto/mp.conf"
PW="$P/etc/mosquitto/pw"
USER_NAME="mp"
USER_PASS="M6oehkIzqcJKSTalT8nL"
DOMAINS="mpsamp,eusamp,nasamp,ocsamp"
TOKEN="${1:-${DUCKDNS_TOKEN:-}}"

echo "[MP] installing mosquitto"
pkg install -y mosquitto termux-api >/dev/null 2>&1 || pkg install -y mosquitto
mkdir -p "$P/etc/mosquitto" "$HOME/.termux/boot" "$HOME/mp-broker"
mosquitto_passwd -c -b "$PW" "$USER_NAME" "$USER_PASS"
printf 'listener 1883 0.0.0.0\nlistener 8883 0.0.0.0\nallow_anonymous false\npassword_file %s\npersistence false\nlog_dest file %s/mp-broker/mosquitto.log\n' "$PW" "$HOME" > "$CONF"
[ -n "$TOKEN" ] && echo "$TOKEN" > "$HOME/mp-broker/duckdns.token" && chmod 600 "$HOME/mp-broker/duckdns.token"

# keep-alive loop: restarts mosquitto within 2 s, refreshes DuckDNS every 5 min
cat > "$HOME/mp-broker/loop.sh" <<'EOF2'
#!/data/data/com.termux/files/usr/bin/bash
D="$HOME/mp-broker"; CONF="$PREFIX/etc/mosquitto/mp.conf"; n=0
while true; do
  pgrep -x mosquitto >/dev/null || mosquitto -d -c "$CONF"
  if [ $((n % 150)) -eq 0 ] && [ -s "$D/duckdns.token" ]; then
    curl -s "https://www.duckdns.org/update?domains=mpsamp,eusamp,nasamp,ocsamp&token=$(cat "$D/duckdns.token")&ip=" > "$D/duckdns.last"
  fi
  n=$((n+1)); sleep 2
done
EOF2
chmod +x "$HOME/mp-broker/loop.sh"

cat > "$HOME/mp-broker.sh" <<'EOF2'
#!/data/data/com.termux/files/usr/bin/bash
D="$HOME/mp-broker"
case "$1" in
  stop) pkill -f mp-broker/loop.sh; pkill -x mosquitto; echo "MP broker stopped"; exit 0;;
  status) pgrep -x mosquitto >/dev/null && echo "mosquitto running (1883, 8883)" || echo "mosquitto NOT running"
          echo "public IP: $(curl -s https://api.ipify.org)"; tail -n 5 "$D/mosquitto.log" 2>/dev/null; exit 0;;
esac
pgrep -f mp-broker/loop.sh >/dev/null && { echo "MP broker already running"; exit 0; }
command -v termux-wake-lock >/dev/null && termux-wake-lock
nohup "$D/loop.sh" >/dev/null 2>&1 &
sleep 2; echo "MP broker started"
EOF2
chmod +x "$HOME/mp-broker.sh"

# start after reboot (needs the Termux:Boot app, opened once)
printf '#!/data/data/com.termux/files/usr/bin/bash\n$HOME/mp-broker.sh start\n' > "$HOME/.termux/boot/mp-broker"
chmod +x "$HOME/.termux/boot/mp-broker"

"$HOME/mp-broker.sh" start
sleep 2
"$HOME/mp-broker.sh" status
[ -s "$HOME/mp-broker/duckdns.last" ] && echo "DuckDNS: $(cat "$HOME/mp-broker/duckdns.last")"
LAN=$(ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1)
echo
echo "Router (http://192.168.1.1) port forwarding:"
echo "  MP-MQTT      TCP 1883 -> ${LAN:-192.168.1.137}:1883"
echo "  MP-MQTT-TLS  TCP 8883 -> ${LAN:-192.168.1.137}:8883"
