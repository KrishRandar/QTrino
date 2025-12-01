# DTLS 1.3 Post-Quantum Handshake Capture Guide

This guide provides step-by-step instructions to build, run, and capture a complete DTLS 1.3 handshake between a RISC-V bare-metal client and a Linux server using Post-Quantum Cryptography (PQC) algorithms.

## Overview

- **Client**: RISC-V VexRiscv CPU running bare-metal firmware (no OS)
- **Server**: Linux host running DTLS 1.3 server
- **PQC Algorithms**: ML-KEM-512 (key exchange), ML-DSA-44 (signatures)
- **Protocol**: DTLS 1.3 over UDP
- **Network**: TAP interface (`tap0`) for simulation

## Prerequisites

### System Requirements
- **OS**: Linux (Ubuntu 20.04+ recommended)
- **Python**: 3.8 or higher
- **RAM**: At least 4GB available
- **Disk**: ~2GB free space

### Required Software
```bash
# Install system dependencies
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    python3 \
    python3-venv \
    python3-pip \
    verilator \
    libevent-dev \
    libjson-c-dev \
    wireshark \
    tshark
```

### RISC-V Toolchain
The RISC-V GCC toolchain will be installed automatically by the LiteX setup script.

## Step 1: Clone and Setup Environment

### 1.1 Clone Repository
```bash
git clone https://github.com/QTrino-Labs-Pvt-Ltd/Constraint_Env_Sim.git
cd Constraint_Env_Sim
```

### 1.2 Create Python Virtual Environment
```bash
python3 -m venv litex-env
source litex-env/bin/activate
```

### 1.3 Install LiteX and Dependencies
```bash
# Make setup script executable
chmod +x litex_setup.py

# Initialize LiteX environment
./litex_setup.py --init --install

# Install additional Python dependencies
pip3 install meson ninja
```

### 1.4 Install RISC-V Toolchain
```bash
# Install RISC-V GCC toolchain (requires sudo)
sudo ./litex_setup.py --gcc=riscv
```

**Note**: This installs the toolchain system-wide. The installation may take 10-15 minutes.

## Step 2: Build Firmware

### 2.1 Navigate to Boot Directory
```bash
cd boot
```

### 2.2 Build Firmware
```bash
# Clean previous build (optional)
make clean

# Build firmware
make
```

**Expected Output:**
```
[CC] main.c
[CC] network.c
[CC] crt0.S
[LD] boot.elf
[OBJCOPY] boot.bin
```

**Output Files:**
- `boot/boot.bin` - Binary firmware image
- `boot/boot.elf` - ELF file with debug symbols

### 2.3 Verify Build
```bash
# Check if boot.bin exists
ls -lh boot.bin

# Check file size (should be reasonable, e.g., < 1MB)
file boot.bin
```

## Step 3: Build Server

### 3.1 Navigate to Server Directory
```bash
cd ../server
```

### 3.2 Build Server
```bash
# Clean previous build (optional)
make clean

# Build server
make
```

**Expected Output:**
```
[CC] server.c
[CC] wolfssl/src/*.c
[LD] server
```

**Output File:**
- `server/server` - Executable DTLS server

### 3.3 Verify Build
```bash
# Check if server executable exists
ls -lh server

# Test server help (if available)
./server --help || echo "Server built successfully"
```

## Step 4: Prepare Network Capture

### 4.1 Start Wireshark (Optional but Recommended)
Open Wireshark in a separate terminal or GUI:

```bash
# Start Wireshark (GUI)
wireshark &

# OR use tshark (command-line)
# tshark -i tap0 -w dtls_handshake.pcapng
```

**Wireshark Configuration:**
1. Select interface: `tap0` (will appear when simulation starts)
2. Filter: `udp.port == 11111 || udp.port == 12345`
3. Start capture before running simulation

### 4.2 Alternative: Command-Line Capture
```bash
# Capture to file (run in separate terminal)
sudo tshark -i tap0 -w dtls_handshake.pcapng -f "udp port 11111 or udp port 12345"
```

## Step 5: Run Complete Handshake

### 5.1 Terminal Setup
You'll need **3 terminals**:

- **Terminal 1**: LiteX simulation (client)
- **Terminal 2**: Network setup + Server
- **Terminal 3**: Wireshark (optional)

### 5.2 Start LiteX Simulation (Terminal 1)

