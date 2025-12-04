# wolfSSL DTLS 1.3: PSK Resumption + Mutual Authentication Research

## Research Summary

Comprehensive research into wolfSSL's handling of Pre-Shared Key (PSK) session resumption combined with mutual certificate authentication in DTLS 1.3.

---

## Key Findings

### 1. **PSK Modes in TLS/DTLS 1.3**

wolfSSL TLS 1.3 supports two distinct PSK key exchange modes:

#### `psk_ke` (PSK-Only)
- **Authentication**: PSK only, **NO certificates**
- **Forward Secrecy**: ❌ None
- **Performance**: Fastest (no DH computation)
- **Use Case**: Resource-constrained devices, closed networks

#### `psk_dhe_ke` (PSK + Diffie-Hellman)
- **Authentication**: PSK + ephemeral DH key exchange
- **Forward Secrecy**: ✅ Yes (recommended by wolfSSL)
- **Performance**: Slower but still fast
- **Certificates**: Still **NO certificates** in standard mode
- **Use Case**: Production environments requiring forward secrecy

### 2. **Standard Behavior: PSK XOR Certificates**

**In standard DTLS 1.3, authentication is MUTUALLY EXCLUSIVE:**
- Either use certificates (X.509/RPK)
- OR use PSK
- **NOT both in the same handshake**

From research:
> "In the core DTLS 1.3 specification, authentication is typically achieved either through certificates (asymmetric cryptography) or PSKs (symmetric cryptography). These are generally mutually exclusive in a single handshake."

### 3. **Exception: RFC 8773/9492 Extension** 

There IS an IETF extension (RFC 8773, updated by RFC 9492) that allows **hybrid authentication**:
- Server authenticates with BOTH certificate + external PSK
- Designed for post-quantum security (defense in depth)
- Combines benefits of both authentication types

**⚠️ CRITICAL: No confirmation that wolfSSL implements RFC 8773/9492 for DTLS 1.3**

The research found:
> "However, as of the current search, direct evidence of wolfSSL's implementation of this specific RFC for DTLS 1.3 has not been found."

---

## Our Problem: Server Configuration Conflict

### Current Server Setup

```c
// Forces mutual certificate authentication
wolfSSL_CTX_set_verify(ctx, 
    WOLFSSL_VERIFY_PEER | WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, 
    verify_RPK_callback);
```

**This policy requires:**
1. Client MUST present a certificate
2. Certificate verification MUST succeed  
3. Handshake fails if no client cert

### What Happens with PSK Resumption

When client presents a valid PSK ticket:

1. ✅ Server decrypts ticket successfully
2. ✅ Server validates PSK binder  
3. ❌ **Server STILL demands client certificate** (due to `VERIFY_PEER`)
4. ❌ PSK-only mode rejected
5. ❌ Falls back to full handshake with certificates

**Root cause**: `WOLFSSL_VERIFY_PEER` policy overrides PSK-only authentication.

---

## Recommended Solution

### Pattern: Certificate for Initial, PSK for Resumption

From wolfSSL best practices:

> "A secure pattern involves using certificates for initial full handshakes and then leveraging PSKs for efficient session resumption."

**Implementation:**

1. **Connection #1 (Full Handshake)**:
   - Use mutual certificate authentication (RPK)
   - Verify both client and server identities
   - Server issues `NewSessionTicket`
   - Strong cryptographic binding established

2. **Connection #2+ (Resumed)**:
   - Client presents PSK from ticket
   - **Skip certificate verification** (identity already proven)
   - PSK authentication is sufficient (mutual authentication inherent in PSK)
   - Fast handshake without PQC operations

### How to Implement

**Option A: Conditional Verification (Recommended)**

Modify verify callback to skip verification when session is being resumed:

```c
int verify_RPK_callback_conditional(int preverify, WOLFSSL_X509_STORE_CTX* store) {
    WOLFSSL* ssl = wolfSSL_X509_STORE_CTX_get_ex_data(store, 
                        wolfSSL_get_ex_data_X509_STORE_CTX_idx());
    
    // Check if this is a resumed session
    if (wolfSSL_session_reused(ssl)) {
        // Session resumed with valid PSK - skip certificate verification
        printf("[RPK] Session resumed - skipping certificate verification\\n");
        return 1;  // Accept
    }
    
    // Full handshake - verify certificate as normal
    return verify_RPK_normal(preverify, store);
}
```

**Option B: Dynamic Verify Mode**

Set verify mode based on handshake type:

```c
// In server handshake code, after accepting connection
if (/* detect PSK is present */) {
    wolfSSL_set_verify(ssl, WOLFSSL_VERIFY_NONE, NULL);
} else {
    wolfSSL_set_verify(ssl, WOLFSSL_VERIFY_PEER | WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, 
                       verify_RPK_callback);
}
```

**Option C: Disable Peer Verification Entirely for Testing**

Temporary test to confirm hypothesis:

```c
// Comment out:
// wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_PEER | WOLFSSL_VERIFY_FAIL_IF_NO_PEER_CERT, ...);

// Replace with:
wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);
```

