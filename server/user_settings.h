#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

// Enable Server Support
#define WOLFSSL_SERVER
#undef NO_WOLFSSL_SERVER

// Enable DTLS 1.3
#define WOLFSSL_DTLS
#define WOLFSSL_DTLS13
#define WOLFSSL_TLS13
#define WOLFSSL_DTLS_CID
#define HAVE_TLS_EXTENSIONS
#define HAVE_SUPPORTED_CURVES        // Required for TLS 1.3 Groups (including PQC)
#define WOLFSSL_SEND_HRR_COOKIE
#define WOLFSSL_DTLS_CH_FRAG

// Enable PQC
#define WOLFSSL_MLKEM512
#define DILITHIUM_LEVEL2
#define HAVE_DILITHIUM               // Enable Dilithium support
#define WOLFSSL_WC_DILITHIUM         // Use internal WolfCrypt implementation
#define WOLFSSL_HAVE_MLKEM           // Enable ML-KEM support
#define WOLFSSL_WC_MLKEM             // Use internal WolfCrypt implementation
#define WOLFSSL_SHA3                 // Required for ML-KEM
#define WOLFSSL_SHAKE256             // Required for ML-KEM (SHAKE)
#define WOLFSSL_SHAKE128             // Required for ML-KEM (SHAKE)
#define WOLFSSL_SHAKE128             // Required for ML-KEM (SHAKE)

// Enable Cipher Suites
#define HAVE_AESGCM
#define HAVE_SHA256
#define HAVE_SHA384
#define WOLFSSL_AES_128              // Required for AES-128-GCM-SHA256
#define WOLFSSL_AES_128_GCM_SHA256   // TLS_AES_128_GCM_SHA256 (fallback)
// NOTE: AES-256 disabled - ChaCha20-Poly1305 preferred for software-only RISC-V performance

// Enable ChaCha20-Poly1305 (optimized for software-only RISC-V)
#define HAVE_CHACHA                  // ChaCha20 stream cipher
#define HAVE_POLY1305                // Poly1305 MAC
#define HAVE_AEAD                    // AEAD support

// Disable Classical Algorithms (Pure PQC)
#define NO_RSA
#define NO_DH
#define NO_ECC
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_OLD_TLS

// Standard System Features (Enable these for Host Server)
#define WOLFSSL_ASN_TEMPLATE         // ASN.1 template parsing (needed for RPK)
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME  // Disable HRR on resume
#define WOLFSSL_STATIC_PSK           // Bypass "No cipher suites" check and enable PSK logic
#define HAVE_HKDF                    // Required for TLS 1.3
#define HAVE_SYS_TIME_H              // Use system time functions (gettimeofday)

// Raw Public Key (RPK) Support - RFC 7250
// Replaces X.509 certificates with lightweight SubjectPublicKeyInfo
#define HAVE_RPK                     // Enable Raw Public Key authentication
#define WOLFSSL_ALWAYS_VERIFY_CB     // Always call verify callback (needed for RPK verification)

// Session Resumption Support
#define HAVE_SESSION_TICKET          // Enable session tickets for resumption
#define WOLFSSL_SESSION_EXPORT       // Allow exporting session data

// Debugging
#define DEBUG_WOLFSSL

#endif /* USER_SETTINGS_H */
