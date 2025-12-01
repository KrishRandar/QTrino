#!/bin/bash
#
# setup_network.sh - Configure Linux host network for DTLS server
#
# This script sets up static ARP entry and routing for the RISC-V client
# to ensure the server can send HelloRetryRequest with correct MAC address.
#
# Client: 192.168.1.50 (MAC: 10:e2:d5:00:00:00)
# Server: 192.168.1.100 (on tap0 interface)
#

# Don't use set -e here because we need to handle interface waiting gracefully
set +e

CLIENT_IP="192.168.1.50"
CLIENT_MAC="10:e2:d5:00:00:00"
INTERFACE="tap0"

echo "=========================================="
echo "Network Setup for DTLS Server"
echo "=========================================="
echo "Client IP:  $CLIENT_IP"
echo "Client MAC: $CLIENT_MAC"
echo "Interface:  $INTERFACE"
echo "=========================================="
echo ""

# Wait for interface to appear (created by litex_sim)
echo "[NETWORK] Waiting for $INTERFACE interface to appear..."
MAX_WAIT=30
WAIT_COUNT=0
while ! ip link show "$INTERFACE" > /dev/null 2>&1; do
    if [ $WAIT_COUNT -ge $MAX_WAIT ]; then
        echo "[ERROR] Interface $INTERFACE did not appear after ${MAX_WAIT} seconds!"
        echo "[INFO]  Start litex_sim first to create the tap interface:"
        echo "        litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \\"
        echo "          --integrated-main-ram-size=0x06400000 --with-ethernet --ram-init=boot/boot.bin"
        exit 1
    fi
    sleep 1
    WAIT_COUNT=$((WAIT_COUNT + 1))
    if [ $((WAIT_COUNT % 5)) -eq 0 ]; then
        echo "[INFO]  Still waiting for $INTERFACE... (${WAIT_COUNT}s)"
    fi
done
echo "[OK] Interface $INTERFACE found"

# Re-enable exit on error for the rest of the script
set -e

# Check if interface is up
if ! ip link show "$INTERFACE" | grep -q "state UP"; then
    echo "[WARNING] Interface $INTERFACE is not UP"
    echo "[INFO]    Bringing interface up..."
    sudo ip link set "$INTERFACE" up || {
        echo "[ERROR] Failed to bring interface up"
        exit 1
    }
fi

# Remove existing ARP entry if present (ignore errors)
sudo ip neigh del "$CLIENT_IP" dev "$INTERFACE" 2>/dev/null || true

# Add static ARP entry
echo "[NETWORK] Adding static ARP entry..."
if sudo ip neigh add "$CLIENT_IP" lladdr "$CLIENT_MAC" dev "$INTERFACE"; then
    echo "[OK] Static ARP entry added"
else
    echo "[ERROR] Failed to add ARP entry"
    exit 1
fi

# Add/update route to client (if not already present)
echo "[NETWORK] Configuring route..."
if ip route get "$CLIENT_IP" > /dev/null 2>&1; then
    # Route exists, check if it's correct
    if ip route get "$CLIENT_IP" | grep -q "dev $INTERFACE"; then
        echo "[OK] Route already configured correctly"
    else
        echo "[INFO] Removing existing route..."
        sudo ip route del "$CLIENT_IP" 2>/dev/null || true
        echo "[INFO] Adding route via $INTERFACE..."
        sudo ip route add "$CLIENT_IP/32" dev "$INTERFACE" || {
            echo "[ERROR] Failed to add route"
            exit 1
        }
        echo "[OK] Route configured"
    fi
else
    # Route doesn't exist, add it
    echo "[INFO] Adding route via $INTERFACE..."
    if sudo ip route add "$CLIENT_IP/32" dev "$INTERFACE"; then
        echo "[OK] Route added"
    else
        echo "[ERROR] Failed to add route"
        exit 1
    fi
fi

echo ""
echo "=========================================="
echo "Verification"
echo "=========================================="

# Verify ARP entry
echo "[VERIFY] ARP table for $INTERFACE:"
ip neigh show dev "$INTERFACE" | grep "$CLIENT_IP" || {
    echo "[WARNING] ARP entry not found in verification"
}

# Verify route
echo "[VERIFY] Route to $CLIENT_IP:"
ip route get "$CLIENT_IP" || {
    echo "[WARNING] Route verification failed"
}

echo ""
echo "=========================================="
echo "Network Setup Complete"
echo "=========================================="
echo "[INFO] You can now start the DTLS server:"
echo "       ./server/server"
echo ""

