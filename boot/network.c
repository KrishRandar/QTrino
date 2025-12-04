// UDP networking abstraction for DTLS communication using LiteX's libliteeth 
// library for Ethernet MAC/PHY access.
 

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "network.h"

#include <generated/csr.h>

#ifdef CSR_ETHMAC_BASE
#include <libliteeth/udp.h>
#include <libliteeth/inet.h>

// CONFIGURATION
#define LOCAL_IP    IPTOINT(192, 168, 1, 50)    // RISC-V client IP
#define LOCAL_PORT  12345                        // Our UDP port
#define SERVER_IP   IPTOINT(192, 168, 1, 100)   // DTLS server IP  
#define SERVER_PORT 11111                        // DTLS server port

// MAC address (must match SoC configuration)
static uint8_t my_mac[6] = {0x10, 0xe2, 0xd5, 0x00, 0x00, 0x00};

// Receive buffer queue (Ring Buffer)
// We need to buffer multiple packets because PQC certificates arrive in bursts
// of fragments, and a single buffer would cause drops while processing.
#define RX_QUEUE_SIZE 64   // Increased from 16 to handle server bursts
#define RX_BUF_SIZE   2048

static uint8_t rx_queue[RX_QUEUE_SIZE][RX_BUF_SIZE];
static int rx_lens[RX_QUEUE_SIZE];
static volatile int rx_head = 0; // Write index
static volatile int rx_tail = 0; // Read index

// UDP CALLBACK - Called by libliteeth when packet arrives
static void udp_rx_callback(uint32_t src_ip, uint16_t src_port,
                            uint16_t dst_port, void *data, uint32_t length) {
    if (dst_port != LOCAL_PORT) {
        return;  // Ignore packets for other ports
    }
    
    // Calculate next write index
    int next_head = (rx_head + 1) % RX_QUEUE_SIZE;
    
    // Check if queue is full (next head would equal tail)
    if (next_head != rx_tail) {
        if (length <= RX_BUF_SIZE) {
            memcpy(rx_queue[rx_head], data, length);
            rx_lens[rx_head] = length;
            rx_head = next_head;
        }
    } else {
        // Queue full - packet dropped!
        // printf("!"); // Minimal debug marker for drop
    }
}

// INITIALIZATION
void network_init(void) {
    // Set our MAC and IP addresses
    udp_set_mac(my_mac);
    udp_set_ip(LOCAL_IP);
    
    // Reset queue
    rx_head = 0;
    rx_tail = 0;
    
    // Start UDP stack (initializes Ethernet MAC)
    // udp_start() clears the callback, so we must set it AFTER
    udp_start(my_mac, LOCAL_IP);
    
    // Register callback for incoming packets
    udp_set_callback(udp_rx_callback);
    
}

// SEND UDP PACKET
int network_send(const uint8_t* data, int len) {
    // Cache ARP resolution - server MAC doesn't change during session
    static int arp_resolved = 0;
    
    if (!arp_resolved) {
        // Resolve server IP to MAC address (ARP) - only once
        if (!udp_arp_resolve(SERVER_IP)) {
            return -1;
        }
        arp_resolved = 1;
    }
    
    // Get transmit buffer from libliteeth
    void* tx_buf = udp_get_tx_buffer();
    
    // Copy our data to transmit buffer
    memcpy(tx_buf, data, len);
    
    // Send the packet
    int ret = udp_send(LOCAL_PORT, SERVER_PORT, len);
    if (ret <= 0) {
        return -1;
    }
    
    return len;
}

// RECEIVE UDP PACKET
int network_recv(uint8_t* buffer, int max_len, int timeout_ms) {
    // Simple busy-wait timeout calibration for ~1MHz CPU
    // At 1MHz, each loop iteration takes ~50 cycles, so:
    // 1000ms / (50 cycles * 1us/cycle) = 1000ms / 50us = 20000 iterations/sec
    // So timeout_ms * 20 gives roughly correct timing
    volatile int timeout_counter = timeout_ms * 20; 
    
    while (timeout_counter > 0) {
        // processes incoming packets (calls udp_rx_callback)
        udp_service();
        
        // Check if we have data in the queue
        if (rx_head != rx_tail) {
            // Pop packet from tail
            int pkt_len = rx_lens[rx_tail];
            int copy_len = (pkt_len < max_len) ? pkt_len : max_len;
            
            memcpy(buffer, rx_queue[rx_tail], copy_len);
            
            // Advance tail
            rx_tail = (rx_tail + 1) % RX_QUEUE_SIZE;
            
            return copy_len;
        }
        
        timeout_counter--;
    }
    
    return 0; // Timeout
}

#else
// Stub implementations if Ethernet is not present
void network_init(void) {
    printf("ERROR: Ethernet not configured in SoC\n");
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
