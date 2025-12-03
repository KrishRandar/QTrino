#!/bin/bash
# validate_environment.sh
# Pre-flight validation for QTrino simulation
#
# Usage: ./scripts/validate_environment.sh

echo "========================================"
echo "QTrino Environment Validation"
echo "========================================"
echo ""

warnings=0
errors=0

# Check 1: CPU Frequency Governor
echo "[CHECK 1] CPU Frequency Scaling"
if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ]; then
    governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)
    echo "  Current governor: $governor"
    
    if [ "$governor" != "performance" ]; then
        echo "  [WARNING] CPU not in performance mode"
        echo "  Recommendation: Run 'sudo ./scripts/setup_cpu_performance.sh'"
        warnings=$((warnings + 1))
    else
        echo "  [OK] CPU in performance mode"
    fi
else
    echo "  [INFO] CPU frequency scaling not available (VM or embedded system)"
fi

echo ""

# Check 2: Power Source
echo "[CHECK 2] Power Source"
if [ -d /sys/class/power_supply ]; then
    on_battery=0
    for bat in /sys/class/power_supply/BAT*/status; do
        if [ -f "$bat" ]; then
            status=$(cat "$bat")
            if [ "$status" = "Discharging" ]; then
                on_battery=1
                echo "  [WARNING] Running on battery power"
                echo "  CPU may throttle during simulation"
                echo "  Recommendation: Connect AC power adapter"
                warnings=$((warnings + 1))
            fi
        fi
    done
    
    if [ $on_battery -eq 0 ]; then
        echo "  [OK] Running on AC power or battery status unknown"
    fi
else
    echo "  [INFO] Power source detection not available"
fi

echo ""

# Check 3: Dependencies
echo "[CHECK 3] Required Dependencies"

deps=("python3" "verilator" "riscv64-unknown-elf-gcc")
for dep in "${deps[@]}"; do
    if command -v $dep &> /dev/null; then
        echo "  [OK] $dep found"
    else
        echo "  [ERROR] $dep not found"
        errors=$((errors + 1))
    fi
done

echo ""

# Check 4: Virtual Environment
echo "[CHECK 4] Python Virtual Environment"
if [ -n "$VIRTUAL_ENV" ]; then
    echo "  [OK] Virtual environment active: $VIRTUAL_ENV"
else
    echo "  [WARNING] No virtual environment detected"
    echo "  Recommendation: source litex-env/bin/activate"
    warnings=$((warnings + 1))
fi

echo ""

# Check 5: Network Configuration
echo "[CHECK 5] Network Configuration"
if ip link show tap0 &> /dev/null; then
    echo "  [OK] tap0 interface exists"
else
    echo "  [INFO] tap0 will be created by simulation"
fi

echo ""
echo "========================================"
echo "Validation Summary"
echo "========================================"
echo "Errors:   $errors"
echo "Warnings: $warnings"
echo ""

if [ $errors -gt 0 ]; then
    echo "[FAIL] Please fix errors before running simulation"
    exit 1
elif [ $warnings -gt 0 ]; then
    echo "[PASS] Environment OK with warnings"
    echo "Simulation may work but could be unreliable"
    exit 0
else
    echo "[PASS] Environment optimal for simulation"
    exit 0
fi
