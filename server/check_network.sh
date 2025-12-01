#!/bin/bash
#
# check_network.sh - Verify network configuration
#

echo "=========================================="
echo "Network Configuration Check"
echo "=========================================="
echo ""

echo "[ARP] Checking ARP table for tap0:"
ip neigh show dev tap0 | grep -E "(192.168.1.50|192.168.1.100)" || echo "  No entries found"
echo ""

echo "[ROUTE] Checking route to client:"
ip route get 192.168.1.50 2>/dev/null || echo "  Route not found"
echo ""

echo "[INTERFACE] Checking tap0 status:"
ip link show tap0 2>/dev/null | grep -E "(state|inet)" || echo "  Interface not found"
echo ""

echo "[VERIFY] Expected configuration:"
echo "  Client IP:  192.168.1.50"
echo "  Client MAC: 10:e2:d5:00:00:00"
echo "  Server IP:  192.168.1.100"
echo "  Interface:  tap0"
echo ""


