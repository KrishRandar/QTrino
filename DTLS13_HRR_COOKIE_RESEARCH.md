# DTLS 1.3 Session Resumption: HRR Cookie Exchange Research

## Critical Discovery

The issue is **NOT** with peer verification - that was a red herring. The real blocker is the **DTLS 1.3 HelloRetryRequest (HRR) cookie exchange** being enforced even with a valid PSK.

---

## How `WOLF SSL_DTLS13_NO_HRR_ON_RESUME` Works

### 1. **Compilation Flag** (Already Done ✅)

Defined in both `user_settings.h` files:
```c
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME
```

This enables the feature at **compile-time**, making the API available.

### 2. **Runtime API Call** (MISSING ❌)

**Must** call the API for each SSL session:
```c
int wolfSSL_dtls13_no_hrr_on_resume(WOLFSSL *ssl, int enabled);
```

**Location in code**: `boot/wolfssl/src/dtls13.c:3058`

**How it works**:
```c
int wolfSSL_dtls13_no_hrr_on_resume(WOLFSSL *ssl, int enabled)
{
    if (ssl->options.side == WOLFSSL_CLIENT_END) {
        return WOLFSSL_FAILURE;  // Only for SERVER!
    }
    ssl->options.dtls13NoHrrOnResume = !!enabled;  // Sets flag
    return WOLFSSL_SUCCESS;
}
```

**Sets internal flag**: `ssl->options.dtls13NoHrrOnResume = 1`

---

## Where The Flag is Checked

### Location: `tls13.c:7297-7302`

```c
#ifdef WOLFSSL_DTLS13_NO_HRR_ON_RESUME
    /* We can skip cookie on resumption */
    if (!ssl->options.dtls || !ssl->options.dtls13NoHrrOnResume ||
            !args->usingPSK)
#endif
        ERROR_OUT(BAD_HELLO, exit_dch);
```

**Logic**:
- If `dtls13NoHrrOnResume` is **TRUE** AND PSK is being used (`usingPSK`)
- Then: **Skip the HRR cookie requirement**
- Otherwise: Send `BAD_HELLO` error (forces full handshake)

### The Flow

```
DoTls13ClientHello()
  └─> Check if cookie is present
        ├─> Cookie missing AND sendCookie enabled
        │     ├─> Check: dtls13NoHrrOnResume flag
        │     │     ├─> TRUE + usingPSK → SKIP HRR (allow resumption)
        │     │     └─> FALSE → ERROR_OUT(BAD_HELLO) → full handshake
        │     └─> Send HelloRetryRequest with cookie
        └─> Cookie present → Validate and proceed
```

---

## Why It's Failing Now

### Current Situation

1. ✅ `WOLFSSL_DTLS13_NO_HRR_ON_RESUME` is **defined** (compile-time)
2. ❌ `wolfSSL_dtls13_no_hrr_on_resume(ssl, 1)` is **NOT called** (runtime)
3. ❌ `ssl->options.dtls13NoHrrOnResume` remains **FALSE** (default)

### What Happens

```
Client sends PSK in ClientHello
  ↓
Server: CheckPreSharedKeys() → Ticket decrypted ✅
  ↓
Server: Cookie check → NO cookie present
  ↓
Server: Check dtls13NoHrrOnResume flag → FALSE ❌
  ↓
Server: ERROR_OUT(BAD_HELLO)
  ↓  
Server: Sends HelloRetryRequest with cookie
  ↓
Client: Must do full handshake with certificates
```

**Result**: PSK resumption **blocked** by cookie requirement!

---

## The Fix

### Server Code Modification

**File**: `server/server.c`

**Add AFTER creating SSL session** (around line 400):

```c
// Create SSL session
ssl = wolfSSL_new(ctx);
if (!ssl) {
    fprintf(stderr, "[ERROR] Failed to create SSL session\n");
    continue;
}
printf("[OK] SSL session created\n");

// ========== CRITICAL FOR PSK RESUMPTION ==========
// Skip HRR cookie exchange when client presents valid PSK
#ifdef WOLFSSL_DTLS13_NO_HRR_ON_RESUME
ret = wolfSSL_dtls13_no_hrr_on_resume(ssl, 1);
if (ret != WOLFSSL_SUCCESS) {
    fprintf(stderr, "[WARNING] Failed to set no-HRR-on-resume: %d\n", ret);
}
else {
    printf("[SESSION] HRR cookie exchange will be skipped on PSK resumption\n");
}
#else
printf("[WARNING] WOLFSSL_DTLS13_NO_HRR_ON_RESUME not defined!\n");
printf("[WARNING] Session resumption may not work properly\n");
#endif
```

