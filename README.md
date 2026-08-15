# QTrino - Technical Documentation

## Post-Quantum DTLS 1.3 on RISC-V with Performance Measurement

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Directory Structure Explanation](#2-directory-structure-explanation)
3. [How to Compile the RISC-V Firmware](#3-how-to-compile-the-risc-v-firmware)
4. [How to Run on LiteX + Verilator](#4-how-to-run-on-litex--verilator)
5. [How to Start DTLS Server](#5-how-to-start-dtls-server)
6. [Complete Execution Walkthrough](#6-complete-execution-walkthrough)
7. [Configuration Reference](#7-configuration-reference)
8. [Session Resumption](#8-session-resumption)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Project Overview

**QTrino** implements **DTLS 1.3 with Post-Quantum Cryptography (PQC)** on a bare-metal RISC-V embedded system. The project demonstrates quantum-resistant secure communication using:

- **ML-KEM-512** (Kyber) for key exchange
- **ML-DSA-44** (Dilithium) for digital signatures
- **Raw Public Key (RPK)** authentication (RFC 7250)
- **RISC-V VexRISCV** CPU running at 1MHz in simulation

### Architecture Diagram

```
┌─────────────────────────────────────────┐
│     RISC-V Client (Bare-Metal)          │
│  ┌───────────────────────────────────┐  │
│  │   boot/main.c (DTLS Client)       │  │
│  │   - wolfSSL/wolfCrypt             │  │
│  │   - ML-KEM-512 + ML-DSA-44        │  │
│  │   - 4MB static memory pool        │  │
│  └───────────────────────────────────┘  │
│           ↓ UDP (192.168.1.50:random)   │
└─────────────────────────────────────────┘
                    │
                    │ DTLS 1.3 Handshake
                    │ (RPK Authentication)
                    ↓
┌─────────────────────────────────────────┐
│      Linux Server (x86_64)              │
│  ┌───────────────────────────────────┐  │
│  │  server/server.c (DTLS Server)    │  │
│  │  - wolfSSL library                │  │
│  │  - Paced I/O for slow client      │  │
│  │  - UDP Port 11111                 │  │
│  └───────────────────────────────────┘  │
│           ↑ tap0 (192.168.1.100)        │
└─────────────────────────────────────────┘
```

---

## 2. Directory Structure Explanation

```
QTrino/
├── README.md                   # Project overview and quick start
├── DOCUMENTATION.md            # This file - detailed technical documentation
├── csr.json                    # Generated CSR (Control/Status Register) map
├── litex_setup.py              # LiteX framework initialization script
├── boot.fbi                    # FBI format boot image (for some loaders)
│
├── boot/                       # RISC-V CLIENT FIRMWARE
│   ├── main.c                  # DTLS 1.3 client implementation
│   ├── network.c               # Bare-metal Ethernet/UDP stack
│   ├── network.h               # Network interface definitions
│   ├── performance.c           # RISC-V cycle counter utilities
│   ├── performance.h           # Performance measurement API
│   ├── Makefile                # Firmware build configuration
│   ├── linker.ld               # Memory layout for RISC-V
│   ├── crt0.o                  # C runtime startup (compiled)
│   ├── certs_placeholder.h     # Embedded RPK keys header
│   ├── boot.bin                # OUTPUT: Raw binary firmware
│   ├── boot.elf                # OUTPUT: ELF executable with symbols
│   ├── boot.elf.map            # OUTPUT: Memory map file
│   ├── wolfssl/                # wolfSSL TLS library headers
│   │   └── src/                # wolfSSL source files
│   └── wolfcrypt/              # wolfCrypt cryptographic library
│       └── src/                # wolfCrypt source files (ML-KEM, ML-DSA, etc.)
│
├── server/                     # LINUX x86_64 DTLS SERVER
│   ├── server.c                # DTLS 1.3 server with echo loop
│   ├── Makefile                # Server build configuration
│   ├── user_settings.h         # wolfSSL compile-time configuration
│   ├── server_rpk.h            # Server private key + client public key
│   ├── server                  # OUTPUT: Compiled server executable
│   └── *.o                     # Compiled object files
│
├── certs/                      # RPK KEY GENERATION
│   ├── generate_rpk_keys.sh    # Script to generate ML-DSA-44 keypairs
│   ├── client_key.pem          # Client ML-DSA-44 private key (PEM)
│   ├── client_key.der          # Client private key (DER format)
│   ├── client_pubkey.der       # Client public key (SubjectPublicKeyInfo)
│   ├── client_rpk.h            # C header: client privkey + server pubkey
│   ├── server_key.pem          # Server ML-DSA-44 private key (PEM)
│   ├── server_key.der          # Server private key (DER format)
│   ├── server_pubkey.der       # Server public key (SubjectPublicKeyInfo)
│   └── server_rpk.h            # C header: server privkey + client pubkey
│
├── build/                      # LITEX BUILD OUTPUT
│   └── sim/                    # Simulation build artifacts
│       ├── gateware/           # Verilator-generated simulation
│       └── software/           # Generated software includes
│           └── include/
│               └── generated/  # Auto-generated headers (csr.h, mem.h, etc.)
│
├── litex/                      # LiteX SoC framework
├── litex-boards/               # Board definition files
├── litex-env/                  # Python virtual environment
├── migen/                      # Migen HDL library
├── liteeth/                    # Ethernet MAC/PHY IP core
├── litedram/                   # DRAM controller IP core
├── litespi/                    # SPI controller IP core
└── pythondata-cpu-vexriscv/    # VexRISCV CPU core
```

### Key Directories Explained

| Directory | Purpose |
|-----------|---------|
| `boot/` | Contains all RISC-V bare-metal client firmware source code, including the DTLS client, network stack, and embedded wolfSSL/wolfCrypt libraries |
| `server/` | Linux x86_64 DTLS server application that communicates with the RISC-V client via TAP interface |
| `certs/` | ML-DSA-44 Raw Public Key generation scripts and pre-generated keys for mutual authentication |
| `build/sim/` | LiteX-generated simulation files, includes auto-generated memory maps and CSR definitions |
| `litex*/` | LiteX framework and related IP cores (installed via `litex_setup.py`) |

---

## 3. How to Compile the RISC-V Firmware

### 3.1 Toolchain Requirements

The following tools are required to compile the RISC-V firmware:

| Tool | Version | Purpose |
|------|---------|---------|
| `riscv64-unknown-elf-gcc` | 12.1.0+ | RISC-V cross-compiler |
| `riscv64-unknown-elf-ld` | 2.38+ | RISC-V linker |
| `riscv64-unknown-elf-objcopy` | 2.38+ | Binary conversion tool |
| `make` | 4.0+ | Build automation |
| `Python` | 3.8+ | LiteX framework |

### 3.2 Installing the RISC-V Toolchain

The RISC-V GCC toolchain is installed via the LiteX setup script:

```bash
# Install RISC-V toolchain (requires sudo)
sudo ./litex_setup.py --gcc=riscv
```

This installs the toolchain to `/opt/riscv/` and updates your PATH.

**Verify installation:**

```bash
riscv64-unknown-elf-gcc --version
# Expected: riscv64-unknown-elf-gcc (GCC) 12.1.0 or higher
```

### 3.3 Compilation Commands

**Step 1: Ensure LiteX build artifacts exist**

Before compiling the firmware, you must generate the SoC (see Section 4). The firmware depends on auto-generated headers in `build/sim/software/include/generated/`.

**Step 2: Compile the firmware**

```bash
# Navigate to the boot directory
cd /path/to/QTrino/boot

# Clean previous build artifacts
make clean

# Compile firmware
make
```

**Step 3: Verify compilation**

```bash
ls -la boot.bin boot.elf
```

### 3.4 Expected Output Binaries

| File | Size | Description |
|------|------|-------------|
| `boot.elf` | ~1.5 MB | ELF executable with debug symbols, used for debugging |
| `boot.bin` | ~449 KB | Raw binary firmware, loaded into RISC-V RAM |
| `boot.elf.map` | ~200 KB | Memory map showing symbol addresses |

**Example successful build output:**

```
$ make
riscv64-unknown-elf-gcc ... -c main.c -o main.o
riscv64-unknown-elf-gcc ... -c network.c -o network.o
riscv64-unknown-elf-gcc ... -c performance.c -o performance.o
[... wolfSSL/wolfCrypt compilation ...]
riscv64-unknown-elf-gcc -T linker.ld -N -o boot.elf ...
riscv64-unknown-elf-objcopy -O binary boot.elf boot.bin
```

---

## 4. How to Run on LiteX + Verilator

### 4.1 Prerequisites

Install system dependencies:

```bash
sudo apt update
sudo apt install -y \
    libevent-dev \
    libjson-c-dev \
    verilator \
    build-essential \
    python3-dev \
    python3-pip \
    python3-venv
```

### 4.2 LiteX Framework Setup

**Step 1: Create and activate Python virtual environment**

```bash
cd /path/to/QTrino
python3 -m venv litex-env
source litex-env/bin/activate
```

**Step 2: Initialize LiteX framework**

```bash
chmod +x litex_setup.py
./litex_setup.py --init --install
pip3 install meson ninja
```

**Step 3: Install RISC-V toolchain**

```bash
sudo ./litex_setup.py --gcc=riscv
```

### 4.3 LiteX SoC Build Steps

**Step 1: Clean previous build (if any)**

```bash
rm -rf build/sim
```

**Step 2: Generate SoC with Verilator simulation**

```bash
litex_sim \
    --csr-json csr.json \
    --cpu-type=vexriscv \
    --cpu-variant=full \
    --with-ethernet \
    --integrated-main-ram-size=0x06400000 \
    --no-compile-gateware
```

**Command explanation:**

| Option | Value | Description |
|--------|-------|-------------|
| `--csr-json` | `csr.json` | Output CSR register map to JSON file |
| `--cpu-type` | `vexriscv` | Use VexRISCV RISC-V CPU |
| `--cpu-variant` | `full` | Full-featured CPU (RV32IM) |
| `--with-ethernet` | - | Enable LiteEth MAC for network |
| `--integrated-main-ram-size` | `0x06400000` | 100MB main RAM |
| `--no-compile-gateware` | - | Skip Verilog compilation (first pass) |

**Step 3: Build bare-metal demo (generates required headers)**

```bash
litex_bare_metal_demo --build-path=build/sim
```

**Step 4: Build firmware (see Section 3)**

```bash
cd boot
make clean && make
cd ..
```

### 4.4 Verilator Simulation Command

**Start the RISC-V simulation:**

```bash
# Ensure virtual environment is activated
source litex-env/bin/activate

# Run simulation with firmware
litex_sim \
    --csr-json csr.json \
    --cpu-type=vexriscv \
    --cpu-variant=full \
    --integrated-main-ram-size=0x06400000 \
    --with-ethernet \
    --ram-init=boot/boot.bin
```

**Command explanation:**

| Option | Value | Description |
|--------|-------|-------------|
| `--ram-init` | `boot/boot.bin` | Load firmware into main RAM at boot |

### 4.5 UART/Serial Output Logs

The simulation outputs to the terminal directly. Expected boot sequence:

```
        __   _ __      _  __
       / /  (_) /____ | |/_/
      / /__/ / __/ -_)>  <
     /____/_/\__/\__/_/|_|
   Build your hardware, easily!

 (c) Copyright 2012-2024 Enjoy-Digital
 (c) Copyright 2007-2015 M-Labs

 BIOS built on Dec  5 2025 18:26:40
 BIOS CRC passed (00000000)

 LiteX git sha1: xxxxxxxx

--=============== SoC ==================--
CPU:            VexRiscv Full @ 1MHz
BUS:            wishbone 32-bit @ 4GiB
CSR:            32-bit data
ROM:            128KiB
SRAM:           8KiB
MAIN-RAM:       102400KiB

--============= Console ================--

===============================================================================
              PQC-DTLS 1.3 Client - RISC-V Bare-Metal
===============================================================================
[CONFIG] Algorithm:  ML-KEM-512 (Key Exchange) + ML-DSA-44 (Signatures)
[CONFIG] Protocol:   DTLS 1.3 (Pure Post-Quantum Cryptography)
[CONFIG] Auth:       Raw Public Key (RPK) Mutual Authentication (RFC 7250)
===============================================================================

[INIT] Initializing network interface...
[OK] Ethernet initialized successfully
...
```

### 4.6 Configuration Values

#### Memory Map (from `csr.json`)

| Region | Base Address | Size | Type |
|--------|--------------|------|------|
| ROM | `0x00000000` | 128 KB | Cached |
| SRAM | `0x10000000` | 8 KB | Cached |
| Main RAM | `0x40000000` | 100 MB | Cached |
| Ethernet MAC | `0x80000000` | 8 KB | I/O |
| CSR | `0xF0000000` | 64 KB | I/O |

#### CSR Registers

| Register | Address | Description |
|----------|---------|-------------|
| `ctrl_reset` | `0xF0000000` | System reset control |
| `uart_rxtx` | `0xF0002800` | UART data register |
| `timer0_value` | `0xF0001830` | Timer counter value |
| `ethmac_*` | `0xF0000800` | Ethernet MAC control |

#### Clock Configuration

| Parameter | Value |
|-----------|-------|
| System Clock | 1 MHz (simulated) |
| CPU Type | VexRISCV RV32IM |
| Timer Resolution | 1 μs |

#### Network Configuration

| Parameter | Value |
|-----------|-------|
| Client IP | 192.168.1.50 |
| Server IP | 192.168.1.100 |
| Server Port | 11111 (UDP) |
| TAP Interface | tap0 |

---

## 5. How to Start DTLS Server

### 5.1 Server Binary Location

The server executable is located at:

```
QTrino/server/server
```

### 5.2 Building the Server

```bash
# Navigate to server directory
cd /path/to/QTrino/server

# Clean and build
make clean
make
```

**Expected output:**

```
gcc -I. -I../boot -I../boot/wolfssl ... -c server.c -o server.o
[... wolfSSL/wolfCrypt compilation ...]
gcc ... -o server server.o *.o
Server built successfully!
```

### 5.3 wolfSSL/wolfCrypt Configuration

The server uses the same wolfSSL configuration as the client, defined in `server/user_settings.h`:

**Key configuration options:**

```c
// Protocol settings
#define WOLFSSL_DTLS13           // Enable DTLS 1.3
#define WOLFSSL_TLS13            // TLS 1.3 base support
#define WOLFSSL_SEND_HRR_COOKIE  // HelloRetryRequest cookies

// Post-Quantum Cryptography
#define HAVE_DILITHIUM           // ML-DSA signatures
#define WOLFSSL_DILITHIUM_44     // ML-DSA-44 (Level 2)
#define HAVE_LIBOQS              // LibOQS integration
#define WOLFSSL_EXPERIMENTAL_SETTINGS

// Raw Public Key (RPK) Authentication
#define HAVE_RPK                 // RFC 7250 support

// Memory settings
#define WOLFSSL_STATIC_MEMORY    // Static memory allocation
#define WOLFSSL_NO_MALLOC        // No dynamic allocation
```

### 5.4 Server IP and Port

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Bind Address** | `0.0.0.0` | Listens on all interfaces |
| **Port** | `11111` | UDP port for DTLS |
| **TAP Interface** | `tap0` | Virtual network interface to LiteX simulation |
| **Expected Client** | `192.168.1.50` | RISC-V client IP address |

### 5.5 Starting the Server

**Terminal 2 (separate from simulation):**

```bash
cd /path/to/QTrino/server

# Run the server
./server
```

**Expected server startup output:**

```
===============================================================================
                        PQC-DTLS 1.3 Server
===============================================================================
[CONFIG] Listening on 192.168.1.100:11111
[CONFIG] Algorithms: ML-KEM-512 + ML-DSA-44
[CONFIG] Authentication: Raw Public Key (RPK) Mutual Authentication
===============================================================================
```

### 5.6 Steps to Reproduce the Handshake

**Complete handshake reproduction procedure:**

1. **Terminal 1: Start DTLS Server**

   ```bash
   cd /path/to/QTrino/server
   ./server
   ```

   Wait for the server to display "Listening on 192.168.1.100:11111"

2. **Terminal 2: Start RISC-V Client Simulation**

   ```bash
   cd /path/to/QTrino
   source litex-env/bin/activate
   
   litex_sim \
       --csr-json csr.json \
       --cpu-type=vexriscv \
       --cpu-variant=full \
       --integrated-main-ram-size=0x06400000 \
       --with-ethernet \
       --ram-init=boot/boot.bin
   ```

3. **Observe handshake** (takes 60-90 seconds due to 1MHz CPU and PQC operations)

4. **Expected handshake flow:**

   ```
   Client                                Server
      |                                     |
      |-------- ClientHello --------------->|
      |<------- HelloRetryRequest ----------|
      |-------- ClientHello (retry) ------->|
      |<------- ServerHello ----------------|
      |<------- EncryptedExtensions --------|
      |<------- CertificateRequest ---------|
      |<------- Certificate (RPK) ----------|
      |<------- CertificateVerify ----------|
      |<------- Finished -------------------|
      |-------- Certificate (RPK) --------->|
      |-------- CertificateVerify --------->|
      |-------- Finished ------------------>|
      |                                     |
      |<====== Secure Channel =============>|
   ```

---

## 6. Complete Execution Walkthrough

### 6.1 Prerequisites Checklist

- [ ] Python 3.8+ installed
- [ ] Virtual environment activated (`source litex-env/bin/activate`)
- [ ] LiteX framework initialized (`./litex_setup.py --init --install`)
- [ ] RISC-V toolchain installed (`sudo ./litex_setup.py --gcc=riscv`)
- [ ] System dependencies installed (`verilator`, `libevent-dev`, `libjson-c-dev`)
- [ ] Firmware compiled (`cd boot && make`)
- [ ] Server compiled (`cd server && make`)

### 6.2 Quick Start Commands

```bash
# From QTrino root directory

# 1. Activate virtual environment
source litex-env/bin/activate

# 2. (First time only) Generate SoC
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \
    --with-ethernet --integrated-main-ram-size=0x06400000 --no-compile-gateware

# 3. (First time only) Build bare-metal demo
litex_bare_metal_demo --build-path=build/sim

# 4. Build firmware
cd boot && make clean && make && cd ..

# 5. Build server
cd server && make clean && make && cd ..

# 6. Start server (Terminal 1)
cd server && ./server

# 7. Start simulation (Terminal 2)
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \
    --integrated-main-ram-size=0x06400000 --with-ethernet --ram-init=boot/boot.bin
```

### 6.3 Expected Client Output

Below is the actual output from a successful PQC-DTLS 1.3 client session, demonstrating full handshake, session resumption, and throughput testing:

```
===============================================================================
              PQC-DTLS 1.3 Client - RISC-V Bare-Metal
===============================================================================
[CONFIG] Algorithm:  ML-KEM-512 (Key Exchange) + ML-DSA-44 (Signatures)
[CONFIG] Protocol:   DTLS 1.3 (Pure Post-Quantum Cryptography)
[CONFIG] Cipher:     ChaCha20-Poly1305-SHA256 (preferred for software-only RISC-V)
[CONFIG] Auth:       Raw Public Key (RPK) Mutual Authentication (RFC 7250)
[CONFIG] CPU:        RISC-V VexRISCV @ ~1MHz (Bare-Metal)
===============================================================================

[INIT] Initializing network layer...
[OK] Network initialized (IP: 192.168.1.50, Server: 192.168.1.100:11111)

[MEMORY] Allocating static memory pool for PQC operations...
[OK] Static memory pool: 4194304 bytes (4 MB) allocated

[INIT] Initializing wolfSSL library...
[OK] wolfSSL library initialized

[DTLS] Creating DTLS 1.3 client context...
[OK] DTLS 1.3 client context created
[CIPHER] Setting preferred cipher suite: TLS13-CHACHA20-POLY1305-SHA256
[OK] ChaCha20-Poly1305-SHA256 set as preferred cipher suite

[RPK] Configuring Raw Public Key authentication (RFC 7250)...
[RPK] Loading client public key as RPK (1334 bytes)...
[OK] Client RPK (public key) loaded successfully
[RPK] Loading client private key (2626 bytes, ML-DSA-44)...
[OK] Client private key loaded successfully

[SESSION] Connection #1
[SESSION] First connection - will perform full handshake

[PERF] Starting handshake timer...
[PERF] Start cycles: 11221634

===============================================================================
                    STARTING DTLS 1.3 HANDSHAKE
===============================================================================
[INFO] This may take 60-90 seconds on 1MHz CPU with PQC operations

  [NETWORK] >>> Sending 938 bytes...
  [OK] Sent 938 bytes successfully
  [NETWORK] <<< Waiting to receive (timeout: 30s)...
  [OK] Received 144 bytes
  ...

[RPK] Verifying server's Raw Public Key...
[RPK] Received server public key: 1334 bytes
[RPK OK] Server public key matches pre-shared key
[RPK OK] Server identity verified successfully

===============================================================================
                   DTLS 1.3 HANDSHAKE COMPLETE!
===============================================================================
[SUCCESS] Secure channel established with server
[SESSION] Full handshake performed
[SUCCESS] Cipher suite: TLS13-CHACHA20-POLY1305-SHA256
[SUCCESS] Protocol version: DTLSv1.3
===============================================================================

[PERF] Handshake timing complete!
[PERF] End cycles: 43869680
[PERF] Total cycles: 32648046
[PERF] Latency: 32648 ms (32 seconds)

[DATA] Sending encrypted message to server...
[DATA] Message: "Hello from RISC-V PQC-DTLS client!" (34 bytes)
[OK] Message sent successfully (34 bytes encrypted + sent)

[DATA] Waiting for encrypted response from server...
[OK] Received encrypted data (27 bytes)
[DATA] Decrypted message: "Hello from PQC-DTLS server!"

[SESSION] Saving session for future resumption...
[SESSION] ✓ Session saved successfully

===============================================================================
                    THROUGHPUT PERFORMANCE TEST
===============================================================================
[INFO] Testing sustained data transfer rate (5 iterations)
[INFO] Packet size: 1024 bytes per iteration

[PERF] Throughput test complete!
[PERF] Successful iterations: 5/5
[PERF] Total bytes sent: 5120
[PERF] Time elapsed: 785 ms
[PERF] Throughput: 6522 bytes/sec
===============================================================================

[TEST] Connection #1 complete. Preparing for next connection...
[TEST] Waiting 3 seconds before next connection...

===============================================================================
           CONNECTION #2 - TESTING SESSION RESUMPTION
===============================================================================

[SESSION] Connection #2
[SESSION] Attempting to resume previous session...
[OK] Session configured for resumption

[PERF] Starting handshake timer...
[PERF] Start cycles: 53509202

===============================================================================
                    STARTING DTLS 1.3 HANDSHAKE
===============================================================================

  [NETWORK] >>> Sending 1145 bytes...
  [OK] Sent 1145 bytes successfully
  [NETWORK] <<< Waiting to receive (timeout: 30s)...
  [OK] Received 109 bytes
  ...

===============================================================================
                   DTLS 1.3 HANDSHAKE COMPLETE!
===============================================================================
[SUCCESS] Secure channel established with server
[SESSION] ✓ Session RESUMED successfully!
[PERF] Skipped expensive PQC key exchange (ML-KEM-512)
[PERF] Skipped signature generation/verification (ML-DSA-44)
[SUCCESS] Cipher suite: TLS13-CHACHA20-POLY1305-SHA256
[SUCCESS] Protocol version: DTLSv1.3
===============================================================================

[PERF] Handshake timing complete!
[PERF] End cycles: 62965235
[PERF] Total cycles: 9456033
[PERF] Latency: 9456 ms (9 seconds)

===============================================================================
           PERFORMANCE COMPARISON - EVALUATION CRITERIA
===============================================================================

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  METRIC              │  CONNECTION #1 (Full)  │  CONNECTION #2 (Resume)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Handshake Latency   │  32648 ms (32 sec)     │   9456 ms ( 9 sec)
  Cycles Consumed     │         32648046       │          9456033
  Throughput (TX)     │       6522 B/s         │        6522 B/s
  Bytes Sent          │             5120       │             5120
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

[ANALYSIS] Performance Insights:
   Session resumption is 3x faster
   Saved 23192 milliseconds by skipping PQC operations

[RESOURCES] Memory & ROM:
  • ROM Footprint: 457 KB (boot.elf)
  • Static Memory Pool: 4 MB
  • Peak RAM Usage: ~2-3 MB (PQC handshake)
===============================================================================

[CLEANUP] Cleaning up SSL resources...
[OK] SSL session freed
[INFO] Client terminated
```

### 6.4 Expected Server Output

Below is the actual output from a successful PQC-DTLS 1.3 server session handling both full handshake and session resumption:

```
===============================================================================
                      PQC-DTLS 1.3 Server
===============================================================================
[CONFIG] Algorithm:  ML-KEM-512 (Key Exchange) + ML-DSA-44 (Signatures)
[CONFIG] Protocol:   DTLS 1.3 (Pure Post-Quantum Cryptography)
[CONFIG] Cipher:     ChaCha20-Poly1305-SHA256 (preferred for software-only RISC-V)
[CONFIG] Auth:       Raw Public Key (RPK) Mutual Authentication (RFC 7250)
[CONFIG] Port:       11111 (UDP)
[CONFIG] Interface:  tap0
[CONFIG] Pacing:     2500ms (handshake), 0ms (data transfer)
===============================================================================

[INIT] Initializing wolfSSL library...
[OK] wolfSSL library initialized

[DTLS] Creating DTLS 1.3 server context...
[OK] DTLS 1.3 server context created
[CIPHER] Setting preferred cipher suite: TLS13-CHACHA20-POLY1305-SHA256
[OK] ChaCha20-Poly1305-SHA256 set as preferred cipher suite

[CONFIG] Enabling session tickets for resumption...
[OK] Session tickets enabled
[OK] Session ticket lifetime: 300 seconds (5 minutes)

[RPK] Configuring Raw Public Key authentication (RFC 7250)...
[RPK] Loading server public key as RPK (1334 bytes)...
[OK] Server RPK (public key) loaded successfully
[RPK] Loading server private key (2626 bytes, ML-DSA-44)...
[OK] Server private key loaded successfully

===============================================================================
                   CONNECTION #1/2
===============================================================================
[INFO] Expecting full handshake with session ticket issuance

[SSL] Creating SSL session object...
[OK] SSL session created
[SESSION] Configuring PSK resumption behavior...
[SESSION] ✓ HRR cookie exchange will be skipped on PSK resumption

[HANDSHAKE] Starting DTLS 1.3 handshake with dynamic timeouts...
  [NETWORK] <<< Waiting to receive (max 1900 bytes)...
  [OK] Received 938 bytes from client
  [NETWORK] >>> Sending 144 bytes to client...
  [OK] Sent 144 bytes successfully
  [PACING] Waiting 2500ms for client to process...
  ...

[RPK] Verifying client's Raw Public Key...
[RPK] Received client public key: 1334 bytes
[RPK OK] Client public key matches pre-shared key
[RPK OK] Client identity verified successfully

===============================================================================
                   DTLS 1.3 HANDSHAKE COMPLETE!
===============================================================================
[SUCCESS] Secure channel established with client
[SUCCESS] Client authenticated successfully
[SESSION] Full handshake performed (new session)
[INFO] Client can resume this session on next connection
[CONNECTION INFO]
  Cipher suite: TLS_CHACHA20_POLY1305_SHA256
  Protocol version: DTLSv1.3
===============================================================================

[DATA] Waiting for encrypted data from client...
[OK] Received encrypted data (34 bytes)
[DATA] Decrypted message: "Hello from RISC-V PQC-DTLS client!"

[DATA] Sending encrypted response to client...
[DATA] Message: "Hello from PQC-DTLS server!" (27 bytes)
[OK] Sent encrypted response (27 bytes)

===============================================================================
                    THROUGHPUT TEST (ONE-WAY RX)
===============================================================================
[INFO] Receiving 5 packets (1024 bytes each) from client

[PERF] Throughput test complete
[PERF] Successfully received: 5/5 packets
[PERF] Total bytes received: 5120 bytes
===============================================================================

[CONNECTION] Cleaning up connection #1 resources...
[OK] SSL session freed

[INFO] Waiting for next client connection...
[INFO] Ticket encryption keys preserved for session resumption

===============================================================================
                   CONNECTION #2/2
===============================================================================
[INFO] Expecting session resumption (if client presents valid ticket)

[HANDSHAKE] Starting DTLS 1.3 handshake with dynamic timeouts...
  [NETWORK] <<< Waiting to receive (max 1900 bytes)...
  [OK] Received 24 bytes from client
  [NETWORK] <<< Waiting to receive (max 1900 bytes)...
  [OK] Received 1145 bytes from client
  [NETWORK] >>> Sending 109 bytes to client...
  ...

===============================================================================
                   DTLS 1.3 HANDSHAKE COMPLETE!
===============================================================================
[SUCCESS] Secure channel established with client
[SUCCESS] Client authenticated successfully
[SESSION] ✓ Session RESUMED from client ticket
[PERF] Skipped expensive PQC key exchange and signatures
[CONNECTION INFO]
  Cipher suite: TLS_CHACHA20_POLY1305_SHA256
  Protocol version: DTLSv1.3
===============================================================================

[DATA] Decrypted message: "Hello from RISC-V PQC-DTLS client!"
[OK] Sent encrypted response (27 bytes)

[PERF] Throughput test complete
[PERF] Successfully received: 5/5 packets
[PERF] Total bytes received: 5120 bytes

===============================================================================
              ALL CONNECTIONS COMPLETE (2/2)
===============================================================================
[SUCCESS] Session resumption test complete
===============================================================================

[CLEANUP] Cleaning up final resources...
[OK] SSL context freed
[OK] wolfSSL library cleaned up
[INFO] Server terminated successfully
```

---

## 7. Configuration Reference

### 7.1 Performance Test Parameters

Edit in `boot/main.c` and `server/server.c`:

```c
#define THROUGHPUT_TEST_COUNT 5     // Number of test iterations
#define THROUGHPUT_PKT_SIZE 1024    // Packet size in bytes
#define NUM_TEST_CONNECTIONS 2      // Connections to test
```

### 7.2 Network Configuration

| Parameter | Client (boot/main.c) | Server (server/server.c) |
|-----------|---------------------|--------------------------|
| IP Address | 192.168.1.50 | 192.168.1.100 |
| Port | Random (ephemeral) | 11111 |
| Interface | Simulated Ethernet | tap0 |

### 7.3 Memory Configuration

| Parameter | Value | Location |
|-----------|-------|----------|
| Static Pool | 4 MB | `boot/main.c` |
| Stack Size | 500 KB | `boot/linker.ld` |
| Heap Size | 500 KB | `boot/linker.ld` |
| Main RAM | 100 MB | LiteX command line |

---

## 8. Session Resumption

DTLS 1.3 session resumption allows clients to reconnect without performing expensive PQC operations (ML-KEM-512 key exchange, ML-DSA-44 signature verification).

### 8.1 Overview

Session resumption uses **Pre-Shared Keys (PSK)** derived from a previous session. The server issues a **NewSessionTicket** after a successful handshake, which the client stores and presents in subsequent connections.

**Performance Impact on 1MHz RISC-V:**

| Metric | Full Handshake | Resumed Session | Improvement |
|--------|----------------|-----------------|-------------|
| Handshake Time | 32.6 seconds | 9.5 seconds | **3.5x faster** |
| CPU Cycles | 32,648,046 | 9,456,033 | **71% reduction** |
| PQC Operations | ML-KEM + ML-DSA | Skipped | **Eliminated** |

### 8.2 Handshake Flow

**Connection #1 (Full PQC Handshake):**
```
Client                                    Server
   |-------- ClientHello ------------------>|
   |<------- HelloRetryRequest + Cookie ----|
   |-------- ClientHello (retry) ---------->|
   |<------- ServerHello -------------------|
   |<------- Certificate (ML-DSA-44 RPK) ---|  ← PQC signature verification
   |<------- Finished ----------------------|
   |-------- Certificate + Finished ------->|
   |<------- NewSessionTicket --------------|  ← Ticket issued for resumption
```

**Connection #2 (Resumed):**
```
Client                                    Server
   |-------- ClientHello + PSK ------------>|  ← Includes session ticket
   |<------- ServerHello + Finished --------|
   |-------- Finished --------------------->|
   |<======= Secure Channel ===============>|  ← No PQC operations!
```

### 8.3 Key Configuration

**wolfSSL Build Flags Required:**
- `HAVE_SESSION_TICKET` - Enable session tickets
- `WOLFSSL_DTLS13_NO_HRR_ON_RESUME` - Skip HRR for resumed sessions
- Session cache must NOT be disabled

**Server Configuration:**
- Session ticket lifetime: 300 seconds (5 minutes)
- Handshake pacing: 2500ms between messages (for slow client)

### 8.4 Measured Results

| Metric | Connection #1 | Connection #2 | Notes |
|--------|---------------|---------------|-------|
| **Handshake Latency** | 32,648 ms | 9,456 ms | **3.5x improvement** |
| **Session Reused** | No | **Yes** | Ticket-based |
| **PQC Operations** | ML-KEM + ML-DSA | Skipped | PSK used |
| **Throughput** | 6,522 B/s | 6,522 B/s | Consistent |

**Key Insight:** 23.2 seconds saved per resumed connection by eliminating PQC cryptography overhead.

### 8.5 Security Considerations

| Aspect | Consideration |
|--------|---------------|
| **Forward Secrecy** | Partially maintained via PSK derived from original handshake |
| **Ticket Lifetime** | Limited to 5 minutes to reduce attack window |
| **PQC Protection** | Resumed sessions inherit trust from PQC-authenticated original session |

---

## 9. Troubleshooting

### 9.1 Build Errors

**Error: `variables.mak: No such file or directory`**

```
Solution: Run litex_sim and litex_bare_metal_demo first to generate build files
```

**Error: `riscv64-unknown-elf-gcc: command not found`**

```
Solution: Install RISC-V toolchain with: sudo ./litex_setup.py --gcc=riscv
```

### 9.2 Runtime Errors

**Handshake timeout after 60+ seconds**

```
This is EXPECTED behavior. PQC operations on 1MHz CPU take 60-90 seconds.
Wait patiently - DTLS automatically handles retransmissions.
```

**Server: "Address already in use"**

```
Solution: Kill any existing server process
$ pkill -f "./server"
```

### 9.3 Network Issues

**No response from client**

```
1. Ensure server started BEFORE simulation
2. Verify tap0 interface exists: ip addr show tap0
3. Check server is listening: netstat -uln | grep 11111
```

---

## References

- [LiteX Framework](https://github.com/enjoy-digital/litex)
- [wolfSSL Documentation](https://www.wolfssl.com/documentation/)
- [DTLS 1.3 RFC 9147](https://datatracker.ietf.org/doc/html/rfc9147)
- [ML-KEM (FIPS 203)](https://csrc.nist.gov/pubs/fips/203/final)
- [ML-DSA (FIPS 204)](https://csrc.nist.gov/pubs/fips/204/final)
- [RPK RFC 7250](https://datatracker.ietf.org/doc/html/rfc7250)

---

