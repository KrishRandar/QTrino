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

// Include embedded certificates
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

static int dtls_send_callback(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    (void)ssl;
    (void)ctx;
    
    int ret = network_send((uint8_t*)buf, sz);
    if (ret < 0) {
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
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
    printf("PQC-DTLS 1.3 Client - RISC-V Bare-Metal\n");
    printf("Algorithm: ML-KEM-512 + ML-DSA-44\n");
    printf("Protocol:  DTLS 1.3 (Pure PQC)\n");
    printf("Auth:      X.509 Mutual Authentication\n");

    // Initialize network layer
    network_init();

    // Initialize wolfSSL static memory pool 
    #ifdef WOLFSSL_STATIC_MEMORY
    // PQC requires large buckets: 64KB (×5) + 128KB (×1) = 448KB minimum
    // Plus certificates, keys, fragment reassembly, and runtime: need ~3MB+
    static unsigned char memory[4194304]; // 4MB for PQC + fragments
    static WOLFSSL_HEAP_HINT* heap_hint = NULL;
    
    ret = wc_LoadStaticMemory(&heap_hint, memory, sizeof(memory), WOLFMEM_GENERAL, 10);
    if (ret != 0) {
        printf("ERROR: Static memory init failed: %d\n", ret);
        return 1;
    }
    printf("[MEM] Static memory pool: %d bytes allocated\n", (int)sizeof(memory));
    #endif

    // Initialize wolfSSL library
    ret = wolfSSL_Init();
    if (ret != WOLFSSL_SUCCESS) {
        printf("ERROR: wolfSSL_Init failed: %d\n", ret);
        return 1;
    }

    // Enable debugging (if compiled with DEBUG_WOLFSSL)
#ifdef DEBUG_WOLFSSL
    // wolfSSL_Debugging_ON();  
#endif

    // Create DTLS 1.3 client context
    #ifdef WOLFSSL_STATIC_MEMORY
        ctx = wolfSSL_CTX_new_ex(wolfDTLSv1_3_client_method(), heap_hint);
    #else
        ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
    #endif
    if (ctx == NULL) {
        printf("ERROR: Failed to create DTLS context\n");
        goto cleanup;
    }

    // Set supported groups (ML-KEM-512)
    // ret = wolfSSL_CTX_set_groups_list(ctx, "ML-KEM-512");
    // if (ret != WOLFSSL_SUCCESS) {
    //     printf("ERROR: Failed to set groups list: %d\n", ret);
    //     goto cleanup;
    // }

    // Set I/O callbacks for bare-metal networking
    wolfSSL_CTX_SetIOSend(ctx, dtls_send_callback);
    wolfSSL_CTX_SetIORecv(ctx, dtls_recv_callback);

    // Load CA certificate for server verification
    if (ca_cert_der_len > 0) {
        ret = wolfSSL_CTX_load_verify_buffer(ctx, ca_cert_der, ca_cert_der_len,
                                             WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("ERROR: Failed to load CA cert: %d\n", ret);
            goto cleanup;
        }
    } else {
    }

    // Load client certificate for mutual authentication
    if (client_cert_der_len > 0) {
        ret = wolfSSL_CTX_use_certificate_buffer(ctx, client_cert_der,
                                                 client_cert_der_len,
                                                 WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("ERROR: Failed to load client cert: %d\n", ret);
            goto cleanup;
        }
    } else {
    }

    // Load client private key
    if (client_key_der_len > 0) {
        ret = wolfSSL_CTX_use_PrivateKey_buffer(ctx, client_key_der,
                                                client_key_der_len,
                                                WOLFSSL_FILETYPE_ASN1);
        if (ret != WOLFSSL_SUCCESS) {
            printf("ERROR: Failed to load client key: %d\n", ret);
            goto cleanup;
        }
    } else {
    }

    // Configure mutual authentication (if certificates are available)
    if (ca_cert_der_len > 0 && client_cert_der_len > 0) {
        wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER |
                                    WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    } else {
    }

    // Create SSL session
    ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        printf("ERROR: Failed to create SSL session\n");
        goto cleanup;
    }

    // Perform DTLS handshake
    printf("STARTING DTLS 1.3 HANDSHAKE\n");

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
                printf("[HANDSHAKE] wolfSSL_connect WANT_%s, retrying...\n",
                       (err_local == WOLFSSL_ERROR_WANT_READ) ? "READ" : "WRITE");
                continue;
            }

            // Anything else is fatal – dump diagnostics and abort.
            char err_buf[80];
            wolfSSL_ERR_error_string(err_local, err_buf);
            printf("\nERROR: Handshake failed!\n");
            printf("  Return code: %d\n", ret);
            printf("  Error code:  %d\n", err_local);
            printf("  Error string: %s\n", err_buf);
            goto cleanup;
        }
    }
    
    printf("DTLS 1.3 HANDSHAKE COMPLETE!\n");

    // Display cipher suite information

    // Send test message over secure channel
    const char* msg = "Hello from RISC-V PQC-DTLS client!";
    printf("DATA: Sending message: \"%s\"\n", msg);
    
    ret = wolfSSL_write(ssl, msg, strlen(msg));
    if (ret < 0) {
        int err = wolfSSL_get_error(ssl, ret);
        printf("ERROR: Send failed: %d (error: %d)\n", ret, err);
        goto cleanup;
    }

    // Receive response from server
    printf("DATA: Waiting for server response...\n");
    char recv_buf[256];
    ret = wolfSSL_read(ssl, recv_buf, sizeof(recv_buf) - 1);
    if (ret > 0) {
        recv_buf[ret] = '\0';
        printf("DATA: Received %d bytes: \"%s\"\n\n", ret, recv_buf);
    } else {
        int err = wolfSSL_get_error(ssl, ret);
    }

    printf("PQC-DTLS 1.3 DEMO COMPLETE - SUCCESS!\n");

cleanup:
    if (ssl) {
        wolfSSL_free(ssl);
    }
    if (ctx) {
        wolfSSL_CTX_free(ctx);
    }
    wolfSSL_Cleanup();    
    return 0;
}