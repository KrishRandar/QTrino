#!/bin/bash
#
# Generate ML-DSA-44 (Dilithium Level 2) certificates for PQC-DTLS
#
# This script creates:
# 1. Root CA (self-signed, ML-DSA-44)
# 2. Server certificate (signed by CA, ML-DSA-44)
# 3. Client certificate (signed by CA, ML-DSA-44)
#
# All certificates are generated in DER format with 1-year validity
#

set -e

echo "========================================="
echo "ML-DSA-44 Certificate Generation"
echo "========================================="
echo ""

# Set OpenSSL to use OQS provider (installed location)
export OPENSSL_MODULES=/usr/lib/x86_64-linux-gnu/ossl-modules
export OPENSSL_CONF=/tmp/openssl-oqs.cnf

# Create temporary OpenSSL config with OQS provider
cat > /tmp/openssl-oqs.cnf << 'EOF'
openssl_conf = openssl_init

[openssl_init]
providers = provider_sect

[provider_sect]
default = default_sect
oqsprovider = oqsprovider_sect

[default_sect]
activate = 1

[oqsprovider_sect]
activate = 1
EOF

# Verify OQS provider is available
echo "[CHECK] Verifying OQS provider..."
if ! openssl list -providers 2>/dev/null | grep -q oqsprovider; then
    echo "ERROR: OQS provider not found!"
    echo "Please run setup_pqc_certs.sh first"
    exit 1
fi
echo "✓ OQS provider found"
echo ""

# Check if ML-DSA-44 is available
echo "[CHECK] Verifying ML-DSA-44 support..."
if ! openssl list -signature-algorithms -provider oqsprovider 2>/dev/null | grep -i "ML-DSA-44"; then
    echo "ERROR: ML-DSA-44 not available!"
    exit 1
fi
echo "✓ ML-DSA-44 available"
echo ""

#=============================================================================
# STEP 1: Generate Root CA
#=============================================================================
echo "[STEP 1/3] Generating Root CA (ML-DSA-44)..."

# Generate CA private key
openssl genpkey -algorithm ML-DSA-44 -out ca_key.pem
echo "  ✓ CA private key generated"

# Create CA certificate (self-signed, 1 year validity)
openssl req -new -x509 -key ca_key.pem \
    -out ca_cert.pem -days 365 \
    -subj "/C=US/ST=CA/O=QTrino Labs/CN=Root CA" \
    -provider oqsprovider -provider default
echo "  ✓ CA certificate created (1 year validity)"

# Convert to DER format
openssl x509 -in ca_cert.pem -outform DER -out ca_cert.der
openssl pkey -in ca_key.pem -outform DER -out ca_key.der
echo "  ✓ Converted to DER format"

#=============================================================================
# STEP 2: Generate Server Certificate
#=============================================================================
echo ""
echo "[STEP 2/3] Generating Server Certificate (ML-DSA-44)..."

# Generate server private key
openssl genpkey -algorithm ML-DSA-44 -out server_key.pem
echo "  ✓ Server private key generated"

# Create certificate signing request
openssl req -new -key server_key.pem \
    -out server.csr \
    -subj "/C=US/ST=CA/O=QTrino Labs/CN=DTLS Server" \
    -provider oqsprovider -provider default
echo "  ✓ Server CSR created"

# Sign with CA (1 year validity)
openssl x509 -req -in server.csr \
    -CA ca_cert.pem -CAkey ca_key.pem \
    -CAcreateserial -out server_cert.pem -days 365 \
    -provider oqsprovider -provider default
echo "  ✓ Server certificate signed by CA"

# Convert to DER format
openssl x509 -in server_cert.pem -outform DER -out server_cert.der
openssl pkey -in server_key.pem -outform DER -out server_key.der
echo "  ✓ Converted to DER format"

#=============================================================================
# STEP 3: Generate Client Certificate
#=============================================================================
echo ""
echo "[STEP 3/3] Generating Client Certificate (ML-DSA-44)..."

# Generate client private key
openssl genpkey -algorithm ML-DSA-44 -out client_key.pem
echo "  ✓ Client private key generated"

# Create certificate signing request
openssl req -new -key client_key.pem \
    -out client.csr \
    -subj "/C=US/ST=CA/O=QTrino Labs/CN=RISC-V Client" \
    -provider oqsprovider -provider default
echo "  ✓ Client CSR created"

# Sign with CA (1 year validity)
openssl x509 -req -in client.csr \
    -CA ca_cert.pem -CAkey ca_key.pem \
    -CAcreateserial -out client_cert.pem -days 365 \
    -provider oqsprovider -provider default
echo "  ✓ Client certificate signed by CA"

# Convert to DER format
openssl x509 -in client_cert.pem -outform DER -out client_cert.der
openssl pkey -in client_key.pem -outform DER -out client_key.der
echo "  ✓ Converted to DER format"

#=============================================================================
# STEP 4: Convert DER to C Headers
#=============================================================================
echo ""
echo "[STEP 4/4] Converting to C headers..."

# Function to convert DER to C header
der_to_header() {
    local der_file=$1
    local header_file=$2
    local var_name=$3
    
    xxd -i "$der_file" | sed "s/unsigned char.*\[/unsigned char ${var_name}[/" | \
        sed "s/unsigned int.*/unsigned int ${var_name}_len = $(stat -f%z "$der_file" 2>/dev/null || stat -c%s "$der_file");/" \
        > "$header_file"
    
    echo "  ✓ Generated $header_file"
}

der_to_header "ca_cert.der" "ca_cert.h" "ca_cert_der"
der_to_header "server_cert.der" "server_cert.h" "server_cert_der"
der_to_header "server_key.der" "server_key.h" "server_key_der"
der_to_header "client_cert.der" "client_cert.h" "client_cert_der"
der_to_header "client_key.der" "client_key.h" "client_key_der"

#=============================================================================
# Summary
#=============================================================================
echo ""
echo "========================================="
echo "Certificate Generation Complete!"
echo "========================================="
echo ""
echo "Generated files:"
echo "  PEM format (human-readable):"
echo "    ca_cert.pem, ca_key.pem"
echo "    server_cert.pem, server_key.pem"
echo "    client_cert.pem, client_key.pem"
echo ""
echo "  DER format (binary, for code):"
echo "    ca_cert.der, ca_key.der"
echo "    server_cert.der, server_key.der"
echo "    client_cert.der, client_key.der"
echo ""
echo "  C headers (for embedding):"
echo "    ca_cert.h, server_cert.h, server_key.h"
echo "    client_cert.h, client_key.h"
echo ""
echo "Certificate details:"
echo "  Algorithm: ML-DSA-44 (Dilithium Level 2)"
echo "  Validity: 1 year from today"
echo "  Organization: QTrino Labs"
echo "  Country: US"
echo "========================================="
echo ""
echo "Next steps:"
echo "  1. Verify certificates: openssl x509 -in ca_cert.pem -text -noout"
echo "  2. Update boot/certs_placeholder.h to use these headers"
echo "  3. Rebuild firmware: cd ../boot && make"
echo "========================================="

# Cleanup temporary files
rm -f server.csr client.csr ca_cert.srl /tmp/openssl-oqs.cnf
