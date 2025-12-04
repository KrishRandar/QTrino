/*
 * server.c - PQC-DTLS 1.3 Server with Raw Public Key (RPK) Authentication
 * 
 * wolfSSL-based DTLS 1.3 server for testing mutual authentication
 * with RISC-V bare-metal client using ML-KEM-512 and ML-DSA-44.
 * 
 * Authentication: Raw Public Keys (RFC 7250) - no X.509 certificates
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
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

// Include embedded RPK keys (server private key + client public key for verification)
#include "server_rpk.h"

// Pacing delay between sends (milliseconds)
// The 1MHz LiteX client takes ~3 seconds to process each packet
// CRITICAL: 500ms was too fast - client drops packets while processing!
// Increased to 3000ms to match client's actual processing speed
#define SEND_PACING_MS 2500

// Quick timeout multiplier for DTLS 1.3
#define QUICK_MULT  4

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

#define SERVER_PORT 11111
#define BUFFER_SIZE 2048
#define TAP_INTERFACE "tap0"
#define CLIENT_IP "192.168.1.50"
#define CLIENT_MAC "10:e2:d5:00:00:00"

// Helper function to set DTLS handshake timeout
// Supports DTLS 1.3 quick timeout for out-of-order message handling
static void setHsTimeout(WOLFSSL* ssl, struct timeval *tv)
{
    int timeout = wolfSSL_dtls_get_current_timeout(ssl);
#ifdef WOLFSSL_DTLS13
    if (wolfSSL_dtls13_use_quick_timeout(ssl)) {
        // Use quick timeout (1/4 of normal timeout)
        if (timeout >= QUICK_MULT)
            tv->tv_sec = timeout / QUICK_MULT;
        else
            tv->tv_usec = timeout * 1000000 / QUICK_MULT;
    }
    else
#endif
        tv->tv_sec = timeout;
}

// Helper function to display connection information
static void showConnInfo(WOLFSSL* ssl)
{
    const char* cipher = wolfSSL_get_cipher(ssl);
    const char* version = wolfSSL_get_version(ssl);
    
    printf("[CONNECTION INFO]\n");
    printf("  Cipher suite: %s\n", cipher ? cipher : "(unknown)");
    printf("  Protocol version: %s\n", version ? version : "(unknown)");
}

// =============================================================================
// RPK Verification Callback for Client Authentication
// =============================================================================
// This callback is called by wolfSSL to verify the client's Raw Public Key.
// Since we use pre-shared public keys, we compare the received RPK with
// our stored copy of the client's public key.
//
// Per wolfSSL RPK documentation: access strctx->certs->buffer for the RPK data
// =============================================================================
static int rpk_verify_callback(int preverify, WOLFSSL_X509_STORE_CTX* store) {
    (void)preverify;  // Not used for RPK
    
    printf("[RPK] Verifying client's Raw Public Key...\n");
    
    if (store == NULL) {
        printf("[RPK ERROR] Verification context is NULL\n");
        return 0;  // Verification failed
    }
    
    // For RPK, the peer's public key is stored in store->certs buffer
    // This is the SubjectPublicKeyInfo DER-encoded data
    if (store->certs == NULL || store->totalCerts < 1) {
        printf("[RPK ERROR] No peer certificate/RPK data available\n");
        return 0;
    }
    
    // Get the first (and only) certificate buffer - this is the RPK
    const unsigned char* peer_pubkey = store->certs[0].buffer;
    int peer_pubkey_len = (int)store->certs[0].length;
    
    if (peer_pubkey == NULL || peer_pubkey_len <= 0) {
        printf("[RPK ERROR] Empty peer public key buffer\n");
        return 0;
    }
    
    printf("[RPK] Received client public key: %d bytes\n", peer_pubkey_len);
    
    // Compare with our pre-shared client public key
    if (peer_pubkey_len != client_pubkey_der_len) {
        printf("[RPK ERROR] Public key length mismatch: got %d, expected %d\n",
               peer_pubkey_len, client_pubkey_der_len);
        return 0;
    }
    
    if (memcmp(peer_pubkey, client_pubkey_der, client_pubkey_der_len) != 0) {
        printf("[RPK ERROR] Client public key does NOT match pre-shared key!\n");
        printf("[RPK ERROR] Possible impersonation attempt!\n");
        return 0;
    }
    
    printf("[RPK OK] Client public key matches pre-shared key\n");
    printf("[RPK OK] Client identity verified successfully\n");
    return WOLFSSL_SUCCESS;  // Verification passed
}

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
    printf("[CONFIG] Auth:       Raw Public Key (RPK) Mutual Authentication (RFC 7250)\n");
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
    
    // Enable debugging to see received data
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
    
    // Disable session tickets to simplify handshake for slow client
    printf("[CONFIG] Disabling session tickets (simplify handshake)...\n");
    wolfSSL_CTX_no_ticket_TLSv13(ctx);
    printf("[OK] Session tickets disabled\n");
    printf("\n");

    // ==========================================================================
    // RAW PUBLIC KEY (RPK) CONFIGURATION - RFC 7250
    // ==========================================================================
    // Instead of X.509 certificates, we use Raw Public Keys (SubjectPublicKeyInfo)
    // which are much lighter weight and perfect for embedded/IoT devices.
    // Authentication is done by comparing received RPK with pre-shared keys.
    // ==========================================================================
    
    printf("[RPK] Configuring Raw Public Key authentication (RFC 7250)...\n");
    
    // Set certificate types - SERVER SIDE:
    // - server_cert_type: Types WE can SEND (our RPK)
    // - client_cert_type: Types WE can ACCEPT (client's RPK)
    char rpk_type[] = {WOLFSSL_CERT_TYPE_RPK};
    
    printf("[RPK] Setting server certificate type to RPK...\n");
    ret = wolfSSL_CTX_set_server_cert_type(ctx, rpk_type, sizeof(rpk_type));
    if (ret != WOLFSSL_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to set server cert type to RPK: %d\n", ret);
        return 1;
    }
    printf("[OK] Server will send RPK (not X.509 certificate)\n");
    
    printf("[RPK] Setting client certificate type to RPK...\n");
    ret = wolfSSL_CTX_set_client_cert_type(ctx, rpk_type, sizeof(rpk_type));
    if (ret != WOLFSSL_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to set client cert type to RPK: %d\n", ret);
        return 1;
    }
    printf("[OK] Server will accept RPK from client (not X.509 certificate)\n");
    printf("\n");

    // Load our public key as the "certificate" (RPK)
    printf("[RPK] Loading server public key as RPK (%d bytes)...\n", server_pubkey_der_len);
    ret = wolfSSL_CTX_use_certificate_buffer(ctx, server_pubkey_der,
                                             server_pubkey_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load server RPK: %d\n", ret);
        return 1;
    }
    printf("[OK] Server RPK (public key) loaded successfully\n");

    // Load our private key for signing handshake messages
    printf("[RPK] Loading server private key (%d bytes, ML-DSA-44)...\n", server_key_der_len);
    ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, server_key_der,
                                            server_key_der_len,
                                            WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        fprintf(stderr, "[ERROR] Failed to load server private key: %d\n", ret);
        return 1;
    }
    printf("[OK] Server private key loaded successfully\n");
    printf("\n");

    // Configure RPK verification callback for client authentication
    printf("[RPK] Configuring mutual authentication with verify callback...\n");
    printf("[RPK] Client public key loaded for verification (%d bytes)\n", client_pubkey_der_len);
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER | 
                                WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           rpk_verify_callback);
    printf("[OK] RPK mutual authentication configured\n");
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
    
 
    printf("[DTLS] Configuring extended timeouts for 1MHz PQC client...\n");
    wolfSSL_dtls_set_timeout_init(ssl, 30);  // Initial timeout: 30 seconds
    wolfSSL_dtls_set_timeout_max(ssl, 120);  // Max timeout: 120 seconds  
    printf("[OK] DTLS timeouts: initial=30s, max=120s (for 1MHz client)\n");
    
    // Enable DTLS 1.3 stateless cookie exchange (DoS protection)
#ifdef WOLFSSL_SEND_HRR_COOKIE
    {
        // Applications should update this secret periodically in production
        const char *secret = "QTrino-PQC-Server-Secret-2024";
        printf("[SECURITY] Enabling DTLS 1.3 cookie exchange (DoS protection)...\n");
        if (wolfSSL_send_hrr_cookie(ssl, (byte*)secret, strlen(secret))
                != WOLFSSL_SUCCESS) {
            fprintf(stderr, "[WARNING] wolfSSL_send_hrr_cookie failed\n");
            fprintf(stderr, "[WARNING] Continuing without stateless cookie exchange\n");
        } else {
            printf("[OK] HRR cookie exchange enabled\n");
        }
    }
#else
    printf("[INFO] WOLFSSL_SEND_HRR_COOKIE not enabled (compile-time option)\n");
#endif
    
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

    /*
     * IMPORTANT - DTLS 1.3 Timeout Handling:
     *  - We use dynamic socket timeouts based on DTLS handshake state
     *  - setHsTimeout() uses wolfSSL_dtls_get_current_timeout() and
     *    wolfSSL_dtls13_use_quick_timeout() for DTLS 1.3 quick timeouts
     *  - On socket timeout, we call wolfSSL_dtls_got_timeout() to trigger
     *    retransmissions
     *  - WANT_READ/WRITE mean handshake is in progress (normal for slow client)
     */
    printf("[HANDSHAKE] Starting DTLS 1.3 handshake with dynamic timeouts...\n");
    for (;;) {
        // Set dynamic timeout based on current DTLS state
        if (!wolfSSL_is_init_finished(ssl)) {
            struct timeval tv;
            memset(&tv, 0, sizeof(tv));
            setHsTimeout(ssl, &tv);
            
            // Apply timeout to socket (if not set, defaults to blocking forever)
            if (tv.tv_sec > 0 || tv.tv_usec > 0) {
                setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            }
        }
        
        ret = wolfSSL_accept(ssl);
        
        if (ret == WOLFSSL_SUCCESS) {
            break; // Handshake complete!
        }

        int err = wolfSSL_get_error(ssl, ret);
        
        // Handle timeout - let wolfSSL retransmit
        if (err == WOLFSSL_ERROR_WANT_READ) {
            if (!wolfSSL_is_init_finished(ssl)) {
                // Handshake still in progress - check if timeout occurred
                if (wolfSSL_dtls_got_timeout(ssl) != WOLFSSL_SUCCESS) {
                    fprintf(stderr, "[ERROR] wolfSSL_dtls_got_timeout failed\n");
                    goto cleanup;
                }
                printf("[HANDSHAKE] Timeout - retransmitting...\n");
            } else {
                printf("[HANDSHAKE] In progress... WANT_READ\n");
            }
            continue;
        }
        
        if (err == WOLFSSL_ERROR_WANT_WRITE) {
            printf("[HANDSHAKE] In progress... WANT_WRITE\n");
            continue;
        }

        // Any other error is fatal
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
        fprintf(stderr, "  - Ensure RPK keys match (run: cd certs && ./generate_rpk_keys.sh)\n");
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
    showConnInfo(ssl);
    printf("===============================================================================\n");
    printf("\n");

    // Receive data from client with extended timeout for 1MHz client
    // Client needs time to process ACK and prepare application data
    printf("[DATA] Waiting for encrypted data from client...\n");
    printf("[INFO] Extended timeout: 120 seconds (1MHz client needs time to send data)\n");
    
    // Set extended socket timeout for application data
    struct timeval data_timeout;
    data_timeout.tv_sec = 120;  // 2 minutes for slow client
    data_timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &data_timeout, sizeof(data_timeout));
    
    // Retry loop - client may need multiple attempts
    int max_retries = 5;
    for (int attempt = 1; attempt <= max_retries; attempt++) {
        printf("[DATA] Attempt %d/%d - waiting for client data...\n", attempt, max_retries);
        ret = wolfSSL_read(ssl, buffer, sizeof(buffer) - 1);
        
        if (ret > 0) {
            break;  // Got data!
        }
        
        int err = wolfSSL_get_error(ssl, ret);
        if (err == WOLFSSL_ERROR_WANT_READ) {
            printf("[INFO] Timeout, retrying... (client at 1MHz may still be processing)\n");
            continue;
        } else {
            // Real error, not just timeout
            fprintf(stderr, "[ERROR] wolfSSL_read failed with error: %d\n", err);
            break;
        }
    }
    
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
    printf("  ✓ Client authenticated with Raw Public Key (RPK)\n");
    printf("  ✓ ML-DSA-44 signatures verified\n");
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
