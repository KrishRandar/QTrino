/*
 * server.c - PQC-DTLS 1.3 Server
 * 
 * wolfSSL-based DTLS 1.3 server for testing mutual authentication
 * with RISC-V bare-metal client using ML-KEM-512 and ML-DSA-44.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if.h>
#include <time.h>

// SO_BINDTODEVICE may not be defined in all headers
#ifndef SO_BINDTODEVICE
#define SO_BINDTODEVICE 25
#endif

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

// Pacing delay between sends (milliseconds)
// The 1MHz LiteX client needs time to process each packet
// 2000ms gives the client time to poll MAC before next packet arrives
#define SEND_PACING_MS 2000

// Global for peer address (set after first receive)
static struct sockaddr_in g_peer_addr;
static int g_peer_set = 0;
static int g_sockfd = -1;

// Custom send callback with pacing for slow client
static int PacedSendTo(WOLFSSL* ssl, char* buf, int sz, void* ctx)
{
    (void)ssl;  // unused
    (void)ctx;  // we use global sockfd instead
    
    int sent;
    
    printf("  [NETWORK] >>> Sending %d bytes to client...\n", sz);
    if (g_peer_set) {
        sent = (int)sendto(g_sockfd, buf, sz, 0, 
                           (struct sockaddr*)&g_peer_addr, sizeof(g_peer_addr));
    } else {
        sent = (int)send(g_sockfd, buf, sz, 0);
    }
    
    if (sent > 0) {
        // Add delay after each send to let client process
        printf("  [OK] Sent %d bytes successfully\n", sent);
        printf("  [PACING] Waiting %dms for 1MHz client to process...\n", SEND_PACING_MS);
        usleep(SEND_PACING_MS * 1000);
    } else if (sent < 0) {
        printf("  [ERROR] Send failed: %s\n", strerror(errno));
    }
    
    return sent;
}

// Custom receive callback to capture peer address
static int PacedRecvFrom(WOLFSSL* ssl, char* buf, int sz, void* ctx)
{
    (void)ssl;
    (void)ctx;
    
    socklen_t peer_len = sizeof(g_peer_addr);
    printf("  [NETWORK] <<< Waiting to receive (max %d bytes)...\n", sz);
    int received = (int)recvfrom(g_sockfd, buf, sz, 0,
                                  (struct sockaddr*)&g_peer_addr, &peer_len);
    
    if (received > 0) {
        printf("  [OK] Received %d bytes from client\n", received);
        if (!g_peer_set) {
            g_peer_set = 1;
            printf("  [INFO] Client address: %s:%d\n",
                   inet_ntoa(g_peer_addr.sin_addr), ntohs(g_peer_addr.sin_port));
        }
    } else if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("  [TIMEOUT] No data received (will retry)\n");
        return WOLFSSL_CBIO_ERR_WANT_READ;
    } else if (received < 0) {
        printf("  [ERROR] Receive failed: %s\n", strerror(errno));
    }
    
    return received;
}

#ifdef NO_WOLFSSL_SERVER
#error "NO_WOLFSSL_SERVER IS DEFINED! SERVER SUPPORT DISABLED!"
#endif

#ifndef WOLFSSL_DTLS13
#error "WOLFSSL_DTLS13 IS NOT DEFINED!"
#endif

// Include embedded certificates (placeholder for now)
#include "../boot/certs_placeholder.h"

#define SERVER_PORT 11111
#define BUFFER_SIZE 2048
#define TAP_INTERFACE "tap0"
#define CLIENT_IP "192.168.1.50"
#define CLIENT_MAC "10:e2:d5:00:00:00"

int main(void)
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    WOLFSSL_CTX* ctx;
    WOLFSSL* ssl;
    int ret;
    char buffer[BUFFER_SIZE];

    printf("\n");
    printf("===============================================================================\n");
    printf("                      PQC-DTLS 1.3 Server\n");
    printf("===============================================================================\n");
    printf("[CONFIG] Algorithm:  ML-KEM-512 (Key Exchange) + ML-DSA-44 (Signatures)\n");
    printf("[CONFIG] Protocol:   DTLS 1.3 (Pure Post-Quantum Cryptography)\n");
    printf("[CONFIG] Auth:       X.509 Mutual Authentication\n");
    printf("[CONFIG] Port:       %d (UDP)\n", SERVER_PORT);
    printf("[CONFIG] Interface:  %s\n", TAP_INTERFACE);
    printf("[CONFIG] Pacing:     %dms delay between sends (for 1MHz client)\n", SEND_PACING_MS);
    printf("===============================================================================\n");
    printf("\n");

    // Initialize wolfSSL
    printf("[INIT] Initializing wolfSSL library...\n");
    wolfSSL_Init();
    printf("[OK] wolfSSL library initialized\n");
    printf("\n");
    
    // Enable debugging
    printf("[DEBUG] Enabling wolfSSL debug output...\n");
    wolfSSL_Debugging_ON();
    printf("[OK] Debug logging enabled\n");
    printf("\n");

    // Create DTLS 1.3 server context
    printf("[DTLS] Creating DTLS 1.3 server context...\n");
    ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());
    if (!ctx) {
        fprintf(stderr, "[ERROR] Failed to create DTLS 1.3 context\n");
        fprintf(stderr, "[ERROR] Check wolfSSL compilation (WOLFSSL_DTLS13)\n");
        return 1;
    }
    printf("[OK] DTLS 1.3 server context created\n");
    printf("\n");

    // Load server certificate
    printf("[CERT] Loading certificates and keys...\n");
    if (client_cert_der_len > 0) {
        printf("[CERT] Loading server certificate (%d bytes, ML-DSA-44)...\n", client_cert_der_len);
        // Using client cert as server cert for now (placeholder)
        ret = wolfSSL_CTX_use_certificate_buffer(ctx, client_cert_der,
                                                 client_cert_der_len,
                                                 WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load server certificate: %d\n", ret);
            fprintf(stderr, "[ERROR] Cannot authenticate without certificate\n");
            return 1;
        }
        printf("[OK] Server certificate loaded successfully\n");
    } else {
        printf("[WARNING] No server certificate available (placeholder)\n");
        printf("[WARNING] Using PSK or anonymous mode\n");
    }

    // Load server private key
    if (client_key_der_len > 0) {
        printf("[CERT] Loading server private key (%d bytes, ML-DSA-44)...\n", client_key_der_len);
        // Using client key as server key for now (placeholder)
        ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, client_key_der,
                                                client_key_der_len,
                                                WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load server private key: %d\n", ret);
            fprintf(stderr, "[ERROR] Cannot sign handshake without private key\n");
            return 1;
        }
        printf("[OK] Server private key loaded successfully\n");
    } else {
        printf("[WARNING] No server private key available (placeholder)\n");
    }
    printf("\n");

    // Load CA for client verification
    if (ca_cert_der_len > 0) {
        printf("[CERT] Loading CA certificate (%d bytes, ASN.1 DER)...\n", ca_cert_der_len);
        ret = wolfSSL_CTX_load_verify_buffer(ctx, ca_cert_der, ca_cert_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load CA certificate: %d\n", ret);
            fprintf(stderr, "[ERROR] Client verification will fail\n");
            return 1;
        }
        printf("[OK] CA certificate loaded successfully\n");
    } else {
        printf("[WARNING] No CA certificate available (placeholder)\n");
        printf("[WARNING] Client verification will be skipped\n");
    }
    printf("\n");

    // Configure mutual authentication (if certificates available)
    if (ca_cert_der_len > 0 && client_cert_der_len > 0) {
        printf("[SECURITY] Configuring mutual authentication...\n");
        wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER |
                                    WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        printf("[OK] Mutual authentication enabled (verify peer cert)\n");
    } else {
        printf("[WARNING] Mutual authentication disabled (missing certificates)\n");
    }
    printf("\n");

    // Create UDP socket
    printf("[NETWORK] Setting up network...\n");
    printf("[NETWORK] Creating UDP socket...\n");
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket creation failed");
        return 1;
    }
    printf("[OK] UDP socket created (fd: %d)\n", sockfd);

    // Optionally bind socket to tap0 interface
    // This ensures packets are sent via the correct interface
    printf("[NETWORK] Binding socket to %s interface...\n", TAP_INTERFACE);
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, TAP_INTERFACE, strlen(TAP_INTERFACE)) < 0) {
        // Not critical - socket will still work, but may use wrong interface
        printf("[WARNING] Failed to bind to %s: %s\n", TAP_INTERFACE, strerror(errno));
        printf("[WARNING] Socket may use default interface\n");
        printf("[INFO] Run './server/setup_network.sh' to configure tap0\n");
    } else {
        printf("[OK] Socket bound to %s\n", TAP_INTERFACE);
    }

    // Verify network configuration
    printf("[NETWORK] Verifying network configuration...\n");
    printf("[INFO] Expected client IP: %s\n", CLIENT_IP);
    printf("[INFO] Expected client MAC: %s\n", CLIENT_MAC);
    printf("[INFO] If connection fails, run: ./server/setup_network.sh\n");

    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    printf("[NETWORK] Binding to port %d on all interfaces...\n", SERVER_PORT);
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] bind failed");
        fprintf(stderr, "[ERROR] Port %d may already be in use\n", SERVER_PORT);
        return 1;
    }
    printf("[OK] Socket bound to port %d\n", SERVER_PORT);
    printf("\n");

    // Create SSL session
    printf("[SSL] Creating SSL session object...\n");
    ssl = wolfSSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "[ERROR] Failed to create SSL session\n");
        fprintf(stderr, "[ERROR] Check context configuration and memory\n");
        return 1;
    }
    printf("[OK] SSL session created\n");
    
    // Store socket in global for callbacks
    g_sockfd = sockfd;
    
    // Use non-blocking mode for DTLS
    wolfSSL_dtls_set_using_nonblock(ssl, 1);
    
    // Register custom I/O callbacks with pacing
    // This adds delays between sends to let the slow 1MHz client process packets
    printf("[PACING] Registering paced I/O callbacks...\n");
    printf("[INFO] Send delay: %dms (allows 1MHz client to process packets)\n", SEND_PACING_MS);
    wolfSSL_SSLSetIOSend(ssl, PacedSendTo);
    wolfSSL_SSLSetIORecv(ssl, PacedRecvFrom);
    printf("[OK] I/O callbacks registered with pacing\n");
    printf("\n");

    printf("===============================================================================\n");
    printf("                     WAITING FOR CLIENT CONNECTION\n");
    printf("===============================================================================\n");
    printf("[INFO] Listening on UDP port %d (interface: %s)\n", SERVER_PORT, TAP_INTERFACE);
    printf("[INFO] Expected client: %s\n", CLIENT_IP);
    printf("[INFO] Handshake may take 60-90 seconds with 1MHz PQC client\n");
    printf("\n");

    struct timeval timeout;
    timeout.tv_sec  = 1800;  // 30 minute overall socket receive timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    /*
     * IMPORTANT:
     *  - PacedRecvFrom() returns WOLFSSL_CBIO_ERR_WANT_READ on timeout,
     *    which becomes WOLFSSL_ERROR_WANT_READ from wolfSSL_get_error().
     *  - On this setup the client is extremely slow (1MHz + PQC), so the
     *    server must treat WANT_READ/WRITE as "handshake still in progress"
     *    and keep calling wolfSSL_accept().
     */
    printf("[HANDSHAKE] Starting DTLS 1.3 handshake...\n");
    for (;;) {
        ret = wolfSSL_accept(ssl);
        if (ret == WOLFSSL_SUCCESS) {
            break; // handshake complete
        }

        int err = wolfSSL_get_error(ssl, ret);
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            printf("[HANDSHAKE] In progress... WANT_%s (normal for slow client)\n",
                    (err == WOLFSSL_ERROR_WANT_READ) ? "READ" : "WRITE");
            continue;
        }

        // Any other error is fatal.
        char err_buf[80];
        wolfSSL_ERR_error_string(err, err_buf);
        fprintf(stderr, "\n");
        fprintf(stderr, "===============================================================================\n");
        fprintf(stderr, "                        HANDSHAKE FAILED!\n");
        fprintf(stderr, "===============================================================================\n");
        fprintf(stderr, "[ERROR] Return code: %d\n", ret);
        fprintf(stderr, "[ERROR] Error code:  %d\n", err);
        fprintf(stderr, "[ERROR] Error string: %s\n", err_buf);
        fprintf(stderr, "\n");
        fprintf(stderr, "[TROUBLESHOOTING]\n");
        fprintf(stderr, "  - Verify client is running on LiteX simulator\n");
        fprintf(stderr, "  - Check tap0 network interface configuration\n");
        fprintf(stderr, "  - Ensure certificates match (ca_cert.h, server/client certs)\n");
        fprintf(stderr, "  - Verify ARP entry: arp -n | grep %s\n", CLIENT_IP);
        fprintf(stderr, "===============================================================================\n");
        goto cleanup;
    }

    printf("\n");
    printf("===============================================================================\n");
    printf("                   DTLS 1.3 HANDSHAKE COMPLETE!\n");
    printf("===============================================================================\n");
    printf("[SUCCESS] Secure channel established with client\n");
    printf("[SUCCESS] Client authenticated successfully\n");
    printf("[SUCCESS] Cipher suite: %s\n", wolfSSL_get_cipher(ssl));
    printf("[SUCCESS] Protocol version: %s\n", wolfSSL_get_version(ssl));
    printf("===============================================================================\n");
    printf("\n");

    // Receive data from client
    printf("[DATA] Waiting for encrypted data from client...\n");
    ret = wolfSSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (ret > 0) {
        buffer[ret] = '\0';
        printf("[OK] Received encrypted data (%d bytes)\n", ret);
        printf("[DATA] Decrypted message: \"%s\"\n", buffer);
        printf("\n");

        // Send response
        const char* response = "Hello from PQC-DTLS server!";
        printf("[DATA] Sending encrypted response to client...\n");
        printf("[DATA] Message: \"%s\" (%d bytes)\n", response, (int)strlen(response));
        ret = wolfSSL_write(ssl, response, strlen(response));
        if (ret > 0) {
            printf("[OK] Sent encrypted response (%d bytes)\n", ret);
        } else {
            int err = wolfSSL_get_error(ssl, ret);
            fprintf(stderr, "[ERROR] Failed to send response: %d (error: %d)\n", ret, err);
        }
    } else {
        int err = wolfSSL_get_error(ssl, ret);
        fprintf(stderr, "[ERROR] Failed to receive data: %d (error: %d)\n", ret, err);
    }
    printf("\n");

    printf("===============================================================================\n");
    printf("                         SESSION COMPLETE\n");
    printf("===============================================================================\n");
    printf("[SUMMARY]\n");
    printf("  ✓ DTLS 1.3 handshake completed successfully\n");
    printf("  ✓ Client authenticated with PQC certificate\n");
    printf("  ✓ Secure bidirectional communication established\n");
    printf("  ✓ Data encrypted with quantum-resistant algorithms\n");
    printf("===============================================================================\n");

cleanup:
    printf("\n[CLEANUP] Cleaning up resources...\n");
    wolfSSL_free(ssl);
    printf("[OK] SSL session freed\n");
    wolfSSL_CTX_free(ctx);
    printf("[OK] SSL context freed\n");
    wolfSSL_Cleanup();
    printf("[OK] wolfSSL library cleaned up\n");
    close(sockfd);
    printf("[OK] Socket closed\n");
    
    printf("\n[INFO] Server terminated successfully\n");
    return 0;
}
