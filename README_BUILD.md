# Build Instructions - PQC-DTLS 1.3 on RISC-V

This document provides step-by-step instructions to build and run the PQC-DTLS 1.3 implementation.

## Prerequisites

Ensure you have the following installed:
- RISC-V GCC toolchain (`riscv64-unknown-elf-gcc`)
- Python 3.8+
- LiteX framework
- Build tools (make, gcc)

## Build Steps

### Step 2: Build Client Firmware

Navigate to the `boot` directory and compile the firmware:

```bash
cd boot
make clean
make
```

This will:
- Compile `main.c`, `network.c`, and wolfSSL sources
- Link with RISC-V bare-metal libraries
- Generate `boot.bin` firmware image

**Output:** `boot/boot.bin` (~436KB)

### Step 3: Build Server Application

Navigate to the `server` directory and compile the server:

```bash
cd server
make clean
make
```

This will:
- Compile `server.c` with wolfSSL
- Link against system libraries
- Generate `server` executable

**Output:** `server/server` (Linux executable)

## Running the Demo

### Terminal 1: Start Client Simulation

```bash
litex_sim --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 \
  --with-ethernet \
  --ram-init=boot/boot.bin
```

**Wait 2-3 seconds** for the simulation to initialize.

### Terminal 2: Start Server

```bash
cd server
./server
```

The server will listen on port 11111 and wait for the DTLS handshake.

## Expected Output

### Client Console:
```
PQC-DTLS 1.3 Client - RISC-V Bare-Metal
Algorithm: ML-KEM-512 + ML-DSA-44
Protocol:  DTLS 1.3 (Pure PQC)
Auth:      X.509 Mutual Authentication

STARTING DTLS 1.3 HANDSHAKE
DTLS 1.3 HANDSHAKE COMPLETE!
DATA: Sending message: "Hello from RISC-V PQC-DTLS client!"
PQC-DTLS 1.3 DEMO COMPLETE - SUCCESS!
```

### Server Console:
```
[WAITING] For DTLS handshake from client...
[SUCCESS] Handshake complete!
[DATA] Received: "Hello from RISC-V PQC-DTLS client!"
```

