/*
 * network.c - UDP networking for DTLS over LiteEth
 * 
 * This file provides UDP networking abstraction for DTLS communication
 * using LiteX's libliteeth library for Ethernet MAC/PHY access.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "network.h"

#include <generated/csr.h>

// Only compile if Ethernet is present in SoC
#ifdef CSR_ETHMAC_BASE
#include <libliteeth/udp.h>
#include <libliteeth/inet.h>

//=============================================================================
// CONFIGURATION
//=============================================================================
#define LOCAL_IP    IPTOINT(192, 168, 1, 50)    // RISC-V client IP
#define LOCAL_PORT  12345                        // Our UDP port
#define SERVER_IP   IPTOINT(192, 168, 1, 100)   // DTLS server IP  
#define SERVER_PORT 11111                        // DTLS server port

// MAC address (must match SoC configuration)
static uint8_t my_mac[6] = {0x10, 0xe2, 0xd5, 0x00, 0x00, 0x00};

// Receive buffer for incoming packets
static uint8_t rx_buffer[2048];
static int rx_len = 0;
static int rx_ready = 0;

//=============================================================================
// UDP CALLBACK - Called by libliteeth when packet arrives
//=============================================================================
static void udp_rx_callback(uint32_t src_ip, uint16_t src_port,
                            uint16_t dst_port, void *data, uint32_t length) {
    // Debug: Print every packet received (including ARP, ICMP, etc.)
    printf("[NET] RX Callback: Src=%08x:%d Dst=%d Len=%d\n", 
           (unsigned int)src_ip, (int)src_port, (int)dst_port, (int)length);

    // Check if this is for our DTLS port
    if (dst_port != LOCAL_PORT) {
        printf("[NET] Ignored packet for port %d (expected %d)\n", (int)dst_port, LOCAL_PORT);
        return;  // Ignore packets for other ports
    }
    
    // Copy to receive buffer (if not already full)
    if (!rx_ready && length <= sizeof(rx_buffer)) {
        memcpy(rx_buffer, data, length);
        rx_len = length;
        rx_ready = 1;
        printf("[NET] Packet buffered (%d bytes)\n", (int)length);
    } else {
        printf("[NET] Buffer full or packet too large (rx_ready=%d, length=%d, max=%d)\n", 
               rx_ready, (int)length, (int)sizeof(rx_buffer));
    }
}

//=============================================================================
// INITIALIZATION
//=============================================================================
void network_init(void) {
    // Set our MAC and IP addresses
    udp_set_mac(my_mac);
    udp_set_ip(LOCAL_IP);
    
    // Start UDP stack (initializes Ethernet MAC)
    // NOTE: udp_start() clears the callback, so we must set it AFTER
    udp_start(my_mac, LOCAL_IP);
    
    // Register callback for incoming packets (AFTER udp_start!)
    udp_set_callback(udp_rx_callback);
    
    printf("[NET] Network initialized\n");
    printf("[NET] Local IP: 192.168.1.50:%d\n", LOCAL_PORT);
    printf("[NET] Server: 192.168.1.100:%d\n", SERVER_PORT);
    printf("[NET] MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
           my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
    
    // Verify IP is set correctly
    extern uint32_t udp_get_ip(void);
    uint32_t current_ip = udp_get_ip();
    printf("[NET] Configured IP: 0x%08x (expected: 0x%08x)\n", 
           (unsigned int)current_ip, (unsigned int)LOCAL_IP);
    if (current_ip != LOCAL_IP) {
        printf("[NET] WARNING: IP mismatch! This will cause packets to be dropped!\n");
    }
}

//=============================================================================
// SEND UDP PACKET
//=============================================================================
int network_send(const uint8_t* data, int len) {
    // Resolve server IP to MAC address (ARP)
    if (!udp_arp_resolve(SERVER_IP)) {
        printf("[NET] ARP resolution failed\n");
        return -1;
    }
    
    // Get transmit buffer from libliteeth
    void* tx_buf = udp_get_tx_buffer();
    
    // Copy our data to transmit buffer
    memcpy(tx_buf, data, len);
    
    // Send the packet
    int ret = udp_send(LOCAL_PORT, SERVER_PORT, len);
    if (ret <= 0) {
        printf("[NET] UDP send failed\n");
        return -1;
    }
    
    return len;
}

//=============================================================================
// RECEIVE UDP PACKET
//=============================================================================
int network_recv(uint8_t* buffer, int max_len, int timeout_ms) {
    // Simple timeout implementation using busy loop
    // In production, use timer interrupt
    volatile int timeout_counter = timeout_ms * 1000;  // Approximate
    static int call_count = 0;
    static int last_print = 0;
    static int udp_service_count = 0;
    static int hardware_event_count = 0;
    
    call_count++;
    
    // Print diagnostic every ~1000 calls (roughly every second at 1MHz)
    if (call_count - last_print > 1000) {
        printf("[NET] network_recv: still waiting (call #%d, udp_service=%d, hw_events=%d)\n", 
               call_count, udp_service_count, hardware_event_count);
        last_print = call_count;
    }
    
    while (timeout_counter > 0) {
        // Service the UDP stack (processes incoming packets)
        // This checks if hardware has written a packet to SRAM
        // Check hardware event register BEFORE calling udp_service
        #ifdef CSR_ETHMAC_BASE
        uint32_t hw_event = ethmac_sram_writer_ev_pending_read();
        if (hw_event & 0x1) {  // ETHMAC_EV_SRAM_WRITER
            hardware_event_count++;
            if (hardware_event_count == 1) {
                printf("[NET] Hardware event detected! Packet in SRAM\n");
                printf("[NET] Calling udp_service() to process packet...\n");
            }
        }
        #endif
        
        udp_service();
        udp_service_count++;
        
        // After udp_service, check if callback should have been called
        if (hardware_event_count == 1 && !rx_ready && udp_service_count == 1) {
            printf("[NET] WARNING: Packet processed but callback not triggered!\n");
            printf("[NET] This suggests packet was filtered by IP/UDP checks\n");
            
            // Diagnostic: Check configured IP vs expected
            extern uint32_t udp_get_ip(void);
            uint32_t configured_ip = udp_get_ip();
            printf("[NET] DIAG: Configured IP in libliteeth: 0x%08x\n", (unsigned int)configured_ip);
            printf("[NET] DIAG: Expected IP (LOCAL_IP): 0x%08x\n", (unsigned int)LOCAL_IP);
            if (configured_ip != LOCAL_IP) {
                printf("[NET] ERROR: IP mismatch! This will cause packets to be dropped!\n");
                printf("[NET] Fix: Ensure udp_set_ip() is called with correct value\n");
            } else {
                printf("[NET] DIAG: IP matches - issue may be in packet IP or other checks\n");
            }
        }
        
        // Check if we have data
        if (rx_ready) {
            int copy_len = (rx_len < max_len) ? rx_len : max_len;
            memcpy(buffer, rx_buffer, copy_len);
            rx_ready = 0;
            rx_len = 0;
            return copy_len;
        }
        
        timeout_counter--;
    }
    
    printf("[NET] network_recv: timeout after %d calls\n", call_count);
    printf("[NET]   udp_service called: %d times\n", udp_service_count);
    printf("[NET]   hardware events detected: %d\n", hardware_event_count);
    if (hardware_event_count == 0) {
        printf("[NET]   WARNING: No hardware events - packets not reaching hardware MAC!\n");
    }
    return 0; // Timeout
}

#else
// Stub implementations if Ethernet is not present
void network_init(void) {
    printf("[ERROR] Ethernet not configured in SoC\n");
}

int network_send(const uint8_t* data, int len) {
    (void)data;
    (void)len;
    return -1;
}

int network_recv(uint8_t* buffer, int max_len, int timeout_ms) {
    (void)buffer;
    (void)max_len;
    (void)timeout_ms;
    return -1;
}
#endif // CSR_ETHMAC_BASE
