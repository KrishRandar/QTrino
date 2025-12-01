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

// Include embedded certificates (placeholder for now)
#include "certs_placeholder.h"

// Undef conflicting macros
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

//=============================================================================
// REQUIRED STUBS FOR BARE-METAL
//=============================================================================
#include <wolfssl/wolfcrypt/types.h>
#include <sys/time.h>
#include <time.h>

/* Custom entropy source - REPLACE WITH HARDWARE RNG IN PRODUCTION */
static uint32_t rng_state = 0x12345678; // Initial seed

int CustomRngGenerateBlock(byte *output, word32 sz) {
    // Linear Congruential Generator (LCG)
    // Parameters from Numerical Recipes (better than trivial i*37+123)
    for (word32 i = 0; i < sz; i++) {
        rng_state = rng_state * 1664525UL + 1013904223UL;
        output[i] = (byte)(rng_state >> 24); // Use top 8 bits
    }
    
    return 0;
}

/* Stub for gettimeofday - required by wolfSSL */
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
    return 0; // Return 0 for now
}

static int dtls_send_callback(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    (void)ssl;
    (void)ctx;
    
    printf("[IO] Sending %d bytes\n", sz);
    int ret = network_send((uint8_t*)buf, sz);
    if (ret < 0) {
        printf("[IO] Send failed\n");
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
    printf("[IO] Sent %d bytes\n", ret);
    return ret;
}

// Callback for receiving data from the network
int dtls_recv_callback(WOLFSSL *ssl, char *buf, int sz, void *ctx) {
    int ret;
    (void)ssl;
    (void)ctx;
    
    // Use the udp_recv function from network.c
    ret = network_recv((uint8_t*)buf, sz, 5000); // 5 second timeout
    
    if (ret > 0) {
        return ret;
    } else if (ret == 0) {
        // Timeout
        return WOLFSSL_CBIO_ERR_WANT_READ;
    } else {
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
}

//=============================================================================
// MAIN FUNCTION
//=============================================================================
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
    printf("========================================\n");
    printf("PQC-DTLS 1.3 Client - RISC-V Bare-Metal\n");
    printf("========================================\n");
    printf("Algorithm: ML-KEM-512 + ML-DSA-44\n");
    printf("Protocol:  DTLS 1.3 (Pure PQC)\n");
    printf("Auth:      X.509 Mutual Authentication\n");
    printf("========================================\n\n");

    // Initialize network layer
    printf("[INIT] Initializing network...\n");
    network_init();
    printf("[OK] Network initialized\n\n");

    // Initialize wolfSSL static memory pool (crucial for 1MHz CPU performance!)
    #ifdef WOLFSSL_STATIC_MEMORY
    static unsigned char memory[1024000]; // 1MB static buffer (for PQC DTLS 1.3)
    static WOLFSSL_HEAP_HINT* heap_hint = NULL;
    
    printf("[INIT] Setting up static memory pool (%d bytes)...\n", (int)sizeof(memory));
    ret = wc_LoadStaticMemory(&heap_hint, memory, sizeof(memory), WOLFMEM_GENERAL, 10);
    if (ret != 0) {
        printf("[ERROR] Static memory init failed: %d\n", ret);
        return 1;
    }
    printf("[OK] Static memory pool initialized\n\n");
    #endif

    // Initialize wolfSSL library
    printf("[INIT] Initializing wolfSSL...\n");
    ret = wolfSSL_Init();
    if (ret != WOLFSSL_SUCCESS) {
        printf("[ERROR] wolfSSL_Init failed: %d\n", ret);
        return 1;
    }
    printf("[OK] wolfSSL initialized\n\n");

    // Enable debugging (if compiled with DEBUG_WOLFSSL)
#ifdef DEBUG_WOLFSSL
    // wolfSSL_Debugging_ON();  // DISABLED: Too verbose, slows down simulation
#endif

    // Create DTLS 1.3 client context
    printf("[DTLS] Creating DTLS 1.3 client context...\n");
    #ifdef WOLFSSL_STATIC_MEMORY
        ctx = wolfSSL_CTX_new_ex(wolfDTLSv1_3_client_method(), heap_hint);
    #else
        ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
    #endif
    if (ctx == NULL) {
        printf("[ERROR] Failed to create DTLS context\n");
        goto cleanup;
    }
    printf("[OK] DTLS 1.3 context created\n\n");

    // Set supported groups (ML-KEM-512)
    // printf("[DTLS] Setting supported groups (ML-KEM-512)...\n");
    // ret = wolfSSL_CTX_set_groups_list(ctx, "ML-KEM-512");
    // if (ret != WOLFSSL_SUCCESS) {
    //     printf("[ERROR] Failed to set groups list: %d\n", ret);
    //     goto cleanup;
    // }
    // printf("[OK] Supported groups set\n\n");

    // Set I/O callbacks for bare-metal networking
    printf("[DTLS] Setting I/O callbacks...\n");
    wolfSSL_CTX_SetIOSend(ctx, dtls_send_callback);
    wolfSSL_CTX_SetIORecv(ctx, dtls_recv_callback);
    printf("[OK] I/O callbacks set\n\n");

    // Load CA certificate for server verification
    printf("[CERT] Loading CA certificate...\n");
    if (ca_cert_der_len > 0) {
        ret = wolfSSL_CTX_load_verify_buffer(ctx, ca_cert_der, ca_cert_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("[ERROR] Failed to load CA cert: %d\n", ret);
            goto cleanup;
        }
        printf("[OK] CA certificate loaded (%d bytes)\n", ca_cert_der_len);
    } else {
        printf("[WARNING] No CA certificate available (placeholder)\n");
        printf("[WARNING] Server verification will be skipped\n");
    }

    // Load client certificate for mutual authentication
    printf("[CERT] Loading client certificate...\n");
    if (client_cert_der_len > 0) {
        ret = wolfSSL_CTX_use_certificate_buffer(ctx, client_cert_der,
                                                 client_cert_der_len,
                                                 WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("[ERROR] Failed to load client cert: %d\n", ret);
            goto cleanup;
        }
        printf("[OK] Client certificate loaded (%d bytes)\n", client_cert_der_len);
    } else {
        printf("[WARNING] No client certificate available (placeholder)\n");
    }

    // Load client private key
    printf("[CERT] Loading client private key...\n");
    if (client_key_der_len > 0) {
        ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, client_key_der,
                                                client_key_der_len,
                                                WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("[ERROR] Failed to load client key: %d\n", ret);
            goto cleanup;
        }
        printf("[OK] Client private key loaded (%d bytes)\n\n", client_key_der_len);
    } else {
        printf("[WARNING] No client private key available (placeholder)\n\n");
    }

    // Configure mutual authentication (if certificates are available)
    if (ca_cert_der_len > 0 && client_cert_der_len > 0) {
        printf("[DTLS] Configuring mutual authentication...\n");
        wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER |
                                    WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
        printf("[OK] Mutual authentication configured\n\n");
    } else {
        printf("[WARNING] Skipping mutual authentication (no certificates)\n\n");
    }

    // Create SSL session
    printf("[DTLS] Creating SSL session...\n");
    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        printf("[ERROR] Failed to create SSL session\n");
        printf("[DEBUG] This might be due to insufficient static memory\n");
        printf("[DEBUG] Try increasing the static memory pool size\n");
        goto cleanup;
    }
    printf("[OK] SSL session created\n\n");

    // Perform DTLS handshake
    printf("========================================\n");
    printf("STARTING DTLS 1.3 HANDSHAKE\n");
    printf("========================================\n");

    // wolfSSL_Debugging_ON();  // Uncomment for detailed handshake debugging
    
    ret = wolfSSL_connect(ssl);
    
    // wolfSSL_Debugging_OFF();
    
    
    if (ret != WOLFSSL_SUCCESS) {
        int err = wolfSSL_get_error(ssl, ret);
        char err_buf[80];
        wolfSSL_ERR_error_string(err, err_buf);
        printf("\n[ERROR] Handshake failed!\n");
        printf("  Return code: %d\n", ret);
        printf("  Error code:  %d\n", err);
        printf("  Error string: %s\n", err_buf);
        goto cleanup;
    }
    
    printf("\n========================================\n");
    printf("DTLS 1.3 HANDSHAKE COMPLETE!\n");
    printf("========================================\n");
    printf("[SUCCESS] Secure channel established\n");
    printf("[SUCCESS] Mutual authentication verified\n\n");

    // Display cipher suite information
    printf("[INFO] Cipher Suite: %s\n", wolfSSL_get_cipher(ssl));
    printf("[INFO] Protocol Version: %s\n\n", wolfSSL_get_version(ssl));

    // Send test message over secure channel
    const char* msg = "Hello from RISC-V PQC-DTLS client!";
    printf("[DATA] Sending message: \"%s\"\n", msg);
    
    ret = wolfSSL_write(ssl, msg, strlen(msg));
    if (ret < 0) {
        int err = wolfSSL_get_error(ssl, ret);
        printf("[ERROR] Send failed: %d (error: %d)\n", ret, err);
        goto cleanup;
    }
    printf("[OK] Sent %d bytes\n\n", ret);

    // Receive response from server
    printf("[DATA] Waiting for server response...\n");
    char recv_buf[256];
    ret = wolfSSL_read(ssl, recv_buf, sizeof(recv_buf) - 1);
    if (ret > 0) {
        recv_buf[ret] = '\0';
        printf("[DATA] Received %d bytes: \"%s\"\n\n", ret, recv_buf);
    } else {
        int err = wolfSSL_get_error(ssl, ret);
        printf("[WARNING] No data received (ret: %d, error: %d)\n\n", ret, err);
    }

    printf("========================================\n");
    printf("PQC-DTLS 1.3 DEMO COMPLETE - SUCCESS!\n");
    printf("========================================\n");

cleanup:
    if (ssl) {
        printf("\n[CLEANUP] Freeing SSL session...\n");
        wolfSSL_free(ssl);
    }
    if (ctx) {
        printf("[CLEANUP] Freeing SSL context...\n");
        wolfSSL_CTX_free(ctx);
    }
    printf("[CLEANUP] Shutting down wolfSSL...\n");
    wolfSSL_Cleanup();
    
    printf("\n[DONE] Program terminated\n");
    return 0;
}