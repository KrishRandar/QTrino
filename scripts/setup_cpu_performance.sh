#!/bin/bash
# setup_cpu_performance.sh
# Sets CPU governor to performance mode for reliable LiteX simulation
#
# Usage: sudo ./scripts/setup_cpu_performance.sh

echo "========================================"
echo "QTrino CPU Performance Mode Setup"
echo "========================================"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "[ERROR] This script must be run as root"
    echo "Usage: sudo ./scripts/setup_cpu_performance.sh"
    exit 1
fi

# Check if cpupower is available
if ! command -v cpupower &> /dev/null; then
    echo "[WARNING] cpupower not found. Installing..."
    apt-get update && apt-get install -y linux-tools-generic linux-tools-$(uname -r)
fi

echo "[INFO] Current CPU governor:"
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

echo ""
echo "[ACTION] Setting all CPUs to performance mode..."

# Method 1: Try cpupower (preferred)
if command -v cpupower &> /dev/null; then
    cpupower frequency-set -g performance
    if [ $? -eq 0 ]; then
        echo "[OK] CPU frequency governor set to 'performance'"
    else
        echo "[WARNING] cpupower failed, trying manual method..."
    fi
fi

# Method 2: Manual fallback
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    if [ -f "$cpu" ]; then
        echo performance > "$cpu"
    fi
done

echo ""
echo "[VERIFY] Current CPU governors:"
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor | sort | uniq -c

echo ""
echo "[INFO] Current CPU frequencies:"
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null | head -4

echo ""
echo "========================================"
echo "[SUCCESS] CPU configured for optimal simulation performance"
echo ""
echo "Note: This setting will reset after reboot."
echo "Run this script again before each simulation session."
echo "========================================"
