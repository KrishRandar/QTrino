#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>

/**
 * Initialize network layer (Ethernet MAC + UDP stack)
 * Must be called before any network operations
 */
void network_init(void);

/**
 * Send UDP packet to DTLS server
 * @param data Pointer to data to send
 * @param len Length of data in bytes
 * @return Number of bytes sent, or negative on error
 */
int network_send(const uint8_t* data, int len);

/**
 * Receive UDP packet with timeout
 * @param buffer Buffer to store received data
 * @param max_len Maximum bytes to receive
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking)
 * @return Number of bytes received, 0 on timeout, negative on error
 */
int network_recv(uint8_t* buffer, int max_len, int timeout_ms);

#endif // NETWORK_H
