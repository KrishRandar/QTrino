# Network Setup Guide for DTLS Server

## Problem

The RISC-V bare-metal client uses hardware MAC filtering in `libliteeth` that only accepts Ethernet frames addressed to its specific MAC address (`10:e2:d5:00:00:00`). When the Linux server sends a HelloRetryRequest response, it may not have the client's MAC address in its ARP table yet, causing the packet to be:

- Sent to broadcast MAC (rejected by client hardware filter)
- Routed via default gateway (wrong destination)
- Dropped silently

This results in the client never receiving the HelloRetryRequest, causing the DTLS handshake to fail.

## Solution

Configure the Linux host with a **static ARP entry** and **explicit route** before starting the server. This ensures the server knows the client's MAC address immediately and routes packets correctly.

## Quick Start

**Option 1: Run setup script first (recommended)**
The setup script will wait for `tap0` to appear, so you can run it before or after starting the simulation:

1. **Start the LiteX simulation** (this creates the `tap0` interface):
   ```bash
   litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \
     --integrated-main-ram-size=0x06400000 --with-ethernet --ram-init=boot/boot.bin
   ```

2. **Run the network setup script** (in a separate terminal - it will wait for tap0):
   ```bash
   cd /path/to/Constraint_Env_Sim
   ./server/setup_network.sh
   ```

3. **Start the DTLS server**:
   ```bash
   ./server/server
   ```

**Option 2: Run setup script after simulation starts**
You can also start the simulation first, then run the setup script - it will detect the interface immediately.

## Manual Configuration

If you prefer to configure manually or the script fails:

### 1. Add Static ARP Entry

```bash
sudo ip neigh add 192.168.1.50 lladdr 10:e2:d5:00:00:00 dev tap0
```

### 2. Add Route (if not already present)

```bash
sudo ip route add 192.168.1.50/32 dev tap0
```

### 3. Verify Configuration

```bash
# Check ARP entry
ip neigh show dev tap0

# Check route
ip route get 192.168.1.50
```

Expected output:
```
192.168.1.50 dev tap0 lladdr 10:e2:d5:00:00:00 REACHABLE
192.168.1.50 dev tap0 src 192.168.1.100 uid 0
```

## Network Configuration Details

### Client Configuration
- **IP Address:** 192.168.1.50
- **MAC Address:** 10:e2:d5:00:00:00
- **UDP Port:** 12345
- **Interface:** LiteEth (hardware MAC filtering enabled)

### Server Configuration
- **IP Address:** 192.168.1.100
- **Interface:** tap0 (Tun/Tap)
- **UDP Port:** 11111

### Why Static ARP is Required

1. **Hardware MAC Filtering:** The client's LiteEth MAC hardware only accepts frames with destination MAC matching `10:e2:d5:00:00:00`. Broadcast frames for unicast IPs are rejected.

2. **Timing Issue:** The server sends HelloRetryRequest immediately after receiving ClientHello, but the client's ARP reply may arrive 5+ seconds later (due to slow 1MHz CPU). Without static ARP, the server doesn't know the client's MAC.

3. **Routing:** Without an explicit route, Linux may route packets via the default gateway instead of `tap0`.

## Troubleshooting

### Issue: "Interface tap0 does not exist" or "Interface tap0 did not appear after 30 seconds"

**Solution:** 
- The setup script waits up to 30 seconds for `tap0` to appear
- If it times out, ensure `litex_sim` is running with `--with-ethernet` flag
- Check if `tap0` exists: `ip link show tap0`
- If `litex_sim` is running but `tap0` doesn't appear, check simulation logs for errors

### Issue: "Failed to add ARP entry"

**Possible causes:**
- Interface is down: `sudo ip link set tap0 up`
- Permission denied: Run script with `sudo` or ensure user has CAP_NET_ADMIN
- Entry already exists: The script removes existing entries, but manual cleanup may be needed:
  ```bash
  sudo ip neigh del 192.168.1.50 dev tap0
  ```

### Issue: Client still doesn't receive packets

**Debugging steps:**

1. **Verify ARP entry exists:**
   ```bash
   ip neigh show dev tap0 | grep 192.168.1.50
   ```

2. **Verify route:**
   ```bash
   ip route get 192.168.1.50
   ```
   Should show: `192.168.1.50 dev tap0`

3. **Check Wireshark:**
   - Capture on `tap0` interface
   - Verify HelloRetryRequest has destination MAC `10:e2:d5:00:00:00`
   - Check if packet is sent to broadcast or wrong MAC

4. **Check client logs:**
   - Look for `[NET] RX Callback` messages
   - If missing, packet is dropped by hardware MAC filter

5. **Verify interface is UP:**
   ```bash
   ip link show tap0
   ```
   Should show `state UP`

### Issue: Route conflicts with existing routes

If you have a route to `192.168.1.0/24` via another interface, the more specific `/32` route should take precedence. If not, remove conflicting routes:

```bash
# List all routes
ip route show

# Remove conflicting route (if needed)
sudo ip route del 192.168.1.0/24 via <gateway>
```

## Testing

After configuration:

1. **Start simulation:**
   ```bash
   litex_sim --csr-json csr.json --cpu-type=vexriscv --cpu-variant=full \
     --integrated-main-ram-size=0x06400000 --with-ethernet --ram-init=boot/boot.bin
   ```

2. **Run setup script:**
   ```bash
   ./server/setup_network.sh
   ```

3. **Start server:**
   ```bash
   ./server/server
   ```

4. **Verify client receives HelloRetryRequest:**
   - Client console should show: `[NET] RX Callback: Src=c0a80164:11111 Dst=12345 Len=144`
   - Wireshark should show HelloRetryRequest with correct destination MAC

## Cleanup

To remove the static ARP entry and route:

```bash
sudo ip neigh del 192.168.1.50 dev tap0
sudo ip route del 192.168.1.50/32 dev tap0
```

## Additional Notes

- The static ARP entry persists until the interface is removed or the system reboots
- The route persists until removed or the interface is removed
- Both are automatically cleaned up when `litex_sim` exits (if it removes the `tap0` interface)
- The server code now attempts to bind to `tap0` using `SO_BINDTODEVICE`, but this is optional - the static ARP entry is the critical fix

