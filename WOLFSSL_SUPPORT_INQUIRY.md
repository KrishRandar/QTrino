# wolfSSL Support Inquiry: DTLS 1.3 PSK Session Resumption Issue

## Summary

We are attempting to implement DTLS 1.3 session resumption using PSK (from session tickets) with Raw Public Key (RPK) authentication, but the HelloRetryRequest (HRR) cookie exchange continues to occur even after following all documented procedures for `WOLFSSL_DTLS13_NO_HRR_ON_RESUME`.

---

## Environment

### Platform
- **Client**: RISC-V VexRiscv bare-metal (1MHz simulated CPU)
- **Server**: Linux (Ubuntu)
- **wolfSSL Version**: [Please specify your version]
- **Language**: C

### Configuration Defines
```c
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME  // Defined in user_settings.h
#define WOLFSSL_SEND_HRR_COOKIE          // Required for DTLS 1.3
#define WOLFSSL_DTLS
#define WOLFSSL_TLS13
#define HAVE_SESSION_TICKET
#define HAVE_RPK                          // Raw Public Keys (RFC 7250)
// Plus PQC algorithms: ML-KEM-512, ML-DSA-44
```

---

## What We've Implemented

### ✅ Working Components

1. **DTLS 1.3 Handshake**: Full handshake with PQC algorithms works correctly
2. **RPK Mutual Authentication**: Both parties authenticate using Raw Public Keys
3. **Session Tickets**: Server issues tickets, client receives and stores them
4. **Ticket Encryption/Decryption**: Tickets are successfully encrypted and decrypted
5. **PSK Derivation**: PSK from tickets is correctly derived
6. **Multi-Connection Server**: Server handles multiple connections preserving ticket keys

### ❌ Not Working: PSK Session Resumption

Despite implementing all documented requirements, session resumption still performs a full handshake with HRR cookie exchange.

---

## Implementation Steps Taken

### 1. Compile-Time Configuration
```c
// In user_settings.h (both client and server)
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME
```

### 2. Runtime API Calls
```c
// Server code - per SSL session
ret = wolfSSL_dtls13_no_hrr_on_resume(ssl, 1);
if (ret != WOLFSSL_SUCCESS) {
    // Handle error
}
```

### 3. Session Ticket Configuration
```c
// Server
wolfSSL_CTX_UseSessionTicket(ctx);
wolfSSL_CTX_set_TicketHint(ctx, 300);  // 5 minutes

// Client
wolfSSL_CTX_UseSessionTicket(ctx);
```

### 4. Session Save/Restore
```c
// Client - Save after connection #1
saved_session = wolfSSL_get1_session(ssl);

// Client - Restore for connection #2
wolfSSL_set_session(ssl, saved_session);
```

### 5. Attempted: Disable wolfSSL_send_hrr_cookie
```c
// Tried commenting out this call - HRR still occurs!
// wolfSSL_send_hrr_cookie(ssl, secret, len);
```

---

## Observed Behavior

### Connection #1 (Expected - Full Handshake)
```
Client sends: ClientHello (no PSK)
Server sends: HelloRetryRequest + Cookie
Client sends: ClientHello + Cookie
Server sends: ServerHello + Certificate + CertificateVerify + Finished
Client sends: Certificate + CertificateVerify + Finished
Server sends: NewSessionTicket
Result: Full handshake ✅ (~60-90 seconds)
```

### Connection #2 (Expected - PSK Resumption, Actual - Full Handshake)
```
Client sends: ClientHello + PSK (from ticket)
Server: Decrypts ticket successfully ✅
Server: CheckPreSharedKeys returns 0 ✅
Server sends: HelloRetryRequest + Cookie ❌ (Should skip!)
Client sends: ClientHello + Cookie + PSK
Server sends: ServerHello + Certificate + CertificateVerify + Finished ❌
Result: Full handshake instead of PSK resume (~60-90 seconds)
```

### Key Evidence from Logs

**Server logs show**:
```
wolfSSL Entering DoClientTicket_ex
wolfSSL Entering DoDecryptTicket
wolfSSL Leaving DoClientTicket_ex, return 0  ← Ticket valid!
wolfSSL Leaving CheckPreSharedKeys, return 0  ← PSK validated!
...but then...
Cookie extension to write  ← HRR still sent!
```