If this enables PSK resumption, we've confirmed the root cause.

---

## PSK Key Exchange Modes Extension

### What the Client Sends

In our logs:
```
PSK Key Exchange Modes extension to write
```

The client advertises which PSK modes it supports:
- `psk_ke` (PSK-only, no DH)
- `psk_dhe_ke` (PSK + DH)

### What the Server Chooses

The server selects which mode to use based on:
1. Client's advertised modes
2. Server's security policy
3. Cipher suite negotiated

**Currently**: Server appears to be rejecting BOTH modes and forcing full handshake with certificates.

### Potential Fix: Investigate PSK Mode Configuration

Check if server needs explicit configuration:

```c
// May need to add to server.c
wolfSSL_CTX_no_dhe_psk(ctx);  // Force psk_ke mode (no DH)
// OR ensure psk_dhe_ke is allowed
```

API found in `ssl.h`:
```c
WOLFSSL_API int wolfSSL_CTX_no_dhe_psk(WOLFSSL_CTX* ctx);
```

---

## DTLS 1.3 Cookie Exchange

### Current Behavior

Even with `WOLFSSL_DTLS13_NO_HRR_ON_RESUME` defined, we see:
```
HelloRetryRequest format
Cookie extension received
```

### What This Means

- `WOLFSSL_DTLS13_NO_HRR_ON_RESUME` **should** skip HRR cookie on resume
- But it's still happening
- This might be a secondary issue AFTER the cert verification problem

### Verification

Check if `wolfSSL_dtls13_no_hrr_on_resume()` API needs to be called:

```c
// May need to add per-connection:
wolfSSL_dtls13_no_hrr_on_resume(ssl, 1);
```

---

## Action Plan

### Phase 1: Confirm Root Cause ✅ NEXT STEP

1. **Disable peer verification temporarily**:
   ```c
   wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);
   ```

2. **Rebuild and test**:
   ```bash
   cd server && make
   ./server
   ```

3. **Expected outcome**:
   - Connection #2 should show PSK resumption
   - No certificate exchange
   - `wolfSSL_session_reused()` returns 1

### Phase 2: Implement Conditional Verification

If Phase 1 confirms hypothesis:

1. Create conditional verify callback
2. Check for resumed session
3. Skip cert verification on resume
4. Maintain security for initial handshake

### Phase 3: Verify PSK Mode Selection

1. Add logging for PSK mode negotiation
2. Confirm `psk_dhe_ke` or `psk_ke` is being used
3. Check if HRR is still triggered

### Phase 4: Production Hardening

1. Review security implications
2. Add comprehensive logging
3. Test edge cases (expired tickets, etc.)
4. Document configuration

---

## Security Considerations

### Is Skipping Cert Verification Safe?

**YES, when resuming with PSK:**

1. **Initial handshake** established mutual trust with certificates
2. **PSK derived from** that authenticated session
3. **Cryptographic binding**: PSK binder proves client has the secret from original session
4. **No identity change possible**: If client doesn't have valid PSK, handshake fails

From the research:
> "When using PSK, both the client and server prove possession of the shared secret key. This implicitly provides mutual authentication."

### Defense in Depth

The session ticket itself:
- Encrypted with server's secret key
- Contains session state from authenticated connection
- Cryptographically bound to client via PSK binder
- Has expiration time (5 minutes in our config)

**Conclusion**: PSK resumption maintains security without requiring certificate re-verification.

---

## API References

### Relevant wolfSSL APIs

```c
// Session management
wolfSSL_get1_session(ssl);
wolfSSL_set_session(ssl, session);
wolfSSL_session_reused(ssl);

// PSK configuration
wolfSSL_CTX_no_dhe_psk(ctx);
wolfSSL_CTX_UseSessionTicket(ctx);
wolfSSL_CTX_set_TicketHint(ctx, seconds);

// DTLS 1.3 specific 
wolfSSL_dtls13_no_hrr_on_resume(ssl, 1);
wolfSSL_send_hrr_cookie(ssl, secret, len);

// Verification control
wolfSSL_CTX_set_verify(ctx, mode, callback);
wolfSSL_set_verify(ssl, mode, callback);
```

---

## Sources

- [wolfSSL DTLS 1.3 Documentation](https://www.wolfssl.com/documentation/manuals/wolfssl/)
- RFC 8773: Outer Extensions for TLS 1.3 (TLS + external PSK)
- RFC 9492: Update to RFC 8773 for post-quantum security
- wolfSSL GitHub: Implementation references
- TLS 1.3 RFC 8446: PSK specifications

---

## Next Immediate Action

**Test Option C from "Recommended Solution":**

Temporarily disable peer verification to confirm that `WOLFSSL_VERIFY_PEER` is preventing PSK-only resumption.

See [SESSION_RESUMPTION_ANALYSIS.md](file:///home/neem/final/QTrino/SESSION_RESUMPTION_ANALYSIS.md) for full test results and evidence.
