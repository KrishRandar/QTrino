# QTrino - Post-Quantum DTLS 1.3 on RISC-V

<div align="center">

**Quantum-Resistant Secure Communication for Embedded Systems**

[![RISC-V](https://img.shields.io/badge/CPU-RISC--V%20VexRISCV-blue)](https://github.com/enjoy-digital/litex)
[![DTLS 1.3](https://img.shields.io/badge/Protocol-DTLS%201.3-green)](https://datatracker.ietf.org/doc/html/rfc9147)
[![PQC](https://img.shields.io/badge/Crypto-Post--Quantum-purple)](https://csrc.nist.gov/projects/post-quantum-cryptography)
[![WolfSSL](https://img.shields.io/badge/Library-WolfSSL-red)](https://www.wolfssl.com/)

</div>

---

## 📖 Overview

**QTrino** is a research project demonstrating **DTLS 1.3 with Post-Quantum Cryptography (PQC)** on resource-constrained RISC-V embedded systems. This implementation showcases:

- ✅ **Quantum-Resistant Security**: ML-KEM-512 (key exchange) + ML-DSA-44 (signatures)
- ✅ **Bare-Metal RISC-V Client**: No OS, running on 1MHz VexRISCV with 100MB RAM
- ✅ **Mutual X.509 Authentication**: Using PQC certificates (~25KB each)
- ✅ **DTLS 1.3 Protocol**: Latest datagram TLS with pure post-quantum cipher suites
- ✅ **Real Hardware Simulation**: LiteX framework for FPGA-style SoC development

### Architecture

```
┌─────────────────────────────────────────┐
│     RISC-V Client (Bare-Metal)          │
│  ┌───────────────────────────────────┐  │
│  │   boot/main.c (DTLS Client)       │  │
│  │   - wolfSSL/wolfCrypt             │  │
│  │   - ML-KEM-512 + ML-DSA-44        │  │
│  │   - 4MB static memory pool        │  │
│  └───────────────────────────────────┘  │
│           ↓ UDP (192.168.1.50)          │
└─────────────────────────────────────────┘
                    │
                    │ DTLS 1.3 Handshake
                    │ (PQC Certificates)
                    ↓
┌─────────────────────────────────────────┐
│      Linux Server (x86_64)              │
│  ┌───────────────────────────────────┐  │
│  │  server/server.c (DTLS Server)    │  │
│  │  - wolfSSL library                │  │
│  │  - Paced I/O (200ms delays)       │  │
│  │  - UDP Port 11111                 │  │
│  └───────────────────────────────────┘  │
│           ↑ tap0 interface              │
└─────────────────────────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites

Ensure your system has:
- **Operating System**: Linux (Ubuntu 20.04+ recommended)
- **Python**: 3.8 or higher
- **Build Tools**: gcc, make, cmake, git
- **Sudo Access**: Required for RISC-V toolchain installation

> [!IMPORTANT]
> **CPU Power Management**: The LiteX simulator is timing-sensitive. For reliable operation:
> - Connect your laptop to AC power (prevents CPU throttling)
> - Set CPU governor to "performance" mode (see below)
> - The client automatically calibrates timeouts for your CPU speed

### CPU Frequency Configuration (Critical!)

The simulation uses busy-wait timing loops that depend on your host CPU speed. When your CPU throttles (battery mode or frequency scaling), timeouts become unreliable.

**Quick Setup:**
```bash
# Validate your environment (checks CPU governor, power source, dependencies)
./scripts/validate_environment.sh

# If warnings appear, configure CPU for performance
sudo ./scripts/setup_cpu_performance.sh
```

**Manual Configuration (if needed):**
```bash
# Check current CPU governor
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# Set to performance mode
sudo cpupower frequency-set -g performance

# Or manually for all CPUs
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu
done
```

**Note**: This setting resets after reboot. Run the setup script before each simulation session.

### 1️⃣ Clone the Repository

```bash
git clone https://github.com/KrishRandar/QTrino.git
cd QTrino
```

### 2️⃣ Setup Python Virtual Environment

Create and activate an isolated Python environment:

```bash
python3 -m venv litex-env
source litex-env/bin/activate
```

Your prompt should now show `(litex-env)`.

### 3️⃣ Initialize LiteX Framework

Make the setup script executable and install LiteX dependencies:

```bash
chmod +x litex_setup.py
./litex_setup.py --init --install
pip3 install meson ninja
```

### 4️⃣ Install RISC-V Toolchain

Install the RISC-V GCC cross-compiler (requires sudo):

```bash
sudo ./litex_setup.py --gcc=riscv
```

This installs `riscv64-unknown-elf-gcc` system-wide.

### 5️⃣ Install System Dependencies

```bash
sudo apt install libevent-dev libjson-c-dev verilator
```

These packages enable LiteX simulation and SoC generation.

---

## 🔧 Building the Project

### Step 1: Generate the SoC

Remove any previous build and create the LiteX SoC with Ethernet support:

```bash
rm -rf build/sim

litex_sim \
  --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --with-ethernet \
  --integrated-main-ram-size=0x06400000 \
  --no-compile-gateware
```

**Parameters:**
- `--cpu-type=vexriscv`: RISC-V CPU core
- `--cpu-variant=full`: Full RV32IM with all extensions
- `--with-ethernet`: Enable network interface
- `--integrated-main-ram-size=0x06400000`: 100MB RAM
- `--no-compile-gateware`: Skip Verilog compilation (faster for software-only work)

### Step 2: Build Bare-Metal Demo

Generate the base bare-metal software framework:

```bash
litex_bare_metal_demo --build-path=build/sim
```

This creates the runtime environment and linker scripts.

### Step 3: Generate PQC Certificates

Navigate to the `certs` directory and build the certificate generator:

```bash
cd certs
make clean
make
```

**Output files:**
- `ca_cert.h` - Root CA certificate (25,667 bytes)
- `server_cert.h`, `server_key.h` - Server credentials
- `client_cert.h`, `client_key.h` - Client credentials

These certificates use **ML-DSA-44 (Dilithium)** for signatures and are embedded as C headers.

### Step 4: Build Client Firmware

Compile the RISC-V bare-metal DTLS client:

```bash
cd boot
make clean
make
```

**Output:** `boot.bin` (~443KB firmware image)

This compilation:
- Links wolfSSL library with PQC support
- Embeds certificates into the binary
- Allocates 4MB static memory for PQC operations
- Creates bootable firmware for RISC-V

### Step 5: Build Server Application

Compile the Linux DTLS server:

```bash
cd ../server
make clean
make
```

**Output:** `server` (Linux x86_64 executable)

---

## ▶️ Running the Demo

You'll need **two terminals** - one for the client simulator, one for the server.

### Terminal 1: Start RISC-V Client Simulation

```bash
litex_sim --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 \
  --with-ethernet \
  --ram-init=boot/boot.bin
```

**What happens:**
- Simulates RISC-V SoC at ~1MHz
- Boots firmware from `boot.bin`
- Creates TAP network interface (192.168.1.50)
- Runs DTLS client

**Wait 2-3 seconds** for initialization before starting the server.

### Terminal 2: Start Server

```bash
cd server
./server
```

The server:
- Binds to UDP port 11111 on tap0
- Configures static ARP for client (192.168.1.50)
- Waits for DTLS handshake
- Uses paced sending (200ms delays) to accommodate slow client

---

## ✅ Expected Output

### Client Console (Terminal 1)

```
================================================================================
                    PQC-DTLS 1.3 Client - RISC-V Bare-Metal
================================================================================
[CONFIG] Algorithm:  ML-KEM-512 + ML-DSA-44
[CONFIG] Protocol:   DTLS 1.3 (Pure PQC)
[CONFIG] Auth:       X.509 Mutual Authentication
================================================================================

[INIT] Initializing network layer...
[OK] Network initialized

[MEMORY] Static memory pool: 4194304 bytes allocated

[CERT] Loading CA certificate (25667 bytes)...
[OK] CA certificate loaded

[CERT] Loading client certificate (25933 bytes)...
[OK] Client certificate loaded

[HANDSHAKE] Starting DTLS 1.3 handshake...
[HANDSHAKE] Sending ClientHello...
[HANDSHAKE] Processing ServerHello...
[HANDSHAKE] Verifying server certificate...
[OK] DTLS 1.3 HANDSHAKE COMPLETE!

[DATA] Sending: "Hello from RISC-V PQC-DTLS client!"
[DATA] Received: "Hello from PQC-DTLS server!"

[SUCCESS] PQC-DTLS 1.3 DEMO COMPLETE!
```

### Server Console (Terminal 2)

```
================================================================================
                         PQC-DTLS 1.3 Server
================================================================================
[CONFIG] Algorithm:  ML-KEM-512 + ML-DSA-44
[CONFIG] Protocol:   DTLS 1.3 (Pure PQC)
[CONFIG] Port:       11111
[CONFIG] Pacing:     200ms delay between sends
================================================================================

[INIT] Initializing wolfSSL...
[OK] wolfSSL initialized

[CERT] Loading server certificate (25933 bytes)...
[OK] Server certificate loaded

[NETWORK] Binding to tap0 interface...
[OK] Network configured

[READY] Waiting for client from 192.168.1.50...

[HANDSHAKE] Received ClientHello
[HANDSHAKE] Sending ServerHello...
[PACING] Sent certificate (25933 bytes), waiting 200ms...
[HANDSHAKE] Verifying client certificate...
[OK] DTLS 1.3 HANDSHAKE COMPLETE!

[DATA] Received: "Hello from RISC-V PQC-DTLS client!"
[DATA] Sending: "Hello from PQC-DTLS server!"

[SUCCESS] Session complete
```

---

## 🔍 Troubleshooting

### Issue: "Failed to bind to tap0"

**Solution:** The server needs to create/configure the TAP interface:

```bash
cd server
sudo ./setup_network.sh
```

This script:
- Creates tap0 interface
- Assigns IP 192.168.1.100 to tap0
- Adds static ARP entry for client (192.168.1.50)

### Issue: "Handshake timeout" or "WANT_READ"

**Cause:** The 1MHz RISC-V client is slow - PQC operations can take several seconds.

**Solution:** This is **expected behavior**. The client and server automatically retry with DTLS retransmission. Wait up to 60 seconds for handshake completion.

### Issue: "Certificate verification failed"

**Cause:** Mismatched or missing certificates.

**Solution:**
1. Rebuild certificates: `cd certs && make clean && make`
2. Rebuild client: `cd boot && make clean && make`
3. Ensure both client and server use the same certificate set

### Issue: "Permission denied" for network setup

**Cause:** Network configuration requires root privileges.

**Solution:** Run server with sudo or configure permissions:

```bash
sudo ./server/server
```

### Issue: Slow handshake performance

**Cause:** PQC signatures (ML-DSA-44) are computationally expensive on 1MHz CPU.

**Expected times:**
- ClientHello: ~2 seconds
- Certificate verification: ~15-30 seconds
- Full handshake: ~60-90 seconds

This is **normal** for this constraint environment.

### Issue: Client/server stuck on "waiting to receive" (on battery or different laptop)

**Symptoms:**
- Works fine on AC power, fails on battery
- Works on one laptop but not another
- Both endpoints stuck waiting indefinitely

**Cause:** CPU frequency scaling causes busy-wait timeout loops to become unreliable.

**Solution:**
```bash
# 1. Validate your environment
./scripts/validate_environment.sh

# 2. If warnings appear about CPU governor or battery:
sudo ./scripts/setup_cpu_performance.sh

# 3. Connect AC power adapter if possible

# 4. Retry the simulation
```

**Technical details:**
- The client now automatically calibrates timeouts for your CPU speed
- Base timeouts are increased to 60 seconds (client) and 3 seconds (server pacing)
- However, performance mode is still recommended for consistent timing

**If issues persist on specific hardware:**
- Some older/slower CPUs may need even longer timeouts
- Reduce background CPU load (close browsers, etc.)
- Check that virtualization is not adding overhead

---

## 📊 Technical Details

### System Specifications

| Component | Details |
|-----------|---------|
| **CPU** | RISC-V VexRISCV (RV32IM) @ ~1MHz |
| **RAM** | 100MB integrated SRAM |
| **Network** | LiteEth MAC + TAP interface |
| **Toolchain** | riscv64-unknown-elf-gcc |
| **C Library** | Picolibc (embedded) |
| **Build System** | Make + LiteX |

### Cryptographic Suite

| Algorithm | Purpose | Key Size | Signature/CT Size |
|-----------|---------|----------|-------------------|
| **ML-KEM-512** | Key Encapsulation | 800 bytes (public) | 768 bytes (ciphertext) |
| **ML-DSA-44** | Digital Signatures | 1,312 bytes (public) | ~2,420 bytes (signature) |
| **SHA3-256** | Hashing | N/A | 32 bytes |
| **AES-128-GCM** | Symmetric Encryption | 128 bits | N/A |

### Memory Usage

- **Static allocation**: 4MB for wolfSSL + PQC operations
- **Certificate storage**: ~68KB (CA + client cert + key)
- **Firmware size**: ~443KB
- **Peak RAM usage**: ~5MB during handshake

---

## 🛠️ Development

### Project Structure

```
QTrino/
├── boot/                   # RISC-V client firmware
│   ├── main.c             # DTLS client application
│   ├── network.c          # Bare-metal network stack
│   ├── Makefile           # Build configuration
│   ├── wolfssl/           # WolfSSL headers
│   └── wolfcrypt/         # WolfCrypt implementation
├── server/                # Linux server application
│   ├── server.c           # DTLS server with pacing
│   ├── Makefile           # Server build config
│   └── setup_network.sh   # TAP interface setup
├── certs/                 # PQC certificate generation
│   ├── gen_pqc_certs.c    # Certificate generator
│   ├── convert_pem_to_header.py  # PEM → C header converter
│   └── Makefile           # Certificate build
├── litex/                 # LiteX SoC framework
├── build/                 # Build artifacts
└── README.md              # This file
```

### Modifying the Client

To customize the RISC-V client:

1. Edit `boot/main.c`
2. Rebuild: `cd boot && make clean && make`
3. Re-run simulation with new firmware: `litex_sim ... --ram-init=boot/boot.bin`

### Modifying the Server

To customize the server:

1. Edit `server/server.c`
2. Rebuild: `cd server && make clean && make`
3. Restart: `./server/server`

### Generating New Certificates

To create fresh PQC certificates:

```bash
cd certs
make clean
make
```

The certificates are automatically embedded in both client and server during their respective builds.

---

## 📚 References

- **LiteX**: [https://github.com/enjoy-digital/litex](https://github.com/enjoy-digital/litex)
- **WolfSSL**: [https://www.wolfssl.com/](https://www.wolfssl.com/)
- **NIST PQC**: [https://csrc.nist.gov/projects/post-quantum-cryptography](https://csrc.nist.gov/projects/post-quantum-cryptography)
- **DTLS 1.3 RFC**: [https://datatracker.ietf.org/doc/html/rfc9147](https://datatracker.ietf.org/doc/html/rfc9147)
- **ML-KEM (Kyber)**: [https://pq-crystals.org/kyber/](https://pq-crystals.org/kyber/)
- **ML-DSA (Dilithium)**: [https://pq-crystals.org/dilithium/](https://pq-crystals.org/dilithium/)

---

## 🎯 Research Significance

This project demonstrates:

1. **Feasibility**: Post-quantum cryptography is viable on resource-constrained embedded systems
2. **Performance**: DTLS 1.3 handshakes complete even at 1MHz with large PQC certificates
3. **Security**: Quantum-resistant mutual authentication without hardware acceleration
4. **Bare-metal**: No OS overhead - direct hardware programming for maximum control

### Future Work

- Hardware acceleration for PQC operations
- Optimization for lower memory footprint
- Integration with real FPGA hardware
- Performance profiling and bottleneck analysis
- Alternative PQC algorithms (Falcon, SPHINCS+)

---

## 📄 License

This project uses components with various licenses:
- **LiteX**: BSD-2-Clause
- **WolfSSL**: GPLv2 or Commercial
- **VexRISCV**: MIT

See individual component licenses for details.

---

## 🙏 Acknowledgments

Developed by **QTrino Labs Pvt Ltd** as part of embedded systems security research focusing on post-quantum cryptography for IoT and edge computing applications.

---

<div align="center">

**Ready to build quantum-resistant embedded systems? Get started above! 🚀**

For questions or contributions, please open an issue or pull request.

</div>