**Client logs show**:
```
Pre-Shared Key extension to write
wolfSSL Entering WritePSKBinders
...
HelloRetryRequest format  ← HRR received
Cookie extension received
...full handshake continues
```

---

## Questions for wolfSSL Support

1. **Is this configuration supported?**
   - DTLS 1.3 stateless server
   - PSK session resumption (with `WOLFSSL_DTLS13_NO_HRR_ON_RESUME`)
   - RPK authentication for initial handshake
   - Post-Quantum Cryptography (ML-KEM-512, ML-DSA-44)

2. **Why does HRR cookie exchange still occur?**
   - We've set the compile flag
   - We've called the runtime API
   - We've even tried disabling `wolfSSL_send_hrr_cookie()`
   - Ticket is decrypted and PSK validated successfully
   - But HRR is still sent

3. **Is there something we're missing?**
   - Additional configuration required?
   - Specific order of API calls?
   - Incompatibility with RPK + PSK combination?
   - Version-specific limitation?

4. **Working alternative?**
   - Should we use a stateful server instead of stateless?
   - Is there a different approach to achieve fast resumption with DTLS 1.3 + RPK?
   - Would 0-RTT Early Data work better in this scenario?

---

## Code Snippets

### Server Session Setup
```c
// Create SSL context
ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());

// Configure session tickets
wolfSSL_CTX_UseSessionTicket(ctx);
wolfSSL_CTX_set_TicketHint(ctx, 300);

// Configure RPK
wolfSSL_CTX_use_RPK_certificate_file(ctx, "server_pub.der", WOLFSSL_FILETYPE_ASN1);
wolfSSL_CTX_use_RPK_PrivateKey_file(ctx, "server_priv.der", WOLFSSL_FILETYPE_ASN1);

// Per-session configuration
ssl = wolfSSL_new(ctx);
wolfSSL_dtls13_no_hrr_on_resume(ssl, 1);  // Enable PSK resume without HRR

// Note: We tried both WITH and WITHOUT this call:
// wolfSSL_send_hrr_cookie(ssl, secret, len);
```

### Client Session Management
```c
// First connection
ssl1 = wolfSSL_new(ctx);
wolfSSL_connect(ssl1);
saved_session = wolfSSL_get1_session(ssl1);  // Save after receiving ticket
wolfSSL_free(ssl1);

// Second connection  
ssl2 = wolfSSL_new(ctx);
wolfSSL_set_session(ssl2, saved_session);  // Restore for resumption
wolfSSL_connect(ssl2);  // Should resume with PSK, but full handshake occurs
```

---

## Expected vs Actual

### Expected (PSK Resumption)
```
Client → Server: ClientHello + PSK
Server: Validates PSK, skips HRR due to dtls13NoHrrOnResume
Server → Client: ServerHello + Finished (no certificates!)
Client → Server: Finished
Time: ~5-10 seconds
```

### Actual (Still Full Handshake)
```
Client → Server: ClientHello + PSK
Server: Validates PSK ✅... but ignores dtls13NoHrrOnResume flag
Server → Client: HelloRetryRequest + Cookie
...full certificate exchange...
Time: ~60-90 seconds
```

---

## Additional Context

### Why We Need This
- **Performance**: 8-9x speedup (5-10s vs 60-90s)
- **Resource Constraints**: 1MHz CPU needs faster handshakes
- **PQC Overhead**: ML-KEM-512 key exchange is expensive
- **Production Use**: Frequent reconnections in embedded IoT scenario

### What We've Researched
1. wolfSSL documentation on DTLS 1.3 session resumption
2. Source code analysis of `dtls.c`, `tls13.c`, `dtls13.c`
3. RFC 9147 (DTLS 1.3) and RFC 8446 (TLS 1.3) PSK modes
4. wolfSSL GitHub issues and forum discussions

---

## Request

Could you please advise on:
1. Whether this configuration should work
2. What we might be missing
3. Alternative approaches to achieve fast session resumption with DTLS 1.3 + RPK

We'd be happy to provide additional logs, code snippets, or test cases as needed.

---

## Contact Information

- **Project**: QTrino (RISC-V Embedded DTLS 1.3 with PQC)
- **GitHub**: QTrino-Labs-Pvt-Ltd
- **Priority**: High (blocking production deployment)

Thank you for your assistance!
