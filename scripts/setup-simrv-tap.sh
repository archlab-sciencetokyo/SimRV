#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "run as root: sudo $0" >&2
    exit 1
fi

tap_iface=${SIMRV_TAP_IFACE:-simrv0}
guest_user=${SUDO_USER:-${USER:-root}}
uplink=$(ip route show default 2>/dev/null | awk 'NR == 1 {print $5}')

ip tuntap add dev "$tap_iface" mode tap user "$guest_user" 2>/dev/null || true
ip addr replace 10.0.2.1/24 dev "$tap_iface"
ip link set "$tap_iface" up

sysctl -q -w net.ipv4.ip_forward=1
if command -v iptables >/dev/null 2>&1 && [ -n "$uplink" ]; then
    iptables -t nat -C POSTROUTING -s 10.0.2.0/24 -o "$uplink" -j MASQUERADE 2>/dev/null || \
        iptables -t nat -A POSTROUTING -s 10.0.2.0/24 -o "$uplink" -j MASQUERADE
    iptables -C FORWARD -i "$tap_iface" -o "$uplink" -j ACCEPT 2>/dev/null || \
        iptables -A FORWARD -i "$tap_iface" -o "$uplink" -j ACCEPT
    iptables -C FORWARD -i "$uplink" -o "$tap_iface" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || \
        iptables -A FORWARD -i "$uplink" -o "$tap_iface" -m state --state RELATED,ESTABLISHED -j ACCEPT
elif command -v nft >/dev/null 2>&1 && [ -n "$uplink" ]; then
    # WSL and newer minimal hosts commonly provide nftables without the
    # iptables compatibility command. Keep the rules in a private table so
    # repeated setup runs remain idempotent and do not alter other firewalls.
    if ! nft list table ip simrv >/dev/null 2>&1; then
        nft -f - <<EOF
table ip simrv {
    chain forward {
        type filter hook forward priority filter; policy accept;
        iifname "$tap_iface" oifname "$uplink" accept
        iifname "$uplink" oifname "$tap_iface" ct state related,established accept
    }
    chain postrouting {
        type nat hook postrouting priority srcnat; policy accept;
        ip saddr 10.0.2.0/24 oifname "$uplink" masquerade
    }
}
EOF
    fi
fi

echo "SimRV TAP ready: $tap_iface (host 10.0.2.1, guest 10.0.2.2)"
