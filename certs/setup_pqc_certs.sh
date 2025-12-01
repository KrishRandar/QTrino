#!/bin/bash
#
# Setup script for generating real ML-DSA-44 (Dilithium) certificates
# using OpenSSL with OQS (Open Quantum Safe) provider
#
# Run this script with: bash setup_pqc_certs.sh
#

set -e  # Exit on error

echo "========================================="
echo "PQC Certificate Setup Script"
echo "========================================="
echo ""

# Step 1: Install build dependencies
echo "[STEP 1/5] Installing build dependencies..."
sudo apt update
sudo apt install -y cmake ninja-build libssl-dev git build-essential

# Step 2: Build and install liboqs
echo ""
echo "[STEP 2/5] Building liboqs (Open Quantum Safe library)..."
cd /tmp
if [ -d "liboqs" ]; then
    rm -rf liboqs
fi
git clone --depth 1 --branch main https://github.com/open-quantum-safe/liboqs.git
cd liboqs
mkdir -p build && cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=/usr/local ..
ninja
sudo ninja install
sudo ldconfig

# Step 3: Build and install oqs-provider for OpenSSL 3
echo ""
echo "[STEP 3/5] Building oqs-provider for OpenSSL 3..."
cd /tmp
if [ -d "oqs-provider" ]; then
    rm -rf oqs-provider
fi
git clone --depth 1 https://github.com/open-quantum-safe/oqs-provider.git
cd oqs-provider
cmake -S . -B _build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build _build
sudo cmake --install _build
sudo ldconfig

# Step 4: Verify installation
echo ""
echo "[STEP 4/5] Verifying OQS provider installation..."
openssl list -providers | grep -i oqs || echo "WARNING: OQS provider not found in default path"

# Step 5: Return to project directory
echo ""
echo "[STEP 5/5] Setup complete!"
echo ""
echo "========================================="
echo "Installation Summary"
echo "========================================="
echo "✓ liboqs installed to /usr/local"
echo "✓ oqs-provider installed to /usr/local"
echo ""
echo "Next step: Run the certificate generation script"
echo "  cd certs"
echo "  bash generate_ml_dsa_certs.sh"
echo "========================================="
