# PQC-DTLS 1.3 on RISC-V Bare-Metal - Build & Run Instructions

## Overview

This project implements **DTLS 1.3 with pure post-quantum cryptography** on a RISC-V bare-metal system using:
- **ML-KEM-512** (NIST FIPS 203) for key encapsulation
- **ML-DSA-44** (NIST FIPS 204) for digital signatures  
- **X.509 certificate-based mutual authentication**
- **LiteX + Verilator** simulation environment
- **wolfSSL/wolfCrypt** cryptographic library

---

## Prerequisites

- ✅ LiteX environment installed (Python 3.13.7)
- ✅ RISC-V GCC toolchain
- ✅ Verilator
- ✅ wolfSSL library (system-wide installation)
- ✅ Virtual environment activated: `source litex-env/bin/activate`

---

## Project Structure

```
Constraint_Env_Sim/
├── boot/                      # RISC-V bare-metal firmware
│   ├── main.c                 # DTLS client implementation
│   ├── network.c/h            # UDP networking layer (LiteEth)
│   ├── certs_placeholder.h    # Certificate headers (placeholder)
│   ├── Makefile               # Firmware build system
│   ├── linker.ld              # Memory layout
│   ├── crt0.S                 # Startup code
│   └── wolfssl/               # wolfSSL library
├── server/                    # Host machine DTLS server
│   ├── server.c               # DTLS 1.3 server
│   └── Makefile               # Server build system
├── certs/                     # Certificate generation
│   ├── gen_pqc_certs.c        # Certificate generator
│   └── Makefile               # Certificate build system
├── csr.json                   # SoC configuration (with Ethernet)
└── README_BUILD.md            # This file
```

---

## Build Instructions

### Step 1: Activate Virtual Environment

**REQUIRED for all subsequent commands:**

```bash
source litex-env/bin/activate
```

### Step 2: Verify SoC Configuration

Check that Ethernet support is enabled:

```bash
cat csr.json | grep -i eth
```

Expected output: `ethmac`, `ethphy` registers should be present.

If not present, regenerate SoC:

```bash
litex_sim --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 \
  --with-ethernet
```

### Step 3: Generate PQC Certificates (TODO)

> [!WARNING]
> Certificate generation is currently using placeholder code. The `gen_pqc_certs.c` program needs to be tested and may require API adjustments based on wolfSSL's Dilithium support.

```bash
cd certs
make
./gen_pqc_certs
```

This should generate:
- `ca_cert.der` / `ca_cert.h` - Root CA certificate
- `server_cert.der` / `server_cert.h` - Server certificate  
- `server_key.der` / `server_key.h` - Server private key
- `client_cert.der` / `client_cert.h` - Client certificate
- `client_key.der` / `client_key.h` - Client private key

**If certificate generation fails**, you can:
1. Use pre-generated test certificates from wolfSSL examples
2. Generate certificates using OpenSSL with OQS provider
3. Continue testing without certificates (handshake will fail but network layer can be tested)

### Step 4: Build RISC-V Firmware

```bash
cd boot
make clean
make
```

This compiles:
- `main.c` - DTLS client
- `network.c` - UDP networking layer
- `wolfcrypt/src/*.c` - wolfSSL cryptographic functions

Output: `boot.bin` (firmware binary)

### Step 5: Build DTLS Server (Host Machine)

```bash
cd server
make
```

Output: `server` (executable)

---

## Running the Demo

### Terminal 1: Start DTLS Server

```bash
cd server
./server
```

Expected output:
```
========================================
PQC-DTLS 1.3 Server
========================================
Algorithm: ML-KEM-512 + ML-DSA-44
Protocol:  DTLS 1.3 (Pure PQC)
Port:      11111
========================================

[INIT] Initializing wolfSSL...
[OK] wolfSSL initialized

[DTLS] Creating DTLS 1.3 server context...
[OK] DTLS 1.3 context created

[SOCKET] Binding to port 11111...
[OK] Socket bound to port 11111

========================================
SERVER READY - WAITING FOR CLIENT
========================================
```

### Terminal 2: Run RISC-V Simulation

```bash
source litex-env/bin/activate

litex_sim --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 \
  --with-ethernet \
  --ram-init=boot/boot.bin
```

