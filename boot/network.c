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
    // Check if this is for our DTLS port
    if (dst_port != LOCAL_PORT) {
        return;  // Ignore packets for other ports
    }
    
    // Copy to receive buffer (if not already full)
    if (!rx_ready && length <= sizeof(rx_buffer)) {
        memcpy(rx_buffer, data, length);
        rx_len = length;
        rx_ready = 1;
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
    
    while (timeout_counter > 0) {
        // Service the UDP stack (processes incoming packets)
        udp_service();
        
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