```bash
# Ensure you're in project root
cd /path/to/Constraint_Env_Sim

# Activate virtual environment
source litex-env/bin/activate

# Start simulation with Ethernet support
litex_sim --csr-json csr.json \
    --cpu-type=vexriscv \
    --cpu-variant=full \
    --integrated-main-ram-size=0x06400000 \
    --with-ethernet \
    --ram-init=boot/boot.bin
```

**Expected Output:**
```
INFO:SoC:RAM main_ram added Origin: 0x40000000, Size: 0x06400000
INFO:SoCIRQHandler:ethmac IRQ allocated at Location 2
...
[NET] Network initialized
[NET] Local IP: 192.168.1.50:12345
[NET] Server: 192.168.1.100:11111
[DTLS] Initializing DTLS context...
```

**Keep this terminal open** - the simulation must remain running.

### 5.3 Setup Network (Terminal 2)

**Wait 2-3 seconds** for `tap0` interface to be created, then:

```bash
# Navigate to project root
cd /path/to/Constraint_Env_Sim

# Run network setup script
./server/setup_network.sh
```

**Expected Output:**
```
==========================================
Network Setup for DTLS Server
==========================================
Client IP:  192.168.1.50
Client MAC: 10:e2:d5:00:00:00
Interface:  tap0
==========================================

[NETWORK] Waiting for tap0 interface to appear...
[OK] Interface tap0 found
[NETWORK] Adding static ARP entry...
[OK] Static ARP entry added
[NETWORK] Configuring route...
[OK] Route added
```

**If script fails:**
- Ensure `litex_sim` is running with `--with-ethernet`
- Check interface: `ip link show tap0`
- Manually bring up: `sudo ip link set tap0 up`

### 5.4 Start DTLS Server (Terminal 2, same terminal)

```bash
# Start server (in same terminal as network setup)
./server/server
```

**Expected Output:**
```
[NETWORK] Binding socket to tap0 interface...
[OK] Socket bound to tap0
[NETWORK] Verifying network configuration...
[NETWORK] Client IP: 192.168.1.50
[NETWORK] Expected MAC: 10:e2:d5:00:00:00
[DTLS] Server listening on 0.0.0.0:11111
[DTLS] Waiting for client connection...
```

### 5.5 Observe Handshake

**Client Console (Terminal 1)** should show:
```
[DTLS] Connecting to server...
[IO] Sending 1005 bytes
[NET] RX Callback: Src=c0a80164:11111 Dst=12345 Len=847
[IO] Received 847 bytes
[DTLS] Server Hello received
[DTLS] Handshake progressing...
```

**Server Console (Terminal 2)** should show:
```
[DTLS] Client Hello received (1005 bytes)
[DTLS] Sending Server Hello...
[DTLS] Sending Certificate...
[DTLS] Handshake complete!
```

**Wireshark** should show:
- Frame 1: Client Hello (UDP 12345 → 11111)
- Frame 2: HelloRetryRequest (UDP 11111 → 12345)
- Frame 3: Client Hello (retry)
- Frame 4: Server Hello
- Frame 5: Certificate
- ... (complete handshake)

## Step 6: Analyze Handshake Capture

### 6.1 Open Capture in Wireshark

```bash
# If captured to file
wireshark dtls_handshake.pcapng
```

### 6.2 Apply Filters

**DTLS Filter:**
```
dtls
```

**Client Hello:**
```
dtls.handshake.type == 1
```

**Server Hello:**
```
dtls.handshake.type == 2
```

**PQC Key Share:**
```
dtls.handshake.extension.type == 51 && dtls.handshake.extension.len > 800
```

### 6.3 Verify PQC Algorithms

**In Client Hello, check:**

1. **Key Share Extension:**
   - Expand: `DTLSv1.3 Record Layer` → `Handshake Protocol: Client Hello` → `Extension: key_share`
   - Should show: `MLKEM512` or `ML-KEM-512`

2. **Signature Algorithms Extension:**
   - Expand: `Extension: signature_algorithms`
   - Should show: `dilithium_level2_sa_algo` (0x0e), `dilithium_level3_sa_algo` (0x0f), `dilithium_level5_sa_algo` (0x10)
   - Should **NOT** show: RSA (0x01), ECDSA (0x03)

3. **Supported Groups:**
   - Expand: `Extension: supported_groups`
   - Should include: `ML-KEM-512` (or similar PQC group)

### 6.4 Verify Handshake Completion

**Look for:**
- `Handshake Protocol: Finished` messages
- Encrypted Application Data (if any)
- No `Alert` messages indicating errors

## Troubleshooting

### Issue: "Interface tap0 does not exist"

**Solution:**
- Ensure `litex_sim` is running with `--with-ethernet` flag
- Wait 2-3 seconds after starting simulation
- Check: `ip link show tap0`

