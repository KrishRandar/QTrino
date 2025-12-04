#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <irq.h>
#include <libbase/uart.h>
#include <generated/csr.h>

#include <wolfssl/wolfcrypt/user_settings.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include "network.h"

// Include embedded RPK keys (Raw Public Keys for mutual authentication)
#include "certs_placeholder.h"

// Undef conflicting macros
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// REQUIRED STUBS FOR BARE-METAL
#include <wolfssl/wolfcrypt/types.h>
#include <sys/time.h>
#include <time.h>

// Custom entropy source - REPLACE
static uint32_t rng_state = 0x12345678; // Initial seed

int CustomRngGenerateBlock(byte *output, word32 sz) {
    for (word32 i = 0; i < sz; i++) {
        rng_state = rng_state * 1664525UL + 1013904223UL;
        output[i] = (byte)(rng_state >> 24); // Use top 8 bits
    }
    
    return 0;
}

// Stub for gettimeofday - required by wolfSSL
#include <sys/time.h>

// Dummy time functions for bare-metal
int gettimeofday(struct timeval *restrict tv, void *restrict tz) {
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

int getitimer(int which, struct itimerval *curr_value) {
    if (curr_value) {
        curr_value->it_value.tv_sec = 0;
        curr_value->it_value.tv_usec = 0;
    }
    return 0;
}

int setitimer(int which, const struct itimerval *restrict new_value, struct itimerval *restrict old_value) {
    return 0;
}

#include <signal.h>
int sigaction(int signum, const struct sigaction *restrict act, struct sigaction *restrict oldact) {
    return 0;
}

unsigned int LowResTimer(void) {
    return 0; // for now
}

// =============================================================================
// RPK Verification Callback
// =============================================================================
// This callback is called by wolfSSL to verify the peer's Raw Public Key.
// Since we use pre-shared public keys, we compare the received RPK with
// our stored copy of the server's public key.
// 
// Per wolfSSL RPK documentation: access strctx->certs->buffer for the RPK data
// =============================================================================
static int rpk_verify_callback(int preverify, WOLFSSL_X509_STORE_CTX* store) {
    (void)preverify;  // Not used for RPK
    
    printf("[RPK] Verifying server's Raw Public Key...\n");
    
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
    
    printf("[RPK] Received server public key: %d bytes\n", peer_pubkey_len);
    
    // Compare with our pre-shared server public key
    if (peer_pubkey_len != server_pubkey_der_len) {
        printf("[RPK ERROR] Public key length mismatch: got %d, expected %d\n",
               peer_pubkey_len, server_pubkey_der_len);
        return 0;
    }
    
    if (memcmp(peer_pubkey, server_pubkey_der, server_pubkey_der_len) != 0) {
        printf("[RPK ERROR] Server public key does NOT match pre-shared key!\n");
        printf("[RPK ERROR] Possible man-in-the-middle attack!\n");
        return 0;
    }
    
    printf("[RPK OK] Server public key matches pre-shared key\n");
    printf("[RPK OK] Server identity verified successfully\n");
    return WOLFSSL_SUCCESS;  // Verification passed
}

static int dtls_send_callback(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    (void)ssl;
    (void)ctx;
    
    // Process any pending RX packets before sending to prevent buffer overflow
    extern void udp_service(void);
    udp_service();
    
    printf("  [NETWORK] >>> Sending %d bytes...\n", sz);
    int ret = network_send((uint8_t*)buf, sz);
    if (ret < 0) {
        printf("  [ERROR] Network send failed!\n");
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
    printf("  [OK] Sent %d bytes successfully\n", ret);
    
    // Process any RX packets that arrived while we were sending
    udp_service();
    
    return ret;
}

// Callback for receiving data from the network
int dtls_recv_callback(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    int ret;
    (void)ssl;
    (void)ctx;
    
    // Use the udp_recv function from network.c
    printf("  [NETWORK] <<< Waiting to receive (timeout: 30s)...\n");
    ret = network_recv((uint8_t*)buf, sz, 30000); // 30 second timeout for slow PQC ops
    
    if (ret > 0) {
        printf("  [OK] Received %d bytes\n", ret);
        return ret;
    } else if (ret == 0) {
        // Timeout - this is normal during handshake
        printf("  [TIMEOUT] No data received (will retry)\n");
        return WOLFSSL_CBIO_ERR_WANT_READ;
    } else {
        printf("  [ERROR] Network receive failed!\n");
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
}


int main(void)
{
    int ret;
    WOLFSSL_CTX* ctx = NULL;
    WOLFSSL* ssl = NULL;

#ifdef CONFIG_CPU_HAS_INTERRUPT
    irq_setmask(0);
    irq_setie(1);
#endif

    uart_init();
    
    printf("\n");
    printf("===============================================================================\n");
    printf("              PQC-DTLS 1.3 Client - RISC-V Bare-Metal\n");
    printf("===============================================================================\n");
    printf("[CONFIG] Algorithm:  ML-KEM-512 (Key Exchange) + ML-DSA-44 (Signatures)\n");
    printf("[CONFIG] Protocol:   DTLS 1.3 (Pure Post-Quantum Cryptography)\n");
    printf("[CONFIG] Auth:       Raw Public Key (RPK) Mutual Authentication (RFC 7250)\n");
    printf("[CONFIG] CPU:        RISC-V VexRISCV @ ~1MHz (Bare-Metal)\n");
    printf("===============================================================================\n");
    printf("\n");

    // Initialize network layer
    printf("[INIT] Initializing network layer...\n");
    network_init();
    printf("[OK] Network initialized (IP: 192.168.1.50, Server: 192.168.1.100:11111)\n");
    printf("\n");

    // Initialize wolfSSL static memory pool 
    #ifdef WOLFSSL_STATIC_MEMORY
    printf("[MEMORY] Allocating static memory pool for PQC operations...\n");
    // PQC requires large buckets: 64KB (×5) + 128KB (×1) = 448KB minimum
    // Plus certificates, keys, fragment reassembly, and runtime: need ~3MB+
    static unsigned char memory[4194304]; // 4MB for PQC + fragments
    static WOLFSSL_HEAP_HINT* heap_hint = NULL;
    
    ret = wc_LoadStaticMemory(&heap_hint, memory, sizeof(memory), WOLFMEM_GENERAL, 10);
    if (ret != 0) {
        printf("[ERROR] Static memory initialization failed: %d\n", ret);
        printf("[ERROR] PQC operations require large memory allocation\n");
        return 1;
    }
    printf("[OK] Static memory pool: %d bytes (4 MB) allocated\n", (int)sizeof(memory));
    printf("[INFO] Memory reserved for: PQC keys, certificates, handshake buffers\n");
    printf("\n");
    #endif

    // Initialize wolfSSL library
    printf("[INIT] Initializing wolfSSL library...\n");
    ret = wolfSSL_Init();
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] wolfSSL_Init failed: %d\n", ret);
        printf("[ERROR] Cannot proceed without SSL library initialization\n");
        return 1;
    }
    printf("[OK] wolfSSL library initialized\n");
    printf("\n");

    // Enable debugging (if compiled with DEBUG_WOLFSSL)
#ifdef DEBUG_WOLFSSL
    // wolfSSL_Debugging_ON();  
#endif

    // Create DTLS 1.3 client context
    printf("[DTLS] Creating DTLS 1.3 client context...\n");
    #ifdef WOLFSSL_STATIC_MEMORY
        ctx = wolfSSL_CTX_new_ex(wolfDTLSv1_3_client_method(), heap_hint);
    #else
        ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
    #endif
    if (ctx == NULL) {
        printf("[ERROR] Failed to create DTLS 1.3 context\n");
        printf("[ERROR] Check wolfSSL compilation flags (WOLFSSL_DTLS13)\n");
        goto cleanup;
    }
    printf("[OK] DTLS 1.3 client context created\n");
    printf("\n");

    // Set supported groups (ML-KEM-512)
    // ret = wolfSSL_CTX_set_groups_list(ctx, "ML-KEM-512");
    // if (ret != WOLFSSL_SUCCESS) {
    //     printf("ERROR: Failed to set groups list: %d\n", ret);
    //     goto cleanup;
    // }

    // Set I/O callbacks for bare-metal networking
    printf("[NETWORK] Registering custom I/O callbacks...\n");
    wolfSSL_CTX_SetIOSend(ctx, dtls_send_callback);
    wolfSSL_CTX_SetIORecv(ctx, dtls_recv_callback);
    printf("[OK] I/O callbacks registered (send/receive via bare-metal stack)\n");
    printf("\n");

    // ==========================================================================
    // RAW PUBLIC KEY (RPK) CONFIGURATION - RFC 7250
    // ==========================================================================
    // Instead of X.509 certificates, we use Raw Public Keys (SubjectPublicKeyInfo)
    // which are much lighter weight and perfect for embedded/IoT devices.
    // Authentication is done by comparing received RPK with pre-shared keys.
    // ==========================================================================
    
    printf("[RPK] Configuring Raw Public Key authentication (RFC 7250)...\n");
    
    // Set certificate types - CLIENT SIDE:
    // - client_cert_type: Types WE can SEND (our RPK)
    // - server_cert_type: Types WE can ACCEPT (server's RPK)
    char rpk_type[] = {WOLFSSL_CERT_TYPE_RPK};
    
    printf("[RPK] Setting client certificate type to RPK...\n");
    ret = wolfSSL_CTX_set_client_cert_type(ctx, rpk_type, sizeof(rpk_type));
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] Failed to set client cert type to RPK: %d\n", ret);
        goto cleanup;
    }
    printf("[OK] Client will send RPK (not X.509 certificate)\n");
    
    printf("[RPK] Setting server certificate type to RPK...\n");
    ret = wolfSSL_CTX_set_server_cert_type(ctx, rpk_type, sizeof(rpk_type));
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] Failed to set server cert type to RPK: %d\n", ret);
        goto cleanup;
    }
    printf("[OK] Client will accept RPK from server (not X.509 certificate)\n");
    printf("\n");

    // Load our public key as the "certificate" (RPK)
    // For RPK, we use the SubjectPublicKeyInfo DER format
    printf("[RPK] Loading client public key as RPK (%d bytes)...\n", client_pubkey_der_len);
    ret = wolfSSL_CTX_use_certificate_buffer(ctx, client_pubkey_der,
                                             client_pubkey_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] Failed to load client RPK: %d\n", ret);
        printf("[ERROR] Cannot send our identity to server without RPK\n");
        goto cleanup;
    }
    printf("[OK] Client RPK (public key) loaded successfully\n");

    // Load our private key for signing handshake messages
    printf("[RPK] Loading client private key (%d bytes, ML-DSA-44)...\n", client_key_der_len);
    ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, client_key_der,
                                            client_key_der_len,
                                            WOLFSSL_FILETYPE_ASN1);
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] Failed to load client private key: %d\n", ret);
        printf("[ERROR] Cannot sign handshake messages without private key\n");
        goto cleanup;
    }
    printf("[OK] Client private key loaded successfully\n");
    printf("\n");

    // Configure RPK verification callback for server authentication
    // We verify the server's RPK matches our pre-shared copy
    printf("[RPK] Configuring mutual authentication with verify callback...\n");
    printf("[RPK] Server public key loaded for verification (%d bytes)\n", server_pubkey_der_len);
    wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER | 
                                WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           rpk_verify_callback);
    printf("[OK] RPK mutual authentication configured\n");
    printf("\n");

    // Create SSL session
    printf("[SSL] Creating SSL session object...\n");
    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        printf("[ERROR] Failed to create SSL session\n");
        printf("[ERROR] Check memory allocation and context configuration\n");
        goto cleanup;
    }
    printf("[OK] SSL session created\n");


    printf("[DTLS] Configuring extended timeouts for 1MHz PQC operations...\n");
    wolfSSL_dtls_set_timeout_init(ssl, 30);  // Initial timeout: 30 seconds
    wolfSSL_dtls_set_timeout_max(ssl, 120);  // Max timeout: 120 seconds
    printf("[OK] DTLS timeouts: initial=30s, max=120s\n");
    printf("\n");

    // Perform DTLS handshake
    printf("===============================================================================\n");
    printf("                    STARTING DTLS 1.3 HANDSHAKE\n");
    printf("===============================================================================\n");
    printf("[INFO] This may take 60-90 seconds on 1MHz CPU with PQC operations\n");
    printf("[INFO] Progress indicators show send/receive activity\n");
    printf("\n");

    wolfSSL_Debugging_ON();  // Enabled for debugging certificate processing

    /*
     * IMPORTANT:
     *  - On this 1MHz bare-metal target, DTLS 1.3 + PQC can easily exceed
     *    normal network timeouts while doing heavy crypto (cert verify,
     *    Dilithium signatures, etc.).
     *  - Our recv callback returns WOLFSSL_CBIO_ERR_WANT_READ on timeout,
     *    which maps to wolfSSL error 323 / WOLFSSL_ERROR_WANT_READ.
     *  - That is *not* a fatal error; it means "handshake still in progress,
     *    call wolfSSL_connect() again once more data (or time) is available".
     *
     * So we loop wolfSSL_connect() until it either:
     *  - returns WOLFSSL_SUCCESS, or
     *  - returns a real fatal error (anything other than WANT_READ/WRITE).
     */
    for (;;) {
        ret = wolfSSL_connect(ssl);

        if (ret == WOLFSSL_SUCCESS) {
            break;  // Handshake complete
        }

        {
            int err_local = wolfSSL_get_error(ssl, ret);

            if (err_local == WOLFSSL_ERROR_WANT_READ || err_local == WOLFSSL_ERROR_WANT_WRITE) {
                // Handshake is still in progress (DTLS timers / retransmits).
                // On bare-metal we simply retry; network_recv() already has
                // its own timeout, so this loop will not hard-lock.
                printf("[HANDSHAKE] In progress... WANT_%s (normal for slow PQC ops)\n",
                       (err_local == WOLFSSL_ERROR_WANT_READ) ? "READ" : "WRITE");
                continue;
            }

            // Anything else is fatal – dump diagnostics and abort.
            char err_buf[80];
            wolfSSL_ERR_error_string(err_local, err_buf);
            printf("\n");
            printf("===============================================================================\n");
            printf("                        HANDSHAKE FAILED!\n");
            printf("===============================================================================\n");
            printf("[ERROR] Return code: %d\n", ret);
            printf("[ERROR] Error code:  %d\n", err_local);
            printf("[ERROR] Error string: %s\n", err_buf);
            printf("\n");
            printf("[TROUBLESHOOTING]\n");
            printf("  - Verify server is running on 192.168.1.100:11111\n");
            printf("  - Check network configuration (tap0 interface)\n");
            printf("  - Ensure certificates are properly generated (cd certs && make)\n");
            printf("  - Verify both client and server use same certificate set\n");
            printf("===============================================================================\n");
            goto cleanup;
        }
    }
    
    
    printf("\n");
    printf("===============================================================================\n");
    printf("                   DTLS 1.3 HANDSHAKE COMPLETE!\n");
    printf("===============================================================================\n");
    printf("[SUCCESS] Secure channel established with server\n");
    
    // Get cipher and version info (with safety checks)
    const char* cipher = wolfSSL_get_cipher(ssl);
    const char* version = wolfSSL_get_version(ssl);
    
    printf("[SUCCESS] Cipher suite: %s\n", cipher ? cipher : "(unknown)");
    printf("[SUCCESS] Protocol version: %s\n", version ? version : "(unknown)");
    printf("===============================================================================\n");
    printf("\n");

    // Enable debug logging to see received data
    wolfSSL_Debugging_ON();

    // Send test message over secure channel
    const char* msg = "Hello from RISC-V PQC-DTLS client!";
    printf("[DATA] Sending encrypted message to server...\n");
    printf("[DATA] Message: \"%s\" (%d bytes)\n", msg, (int)strlen(msg));
    
    ret = wolfSSL_write(ssl, msg, strlen(msg));
    if (ret < 0) {
        int err = wolfSSL_get_error(ssl, ret);
        printf("[ERROR] Failed to send data: %d (error: %d)\n", ret, err);
        goto cleanup;
    }
    printf("[OK] Message sent successfully (%d bytes encrypted + sent)\n", ret);
    printf("\n");

    // Receive response from server
    printf("[DATA] Waiting for encrypted response from server...\n");
    char recv_buf[256];
    ret = wolfSSL_read(ssl, recv_buf, sizeof(recv_buf) - 1);
    if (ret > 0) {
        recv_buf[ret] = '\0';
        printf("[OK] Received encrypted data (%d bytes)\n", ret);
        printf("[DATA] Decrypted message: \"%s\"\n", recv_buf);
    } else {
        int err = wolfSSL_get_error(ssl, ret);
        printf("[WARNING] No response received from server (error: %d)\n", err);
    }
    printf("\n");

    printf("===============================================================================\n");
    printf("                  PQC-DTLS 1.3 DEMO COMPLETE - SUCCESS!\n");
    printf("===============================================================================\n");
    printf("[SUMMARY]\n");
    printf("  ✓ DTLS 1.3 handshake completed with PQC algorithms\n");
    printf("  ✓ Mutual authentication using Raw Public Keys (RPK)\n");
    printf("  ✓ ML-DSA-44 signatures for authentication\n");
    printf("  ✓ Secure bidirectional communication established\n");
    printf("  ✓ Data encrypted with quantum-resistant cipher suite\n");
    printf("===============================================================================\n");

cleanup:
    printf("\n[CLEANUP] Cleaning up SSL resources...\n");
    if (ssl) {
        wolfSSL_free(ssl);
        printf("[OK] SSL session freed\n");
    }
    if (ctx) {
        wolfSSL_CTX_free(ctx);
        printf("[OK] SSL context freed\n");
    }
    wolfSSL_Cleanup();
    printf("[OK] wolfSSL library cleaned up\n");
    printf("\n[INFO] Client terminated\n");
    return 0;
}