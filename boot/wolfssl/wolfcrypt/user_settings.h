#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

// WOLFCRYPT_ONLY removed - enabling full DTLS 1.3 support

#define WOLFSSL_SP_MATH // maths backend for crypto

#define NO_TIME_H
#define WOLFSSL_NO_CLOCK
#define NO_ASN_TIME  // Disable all time-related functions (gettimeofday, getitimer, etc.)
#define USER_TIME    // Use custom time functions (disables system time calls)

// Define ssize_t for bare-metal (not available in newlib-nano)
#ifndef _SSIZE_T_DECLARED
typedef long ssize_t;
#define _SSIZE_T_DECLARED
#endif

// Define time structures for bare-metal (required by internal.c)
#include <sys/time.h>
#define HAVE_SYS_TIME_H

// Function prototypes
unsigned int LowResTimer(void);

/* user_settings.h */
#define WOLFSSL_NO_SOCK
#define NO_WRITEV
#define WOLFSSL_USER_IO
#define WOLFSSL_SMALL_STACK         // Optimize for small stack usage
#define WOLFSSL_SMALL_CERT_VERIFY   // Lower memory certificate verification
#define NO_FILESYSTEM               // Don't use file system
#define NO_WOLFSSL_DIR              // Don't use directory access
#define NO_WOLFSSL_DIR              // Don't use directory access
#ifndef BUILDING_SERVER
    #define NO_WOLFSSL_SERVER       // Client only (disables server stateless code) - ONLY for firmware
#endif
#define SINGLE_THREADED             // No threading support needed
#define SINGLE_THREADED             // No threading support needed
#define NO_ERROR_STRINGS            // Save space by removing error strings
#define WOLFSSL_SMALL_SESSION_CACHE // Smaller session cache
#define WOLFSSL_STATIC_MEMORY       // Use static memory pools (crucial for 1MHz CPU!)
#define WOLFSSL_STATIC_MEMORY_SMALL_BUCKETS_ONLY  // Further optimize for embedded
#define NO_SIGNAL                   // Disable signal handling (sigaction)
#define NO_SIG_PIPE                 // Disable SIGPIPE
#define NO_SIGALRM                  // Disable SIGALRM
#define WOLFSSL_USE_ALIGN           // Fix unaligned access traps on RISC-V
#define WOLFSSL_GENERAL_ALIGNMENT 4 // Set alignment requirement
// ============= DTLS 1.3 SUPPORT =============
#define WOLFSSL_DTLS                 // Enable DTLS
#define WOLFSSL_DTLS13               // Enable DTLS 1.3
#define WOLFSSL_TLS13                // TLS 1.3 base (required)
#define WOLFSSL_DTLS_CID             // Connection ID support
#define WOLFSSL_SEND_HRR_COOKIE      // HelloRetryRequest cookies
// WOLFSSL_DTLS_CH_FRAG disabled - requires server stateless mode functions
#define HAVE_EXTENDED_MASTER         // Extended master secret
#define HAVE_TLS_EXTENSIONS          // TLS extensions support
#define WOLFSSL_CALLBACKS            // Enable callbacks
#define WOLFSSL_DTLS_ALLOW_FUTURE    // Allow future DTLS messages
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME  // Disable HRR on resume
#define WOLFSSL_STATIC_PSK           // Bypass "No cipher suites" check

// ============= DISABLE CLASSICAL ALGORITHMS (PURE PQC) =============
#define NO_RSA                       // Disable RSA
#define NO_DH                        // Disable DH
#define NO_ECC                       // Disable ECC
#define NO_DSA                       // Disable DSA
#define NO_DES3                      // Disable 3DES
#define NO_RC4                       // Disable RC4
#define NO_MD4                       // Disable MD4
#define NO_MD5                       // Disable MD5

// ============= ENABLE PQC & CIPHER SUITES =============
#define NO_OLD_TLS                   // Disable old TLS versions (TLS 1.2 and below)
#define WOLFSSL_MLKEM512             // ML-KEM-512
#define DILITHIUM_LEVEL2             // ML-DSA-44
#define HAVE_DILITHIUM               // Enable Dilithium support
#define WOLFSSL_WC_DILITHIUM         // Use internal WolfCrypt implementation
#define HAVE_SUPPORTED_CURVES        // Enable Supported Curves extension (required for PQC groups)

// Required for TLS 1.3 Cipher Suites
#define HAVE_AESGCM
#define HAVE_SHA256
#define HAVE_SHA384
#define WOLFSSL_AES_128_GCM_SHA256   // TLS_AES_128_GCM_SHA256
#define WOLFSSL_AES_256_GCM_SHA384   // TLS_AES_256_GCM_SHA384isable old TLS versions
// NOTE: ECC, X25519, ED25519 disabled by NOT defining HAVE_ECC, HAVE_X25519, etc.

// ============= REQUIRED HASH FUNCTIONS =============
#define WOLFSSL_SHA256               // General use
#define WOLFSSL_SHA512               // PQC operations

// ============= SYMMETRIC CRYPTO (DTLS AEAD) =============
#define HAVE_AESGCM                  // AES-GCM for DTLS records
#define HAVE_CHACHA                  // ChaCha20 alternative
#define HAVE_POLY1305                // Poly1305 MAC
#define HAVE_AEAD                    // AEAD support

// ============= ML-KEM-512 (Key Encapsulation) =============
#define WOLFSSL_WC_MLKEM
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_HAVE_KYBER           // Legacy name support
#define WOLFSSL_MLKEM512             // ML-KEM-512: NIST Level 1
#define WOLFSSL_SHA3                 // Required for ML-KEM and ML-DSA
#define WOLFSSL_SHAKE256             // Required for ML-KEM
#define WOLFSSL_SHAKE128             // Required for ML-KEM
#define WOLFSSL_MLKEM_ENCAPSULATE_SMALL_MEM
#define WOLFSSL_MLKEM_MAKEKEY_SMALL_MEM

// ============= ML-DSA-44 (Digital Signatures) =============
#define WOLFSSL_WC_DILITHIUM
#define WOLFSSL_HAVE_DILITHIUM
#define DILITHIUM_LEVEL2             // ML-DSA-44: NIST Level 2
#define WOLFSSL_DILITHIUM_MAKE_KEY_SMALL_MEM
#define WOLFSSL_DILITHIUM_SIGN_SMALL_MEM
#define WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM

// ============= X.509 CERTIFICATE SUPPORT =============
#define WOLFSSL_CERT_GEN             // Certificate generation
#define WOLFSSL_CERT_EXT             // Certificate extensions
#define WOLFSSL_ASN_TEMPLATE         // ASN.1 template parsing
#define HAVE_PKCS7                   // PKCS#7 support

// ============= KEY DERIVATION =============
#define HAVE_HKDF                    // HKDF for key derivation
#define WOLFSSL_KEY_GEN              // Key generation support

// debug support
#define DEBUG_WOLFSSL
#define SHOW_GEN
#define DEBUG_WOLFSSL_VERBOSE

extern int CustomRngGenerateBlock(unsigned char *, unsigned int);
#define CUSTOM_RAND_GENERATE_SEED CustomRngGenerateBlock

#endif // USER_SETTINGS_H