### Issue: "Failed to add ARP entry"

**Solution:**
```bash
# Bring interface up manually
sudo ip link set tap0 up

# Remove existing entry
sudo ip neigh del 192.168.1.50 dev tap0

# Re-run setup script
./server/setup_network.sh
```

### Issue: Client doesn't receive HelloRetryRequest

**Symptoms:**
- Client sends Client Hello but times out
- Wireshark shows HelloRetryRequest with wrong MAC address

**Solution:**
1. Verify ARP entry: `ip neigh show dev tap0`
2. Verify route: `ip route get 192.168.1.50`
3. Check Wireshark: HelloRetryRequest destination MAC should be `10:e2:d5:00:00:00`
4. Re-run network setup: `./server/setup_network.sh`

### Issue: "No cipher suites" error

**Solution:**
- Rebuild firmware: `cd boot && make clean && make`
- Check `boot/wolfssl/wolfcrypt/user_settings.h` has PQC enabled
- Verify `WOLFSSL_MLKEM512` and `HAVE_DILITHIUM` are defined

### Issue: Handshake fails with "unsupported algorithm"

**Solution:**
- Ensure server and client use same PQC algorithms
- Check server `user_settings.h` matches client configuration
- Rebuild both: `cd boot && make clean && make` and `cd server && make clean && make`

### Issue: Wireshark doesn't show DTLS packets

**Solution:**
- Ensure capturing on `tap0` interface (not `eth0` or `lo`)
- Check filter: `udp.port == 11111 || udp.port == 12345`
- Verify simulation is running and server is listening
- Try: `sudo tshark -i tap0 -f "udp"`

### Issue: Build fails with "cannot find -lwolfssl"

**Solution:**
- Server uses static linking, this error shouldn't occur
- If it does: `cd server && make clean && make`

### Issue: Simulation crashes or hangs

**Solution:**
- Check available RAM: `free -h` (need ~4GB free)
- Reduce RAM size: `--integrated-main-ram-size=0x02000000`
- Check Verilator version: `verilator --version` (should be 4.0+)

## Quick Reference

### Build Commands
```bash
# Firmware
cd boot && make clean && make

# Server
cd server && make clean && make
```

### Run Commands
```bash
# Terminal 1: Simulation
source litex-env/bin/activate
litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \
  --integrated-main-ram-size=0x06400000 --with-ethernet --ram-init=boot/boot.bin

# Terminal 2: Network + Server
./server/setup_network.sh
./server/server

# Terminal 3: Wireshark (optional)
wireshark -i tap0
```

### Network Verification
```bash
# Check ARP entry
ip neigh show dev tap0

# Check route
ip route get 192.168.1.50

# Check interface
ip link show tap0
```

### Cleanup
```bash
# Remove ARP entry
sudo ip neigh del 192.168.1.50 dev tap0

# Remove route
sudo ip route del 192.168.1.50/32 dev tap0
```

## Expected Handshake Flow

1. **Client Hello** (Frame 1)
   - Client → Server
   - Contains: ML-KEM-512 key share, Dilithium signature algorithms
   - UDP: 12345 → 11111

2. **HelloRetryRequest** (Frame 2, optional)
   - Server → Client
   - Requests cookie or different key share
   - UDP: 11111 → 12345

3. **Client Hello (Retry)** (Frame 3, if HRR sent)
   - Client → Server
   - Includes cookie from HRR
   - UDP: 12345 → 11111

4. **Server Hello** (Frame 4)
   - Server → Client
   - Contains: Server's ML-KEM-512 key share
   - UDP: 11111 → 12345

5. **Encrypted Extensions** (Frame 5)
   - Server → Client
   - UDP: 11111 → 12345

6. **Certificate** (Frame 6)
   - Server → Client
   - ML-DSA-44 signed certificate
   - UDP: 11111 → 12345

7. **Certificate Verify** (Frame 7)
   - Server → Client
   - ML-DSA-44 signature
   - UDP: 11111 → 12345

8. **Finished** (Frames 8-9)
   - Both sides send Finished messages
   - Handshake complete

## Additional Resources

- **Network Setup Details**: See `NETWORK_SETUP.md`
- **Build Instructions**: See `README.md`
- **Problem Statement**: See `ps.txt`

## Support

For issues or questions:
1. Check troubleshooting section above
2. Review `NETWORK_SETUP.md` for network-specific issues
3. Verify all prerequisites are installed
4. Check that both client and server are rebuilt after configuration changes