Expected output from client:
```
========================================
PQC-DTLS 1.3 Client - RISC-V Bare-Metal
========================================
Algorithm: ML-KEM-512 + ML-DSA-44
Protocol:  DTLS 1.3 (Pure PQC)
Auth:      X.509 Mutual Authentication
========================================

[INIT] Initializing network...
[NET] Network initialized
[NET] Local IP: 192.168.1.50:12345
[NET] Server: 192.168.1.100:11111

[INIT] Initializing wolfSSL...
[OK] wolfSSL initialized

[DTLS] Creating DTLS 1.3 client context...
[OK] DTLS 1.3 context created

========================================
STARTING DTLS 1.3 HANDSHAKE
========================================
```

### Terminal 3 (Optional): Capture with Wireshark

```bash
sudo wireshark -i tap0 -f "udp port 11111"
```

This captures the DTLS handshake for analysis.

---

## Network Configuration

| Component | IP Address | Port | MAC Address |
|-----------|------------|------|-------------|
| RISC-V Client | 192.168.1.50 | 12345 | 10:e2:d5:00:00:00 |
| Host Server | 192.168.1.100 | 11111 | (host) |

---

## Troubleshooting

### Issue: "Ethernet not configured in SoC"

**Solution**: Regenerate SoC with `--with-ethernet` flag (see Step 2)

### Issue: Certificate loading fails

**Symptoms**:
```
[WARNING] No CA certificate available (placeholder)
[WARNING] No client certificate available (placeholder)
```

**Solution**: 
1. Generate certificates using `certs/gen_pqc_certs` (Step 3)
2. Replace `boot/certs_placeholder.h` with actual certificate headers
3. Rebuild firmware

### Issue: Network timeout during handshake

**Symptoms**:
```
[IO] Receive timeout
[ERROR] Handshake failed
```

**Possible causes**:
1. Server not running
2. IP address mismatch
3. Firewall blocking UDP port 11111
4. TAP interface not configured properly

**Solution**:
```bash
# Check server is listening
netstat -ulnp | grep 11111

# Check TAP interface
ip addr show tap0

# Allow UDP traffic
sudo ufw allow 11111/udp
```

### Issue: Build errors with wolfSSL

**Symptoms**:
```
undefined reference to 'wolfSSL_CTX_new'
```

**Solution**: Install wolfSSL with PQC support:
```bash
# Install wolfSSL from source with Dilithium support
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl
./autogen.sh
./configure --enable-dilithium --enable-kyber --enable-dtls13
make
sudo make install
sudo ldconfig
```

---

## Performance Metrics

To measure performance (for technical report):

### 1. Latency
Measure time from handshake start to completion:
```c
// In main.c, add timing code:
uint64_t start_time = read_cycle_counter();
ret = wolfSSL_connect(ssl);
uint64_t end_time = read_cycle_counter();
uint64_t cycles = end_time - start_time;
printf("Handshake cycles: %llu\n", cycles);
```

### 2. Memory Usage
Check firmware size:
```bash
riscv64-unknown-elf-size boot/boot.elf
```

### 3. Throughput
Measure data transfer rate after handshake establishment.

---

## Next Steps

1. ✅ **Generate real PQC certificates** using `gen_pqc_certs`
2. ✅ **Test network layer** independently
3. ✅ **Complete DTLS handshake** with mutual authentication
4. ✅ **Capture Wireshark trace** for deliverable
5. ✅ **Measure performance metrics** for technical report
6. ✅ **Write technical report** (2-3 pages)

---

## Important Notes

> [!CAUTION]
> **Security Warning**: The current entropy source (`CustomRngGenerateBlock`) is NOT cryptographically secure. For production use, integrate a hardware TRNG.

> [!NOTE]
> **Pure PQC**: This implementation uses ONLY post-quantum algorithms. All classical algorithms (RSA, ECC, X25519, DH) are disabled.

> [!TIP]
> **Debugging**: Enable verbose wolfSSL debugging by ensuring `DEBUG_WOLFSSL` is defined in `user_settings.h`.

---

## References

- **LiteX**: https://github.com/enjoy-digital/litex
- **wolfSSL**: https://www.wolfssl.com/
- **NIST PQC**: https://csrc.nist.gov/projects/post-quantum-cryptography
- **DTLS 1.3 RFC**: https://datatracker.ietf.org/doc/rfc9147/
- **ML-KEM (FIPS 203)**: https://csrc.nist.gov/pubs/fips/203/final
- **ML-DSA (FIPS 204)**: https://csrc.nist.gov/pubs/fips/204/final