### Why This Works

1. Sets `ssl->options.dtls13NoHrrOnResume = 1`
2. When client sends PSK, server checks this flag
3. Server skips HRR cookie requirement
4. PSK-only handshake proceeds **without** forcing full cert exchange

---

## Additional Requirements

### None! 

The compilation flag is already defined, and the runtime API call is all that's needed.

**Verification**:
```bash
# Check if flag is defined
grep -r "WOLFSSL_DTLS13_NO_HRR_ON_RESUME" boot/wolfssl/wolfcrypt/user_settings.h
grep -r "WOLFSSL_DTLS13_NO_HRR_ON_RESUME" server/user_settings.h
```

Both should show:
```
#define WOLFSSL_DTLS13_NO_HRR_ON_RESUME  // Disable HRR on resume
```

---

## Security Implications

### Is It Safe to Skip Cookie Exchange on Resume?

**YES**, for these reasons:

1. **PSK Cryptographic Binding**: The PSK binder proves the client has the secret from the original session
2. **Ticket Encryption**: Session ticket is encrypted with server's secret key
3. **Initial Auth Already Done**: First handshake verified both parties
4. **DoS Protection Not Needed**: PSK verification itself proves client legitimacy

From wolfSSL documentation:
> "In certain scenarios, such as when a client presents a valid session ticket or Pre-Shared Key (PSK), this cookie exchange can be omitted to improve performance."

### What About 0-RTT Early Data?

The research mentioned that skipping HRR is a **prerequisite** for DTLS 1.3 Early Data (0-RTT). However:
- We're not using Early Data currently
- Standard PSK resumption (1-RTT) is sufficient
- Still achieves 8-9x speedup without Early Data complexity

---

## Expected Behavior After Fix

###  Connection #1 (Full Handshake)

```
Server receives ClientHello
  ↓
NO PSK present
  ↓
Send HRR with cookie (DoS protection)
  ↓
Client returns with cookie
  ↓
Full handshake: Certificates + Signatures
  ↓
Server sends NewSessionTicket
```

### ✅ Connection #2 (PSK Resumption)

```
Server receives ClientHello with PSK
  ↓  
PSK validated ✅
  ↓
Check: dtls13NoHrrOnResume = TRUE ✅
  ↓
SKIP HRR cookie exchange
  ↓
PSK-only handshake: NO certificates!
  ↓
Derive keys from PSK
  ↓
Complete in ~5-10 seconds (vs 60-90s)
```

---

## Implementation Steps

### Option A: Complete Fix (Recommended)

1. **Restore peer verification** (was disabled for testing)
2. **Add `wolf SSL_dtls13_no_hrr_on_resume()` call**
3. Rebuild and test

### Option B: Minimal Test (Quick)

1. Keep peer verification disabled (current state)
2. **Add `wolfSSL_dtls13_no_hrr_on_resume()` call**
3. Rebuild and test
4. If successful, restore peer verification

---

## API Reference

### Function Signature

```c
int wolfSSL_dtls13_no_hrr_on_resume(WOLFSSL *ssl, int enabled);
```

**Parameters**:
- `ssl`: The WOLFSSL object (server-side only)
- `enabled`: 1 to skip HRR on resume, 0 to require it

**Returns**:
- `WOLFSSL_SUCCESS` on success
- `WOLFSSL_FAILURE` if called on client

**Requirements**:
- Must be compiled with `WOLFSSL_DTLS13_NO_HRR_ON_RESUME` ✅
- Must be called on **server** SSL object only
- Should be called **before** handshake

---

## Testing the Fix

### What to Look For

**Server logs should show**:
```
[SESSION] HRR cookie exchange will be skipped on PSK resumption
...
wolfSSL Leaving CheckPreSharedKeys, return 0
wolfSSL Leaving DoTls13ClientHello, return 0
```

**NO** `HelloRetryRequest format` on connection #2!

**Server should show**:
```
wolfSSL Leaving wolfSSL_session_reused, return 1
[SESSION] ✓ Session RESUMED from client ticket
```

**Client should show**:
```
wolfSSL Leaving wolfSSL_session_reused, return 1  
[SESSION] ✓ Session RESUMED successfully!
```

---

## Sources

- [wolfSSL DTLS 1.3 Documentation](https://www.wolfssl.com/documentation/)
- wolfSSL source code: `src/dtls13.c` and `src/tls13.c`
- Web search: wolfSSL DTLS 1.3 session resumption requirements

---

##  Next Action

**Add the API call to server.c and test!**

This should finally enable PSK-based session resumption without the HRR cookie blocking it.
