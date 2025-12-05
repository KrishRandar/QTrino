# QTrino - Post-Quantum DTLS 1.3 on RISC-V

<div align="center">

**Quantum-Resistant Secure Communication for Embedded Systems**

[![RISC-V](https://img.shields.io/badge/CPU-RISC--V%20VexRISCV-blue)](https://github.com/enjoy-digital/litex)
[![DTLS 1.3](https://img.shields.io/badge/Protocol-DTLS%201.3-green)](https://datatracker.ietf.org/doc/html/rfc9147)
[![PQC](https://img.shields.io/badge/Crypto-Post--Quantum-purple)](https://csrc.nist.gov/projects/post-quantum-cryptography)
[![WolfSSL](https://img.shields.io/badge/Library-WolfSSL-red)](https://www.wolfssl.com/)

*Demonstrating quantum-resistant secure communication on resource-constrained bare-metal RISC-V systems*

</div>

---

## 📖 Overview

**QTrino** is a research-grade implementation of **DTLS 1.3 with NIST Post-Quantum Cryptography** on bare-metal RISC-V embedded systems. This project demonstrates that PQC-based secure communication is feasible even on severely constrained hardware.

### Key Features

- ✅ **Quantum-Resistant Security**: ML-KEM-512 (key exchange) + ML-DSA-44 (digital signatures)
- ✅ **Bare-Metal RISC-V Client**: No operating system, 1MHz CPU, 100MB RAM
- ✅ **DTLS 1.3 Protocol**: Latest datagram TLS specification with pure PQC cipher suites
- ✅ **Raw Public Key (RPK) Authentication**: RFC 7250 mutual authentication without X.509
- ✅ **Performance Instrumentation**: Real-time latency, throughput, and memory profiling
- ✅ **Production-Ready Server**: Pacing, session tickets, and robust error handling

### Performance Metrics

Measured on 1MHz RISC-V VexRISCV (LiteX simulator):

| Metric | Value |Notes |
|--------|-------|-------------|
| **Handshake Latency** | ~69 seconds | Full PQC handshake (ML-KEM + ML-DSA) |
| **Throughput** | ~238 bytes/sec | Sustained encrypted data transfer |
| **ROM Footprint** | 449 KB | Client firmware size |
| **RAM Usage** | ~2-3 MB peak | During PQC handshake |
| **Reliability** | 100% | All tests passed (50/50 iterations) |

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────┐
│    RISC-V Client (Bare-Metal @ 1MHz)     │
│  ┌────────────────────────────────────┐  │
│  │   boot/main.c                      │  │
│  │   - DTLS 1.3 Client                │  │
│  │   - wolfSSL/wolfCrypt              │  │
│  │   - ML-KEM-512 + ML-DSA-44         │  │
│  │   - 4MB static memory pool         │  │
│  │   - RISC-V cycle counter (rdcycle) │  │
│  └────────────────────────────────────┘  │
│         ↓ UDP (192.168.1.50)             │
└──────────────────────────────────────────┘
                    │
                    │ DTLS 1.3 + PQC
                    │ RPK Mutual Auth
                    ↓
┌──────────────────────────────────────────┐
│     Linux Server (x86_64)                │
│  ┌────────────────────────────────────┐  │
│  │   server/server.c                  │  │
│  │   - DTLS 1.3 Server                │  │
│  │   - Paced I/O for slow client      │  │
│  │   - Session ticket support         │  │
│  │   - UDP Port 11111                 │  │
│  └────────────────────────────────────┘  │
│         ↑ UDP (192.168.1.100)            │
└──────────────────────────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites

- **Ubuntu 20.04+** (or compatible Linux)
- **Python 3.8+** with LiteX installed
- **RISC-V toolchain** (`riscv64-unknown-elf-gcc`)
- **Git** for cloning the repository

### 1. Clone Repository

```bash
git clone https://github.com/KrishRandar/QTrino.git
cd QTrino
```

### 2. Build Client Firmware

```bash
cd boot
make clean
make

# Output: boot.bin (449 KB)
```

### 3. Build Server

```bash
cd ../server
make clean
make

# Output: server executable
```

### 4. Run Test

**Terminal 1 - Start Server:**
```bash
cd server
./server
```

**Terminal 2 - Start Client:**
```bash
cd QTrino
litex_sim --csr-json csr.json \
  --cpu-type=vexriscv \
  --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 \
  --with-ethernet \
  --ram-init=boot/boot.bin
```

### Expected Output

**Client:**
```
[PERF] Handshake timing complete!
[PERF] Total cycles: 68740583
[PERF] Latency: 68740 ms (68 seconds)

[PROGRESS] 10/50 iterations complete
[PROGRESS] 20/50 iterations complete
...
[PERF] Throughput: 238 bytes/sec

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  PERFORMANCE COMPARISON - EVALUATION CRITERIA
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Handshake Latency   │  68740 ms ( 68 sec)
  Throughput Test     │  102400 bytes/429 sec
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

**Server:**
```
[ECHO] 10/50 packets echoed
[ECHO] 20/50 packets echoed
...
[PERF] Successfully echoed: 50/50 packets
```

---

## 📊 Performance Measurement

The implementation includes comprehensive performance instrumentation:

### Configuration

Edit `boot/main.c` (lines 24-26):

```c
#define THROUGHPUT_TEST_COUNT 50    // Adjust iteration count
#define THROUGHPUT_PKT_SIZE 1024    // Adjust packet size
#define NUM_TEST_CONNECTIONS 2      // Number of test connections
```

### Metrics Captured

1. **Latency**: RISC-V `rdcycle` CSR for cycle-accurate timing
2. **Throughput**: Echo test with configurable iterations
3. **Memory**: Static pool usage and ROM footprint
4. **Reliability**: Success rate across sustained operations

### Output Control

**Clean output** (default - production mode):
- Verbose wolfSSL debugging disabled
- Only essential progress indicators shown
- Professional presentation for evaluation

**Verbose output** (debugging mode):
- Uncomment `wolfSSL_Debugging_ON()` at:
  - `boot/main.c`: lines ~429, ~551
  - `server/server.c`: line ~235
- Shows detailed protocol internals

---

## 🔧 Technical Details

### Cryptographic Algorithms

| Component | Algorithm | Size |
|-----------|-----------|------|
| **Key Exchange** | ML-KEM-512 | 800-byte public keys |
| **Signatures** | ML-DSA-44 | 1334-byte public keys |
| **Cipher Suite** | TLS_AES_128_GCM_SHA256 | AEAD |
| **Authentication** | Raw Public Keys (RPK) | No certificates |

### Resource Usage

**Client:**
- ROM: 449 KB (compiled firmware)
- RAM: 4 MB static pool (2-3 MB peak usage)
- Stack: 500 KB
- Heap: 500 KB

**Server:**
- Binary: 942 KB
- Dynamic memory allocation

### Limitations

1. **Session Resumption**: Currently not functional due to [known wolfSSL DTLS 1.3 HRR cookie issue](WOLFSSL_SUPPORT_INQUIRY.md)
2. **Performance**: Intentionally slow (1MHz CPU simulates IoT constraints)
3. **Single Connection**: Client tests 2 sequential connections, not concurrent

---

## 📁 Project Structure

```
QTrino/
├── boot/                      # RISC-V bare-metal client
│   ├── main.c                 # DTLS client implementation
│   ├── network.c/h            # UDP networking (LiteX Ethernet)
│   ├── performance.c/h        # Performance measurement utilities
│   ├── Makefile               # Build system
│   ├── wolfssl/               # wolfSSL library (v5.7.2)
│   └── wolfcrypt/             # wolfCrypt cryptographic primitives
├── server/                    # Linux DTLS server
│   ├── server.c               # DTLS server with pacing & echo
│   ├── user_settings.h        # wolfSSL configuration
│   └── Makefile               # Server build script
├── certs/                     # Raw Public Keys (RPK)
│   ├── client_rpk.h           # Client ML-DSA-44 key pair
│   └── server_rpk.h           # Server ML-DSA-44 key pair
└── docs/                      # Documentation
    ├── PRODUCTION_READY_STATUS.md
    ├── WOLFSSL_SUPPORT_INQUIRY.md
    └── README.md (this file)
```

---

## 🧪 Testing & Validation

### Build Verification

```bash
# Clean build
cd boot && make clean && make
cd ../server && make clean && make

# Should complete without errors
```

### Functional Tests

1. **Handshake**: Both connections complete successfully
2. **Authentication**: RPK mutual authentication verified
3. **Data Transfer**: Bidirectional encrypted communication
4. **Throughput**: 50/50 iterations successful
5. **Cleanup**: Graceful shutdown without memory leaks

### Performance Validation

- Latency measurements consistent (~±5%)
- Throughput stable across iterations
- No packet loss or retransmissions
- Memory usage within expected bounds

---

## 🐛 Troubleshooting

### Client Won't Connect

```bash
# Check server is listening
netstat -ulnp | grep 11111

# Verify client can reach server
ping 192.168.1.100
```

### Build Errors

```bash
# Missing RISC-V toolchain
sudo apt-get install gcc-riscv64-unknown-elf

# Missing LiteX
pip3 install litex

# Clean rebuild
make clean && make
```

### Performance Issues

- **Slow handshake**: Expected! 1MHz CPU takes ~70 seconds for PQC
- **Timeout errors**: Increase `DTLS_TIMEOUT` in user_settings.h
- **Packet drops**: Server pacing may need adjustment (SEND_PACING_MS)

---

## 📚 Documentation

- **[Production Status](PRODUCTION_READY_STATUS.md)**: Security audit and feature status
- **[wolfSSL Inquiry](WOLFSSL_SUPPORT_INQUIRY.md)**: Session resumption investigation
- **[Quick Test Guide](docs/quick_test_guide.md)**: Detailed testing instructions
- **[Performance Summary](docs/FINAL_PERFORMANCE_SUMMARY.md)**: Complete metrics

---

## 🤝 Contributing

This is an academic research project. Contributions are welcome for:

- Performance optimizations
- Additional PQC algorithm support
- Documentation improvements
- Bug fixes

Please open an issue before starting major work.

---

## 📄 License

This project uses wolfSSL library. See individual file headers for licensing details.

---

## 🏆 Acknowledgments

- **wolfSSL Team**: For PQC support and DTLS 1.3 implementation
- **LiteX Project**: For RISC-V SoC framework
- **NIST**: For post-quantum cryptography standardization

---

## 📧 Contact

For questions or issues:
- **GitHub Issues**: [QTrino Issues](https://github.com/KrishRandar/QTrino/issues)
- **Project Author**: [KrishRandar](https://github.com/KrishRandar)

---

<div align="center">

**Built with ❤️ for quantum-resistant security research**

*"Securing tomorrow's communications on today's constrained hardware"*

</div>
