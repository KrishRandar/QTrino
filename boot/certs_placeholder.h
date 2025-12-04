/*
 * RPK Keys for PQC-DTLS 1.3 Mutual Authentication
 * 
 * Algorithm: ML-DSA-44 (NIST FIPS 204)
 * Authentication: Raw Public Keys (RFC 7250)
 * 
 * Contains:
 *   - client_key_der[]    : Our ML-DSA-44 private key for signing
 *   - client_pubkey_der[] : Our public key (sent as RPK)
 *   - server_pubkey_der[] : Server's public key (for verification)
 */

#include "../certs/client_rpk.h"
