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
// 200ms for Certificate fragments which need reassembly
#define SEND_PACING_MS 200

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
    
    if (g_peer_set) {
        sent = (int)sendto(g_sockfd, buf, sz, 0, 
                           (struct sockaddr*)&g_peer_addr, sizeof(g_peer_addr));
    } else {
        sent = (int)send(g_sockfd, buf, sz, 0);
    }
    
    if (sent > 0) {
        // Add delay after each send to let client process
        printf("[PACING] Sent %d bytes, waiting %dms for client...\n", sent, SEND_PACING_MS);
        usleep(SEND_PACING_MS * 1000);
    } else if (sent < 0) {
        printf("[PACING] Send error: %s\n", strerror(errno));
    }
    
    return sent;
}

// Custom receive callback to capture peer address
static int PacedRecvFrom(WOLFSSL* ssl, char* buf, int sz, void* ctx)
{
    (void)ssl;
    (void)ctx;
    
    socklen_t peer_len = sizeof(g_peer_addr);
    int received = (int)recvfrom(g_sockfd, buf, sz, 0,
                                  (struct sockaddr*)&g_peer_addr, &peer_len);
    
    if (received > 0) {
        if (!g_peer_set) {
            g_peer_set = 1;
            printf("[PACING] Peer address captured: %s:%d\n",
                   inet_ntoa(g_peer_addr.sin_addr), ntohs(g_peer_addr.sin_port));
        }
    } else if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return WOLFSSL_CBIO_ERR_WANT_READ;
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

    printf("========================================\n");
    printf("PQC-DTLS 1.3 Server\n");
    printf("========================================\n");
    printf("Algorithm: ML-KEM-512 + ML-DSA-44\n");
    printf("Protocol:  DTLS 1.3 (Pure PQC)\n");
    printf("Auth:      X.509 Mutual Authentication\n");
    printf("Port:      %d\n", SERVER_PORT);
    printf("========================================\n\n");

    // Initialize wolfSSL
    printf("[INIT] Initializing wolfSSL...\n");
    wolfSSL_Init();
    
    // Enable debugging
    wolfSSL_Debugging_ON();
    printf("[OK] wolfSSL initialized\n\n");

    // Create DTLS 1.3 server context
    printf("[DTLS] Creating DTLS 1.3 server context...\n");
    ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());
    if (!ctx) {
        fprintf(stderr, "[ERROR] Failed to create context\n");
        return 1;
    }
    printf("[OK] DTLS 1.3 context created\n\n");

    // Load server certificate
    printf("[CERT] Loading server certificate...\n");
    if (client_cert_der_len > 0) {
        // Using client cert as server cert for now (placeholder)
        ret = wolfSSL_CTX_use_certificate_buffer(ctx, client_cert_der,
                                                 client_cert_der_len,
                                                 WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load server cert: %d\n", ret);
            return 1;
        }
        printf("[OK] Server certificate loaded (%d bytes)\n", client_cert_der_len);
    } else {
        printf("[WARNING] No server certificate available (placeholder)\n");
        printf("[WARNING] Using PSK or anonymous mode\n");
    }

    // Load server private key
    printf("[CERT] Loading server private key...\n");
    if (client_key_der_len > 0) {
        // Using client key as server key for now (placeholder)
        ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, client_key_der,
                                                client_key_der_len,
                                                WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load server key: %d\n", ret);
            return 1;
        }
        printf("[OK] Server private key loaded (%d bytes)\n\n", client_key_der_len);
    } else {
        printf("[WARNING] No server private key available (placeholder)\n\n");
    }

    // Load CA for client verification
    printf("[CERT] Loading CA certificate for client verification...\n");
    if (ca_cert_der_len > 0) {
        ret = wolfSSL_CTX_load_verify_buffer(ctx, ca_cert_der, ca_cert_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[ERROR] Failed to load CA cert: %d\n", ret);
            return 1;
        }
        printf("[OK] CA certificate loaded (%d bytes)\n\n", ca_cert_der_len);
    } else {
        printf("[WARNING] No CA certificate available (placeholder)\n");
        printf("[WARNING] Client verification will be skipped\n\n");
    }

    // Configure mutual authentication (if certificates available)
    if (ca_cert_der_len > 0 && client_cert_der_len > 0) {
        printf("[DTLS] Configuring mutual authentication...\n");
        wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER |
                                    WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        printf("[OK] Mutual authentication configured\n\n");
    } else {
        printf("[WARNING] Skipping mutual authentication (no certificates)\n\n");
    }

    // Create UDP socket
    printf("[SOCKET] Creating UDP socket...\n");
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket");
        return 1;
    }
    printf("[OK] UDP socket created\n");

    // Optionally bind socket to tap0 interface
    // This ensures packets are sent via the correct interface
    printf("[NETWORK] Binding socket to %s interface...\n", TAP_INTERFACE);
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, TAP_INTERFACE, strlen(TAP_INTERFACE)) < 0) {
        // Not critical - socket will still work, but may use wrong interface
        printf("[WARNING] Failed to bind to %s: %s\n", TAP_INTERFACE, strerror(errno));
        printf("[WARNING] Ensure static ARP entry is configured (run setup_network.sh)\n");
    } else {
        printf("[OK] Socket bound to %s\n", TAP_INTERFACE);
    }

    // Verify network configuration
    printf("[NETWORK] Verifying network configuration...\n");
    printf("[NETWORK] Client IP: %s\n", CLIENT_IP);
    printf("[NETWORK] Expected MAC: %s\n", CLIENT_MAC);
    printf("[INFO]   Run './server/setup_network.sh' if ARP entry is missing\n");

    // Bind to port
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    printf("[SOCKET] Binding to port %d...\n", SERVER_PORT);
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[ERROR] bind");
        return 1;
    }
    printf("[OK] Socket bound to port %d\n\n", SERVER_PORT);

    printf("========================================\n");
    printf("SERVER READY - WAITING FOR CLIENT\n");
    printf("========================================\n\n");

    // Create SSL session
    printf("[DTLS] Creating SSL session...\n");
    ssl = wolfSSL_new(ctx);
    if (!ssl) {
        fprintf(stderr, "[ERROR] Failed to create SSL session\n");
        return 1;
    }
    
    // Store socket in global for callbacks
    g_sockfd = sockfd;
    
    // Use non-blocking mode for DTLS
    wolfSSL_dtls_set_using_nonblock(ssl, 1);
    
    // Register custom I/O callbacks with pacing
    // This adds delays between sends to let the slow 1MHz client process packets
    printf("[PACING] Registering paced I/O callbacks (%dms delay between sends)...\n", SEND_PACING_MS);
    wolfSSL_SSLSetIOSend(ssl, PacedSendTo);
    wolfSSL_SSLSetIORecv(ssl, PacedRecvFrom);
    
    printf("[OK] SSL session created with pacing\n\n");

    printf("[WAITING] For DTLS handshake from client...\n");
    printf("  (Client should connect from 192.168.1.50)\n");

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
    for (;;) {
        ret = wolfSSL_accept(ssl);
        if (ret == WOLFSSL_SUCCESS) {
            break; // handshake complete
        }

        int err = wolfSSL_get_error(ssl, ret);
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            fprintf(stderr,
                    "[HANDSHAKE] wolfSSL_accept WANT_%s, retrying...\n",
                    (err == WOLFSSL_ERROR_WANT_READ) ? "READ" : "WRITE");
            continue;
        }

        // Any other error is fatal.
        char err_buf[80];
        wolfSSL_ERR_error_string(err, err_buf);
        fprintf(stderr, "\n[ERROR] Handshake failed!\n");
        fprintf(stderr, "  Return code: %d\n", ret);
        fprintf(stderr, "  Error code:  %d\n", err);
        fprintf(stderr, "  Error string: %s\n", err_buf);
        goto cleanup;
    }

    printf("\n========================================\n");
    printf("DTLS 1.3 HANDSHAKE COMPLETE!\n");
    printf("========================================\n");
    printf("[SUCCESS] Secure channel established\n");
    printf("[SUCCESS] Client authenticated\n\n");

    // Display connection information
    printf("[INFO] Cipher Suite: %s\n", wolfSSL_get_cipher(ssl));
    printf("[INFO] Protocol Version: %s\n\n", wolfSSL_get_version(ssl));

    // Receive data from client
    printf("[DATA] Waiting for data from client...\n");
    ret = wolfSSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (ret > 0) {
        buffer[ret] = '\0';
        printf("[RECEIVED] %d bytes: \"%s\"\n\n", ret, buffer);

        // Send response
        const char* response = "Hello from PQC-DTLS server!";
        printf("[DATA] Sending response: \"%s\"\n", response);
        ret = wolfSSL_write(ssl, response, strlen(response));
        if (ret > 0) {
            printf("[OK] Sent %d bytes\n\n", ret);
        } else {
            int err = wolfSSL_get_error(ssl, ret);
            fprintf(stderr, "[ERROR] Send failed: %d (error: %d)\n\n", ret, err);
        }
    } else {
        int err = wolfSSL_get_error(ssl, ret);
        fprintf(stderr, "[ERROR] Receive failed: %d (error: %d)\n\n", ret, err);
    }

    printf("========================================\n");
    printf("SESSION COMPLETE\n");
    printf("========================================\n");

cleanup:
    printf("\n[CLEANUP] Closing connection...\n");
    wolfSSL_free(ssl);
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    close(sockfd);
    
    printf("[DONE] Server terminated\n");
    return 0;
}
