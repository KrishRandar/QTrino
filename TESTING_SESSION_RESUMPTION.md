# Testing Session Resumption

This guide shows how to test DTLS 1.3 session resumption with multiple connections.

## Quick Test: Manual Modification

### Option 1: Simple Test Loop (Recommended)

Add this at the very end of `main()`, just before the `cleanup:` label (around line 500):

```c
    printf("===============================================================================\n");
    
    // ===== COPY EVERYTHING FROM LINE 345 TO HERE, PASTE BELOW =====
    // This will create a second connection using the saved session
    
    printf("\n\n");
    printf("===============================================================================\n");
    printf("              SECOND CONNECTION - TESTING RESUMPTION\n");
    printf("===============================================================================\n");
    
    // Free current SSL (but keep context and saved_session)
    wolfSSL_shutdown(ssl);
    wolfSSL_free(ssl);
    
    // Recreate SSL and repeat from "Create SSL session" onwards
    // The existing code will auto-detect connection_count > 1
    // and attempt resumption!
```

### Option 2: Proper Loop Structure

For a cleaner approach, wrap the connection logic in a loop. Change line 345 from:

```c
    // Create SSL session
```

To:

```c
    // Test session resumption with 2 connections
    for (int conn = 0; conn < 2; conn++) {
        if (conn > 0) {
            printf("\n\n=== CONNECTION #%d - RESUMPTION TEST ===\n\n", conn + 1);
        }
        
        // Create SSL session
```

Then before `cleanup:` label, add:

```c
        // Cleanup for next iteration
        if (conn < 1) {  // Not last connection
            wolfSSL_shutdown(ssl);
            wolfSSL_free(ssl);
            ssl = NULL;
            
            // Brief delay
            for (volatile int i = 0; i < 2000000; i++);
        }
    } // End connection loop
```

## Expected Output

### First Connection (Full Handshake)
```
[SESSION] Connection #1
[SESSION] First connection - will perform full handshake
[INFO] Session will be saved for future resumption

[DTLS] STARTING DTLS 1.3 HANDSHAKE
[INFO] This may take 60-90 seconds on 1MHz CPU with PQC operations

... (long handshake) ...

[SESSION] Full handshake performed
[SESSION] ✓ Session saved for future resumption
[INFO] Next connection can use session resumption
```

### Second Connection (Resumed)
```
[SESSION] Connection #2
[SESSION] Attempting to resume previous session...
[OK] Session configured for resumption
[INFO] Handshake should be much faster (no PQC key exchange)

[DTLS] STARTING DTLS 1.3 HANDSHAKE

... (MUCH FASTER handshake - ~5-10 seconds) ...

[SESSION] ✓ Session RESUMED successfully!
[PERF] Skipped expensive PQC key exchange (ML-KEM-512)
[PERF] Skipped signature generation/verification (ML-DSA-44)
```

## Performance Comparison

| Metric | Connection #1 | Connection #2 | Improvement |
|--------|---------------|---------------|-------------|
| Time | 60-90 seconds | 5-10 seconds | **~8-9x faster** |
| ML-KEM ops | 1 | 0 | 100% reduction |
| ML-DSA ops | 2 | 0 | 100% reduction |

## Server Output

The server will also show:

**Connection #1:**
```
[SESSION] Full handshake performed (new session)
[INFO] Client can resume this session on next connection
```

**Connection #2:**
```
[SESSION] ✓ Session RESUMED from client ticket
[PERF] Skipped expensive PQC key exchange and signatures
```

## Packet Capture Analysis

To verify session resumption at the protocol level:

```bash
# Terminal 1: Start packet capture
sudo tcpdump -i tap0 -w session_test.pcap udp port 11111

# Terminal 2: Run server
cd server && ./server

# Terminal 3: Run client (with test modifications)
cd boot && make && ./boot.bin
```

Analyze with Wireshark:
- **First handshake:** Look for `Certificate`, `CertificateVerify`, `NewSessionTicket` messages
- **Second handshake:** Should be MUCH shorter, using PSK mode, NO Certificate messages

## Troubleshooting

**No session saved:**
- Check that `saved_session = wolfSSL_get1_session(ssl)` returns non-NULL
- Verify `HAVE_SESSION_TICKET` is actually enabled (check compile warnings)

**Session not resumed:**
- Server should send `NewSessionTicket` after first handshake
- Check `wolfSSL_set_session()` returns `WOLFSSL_SUCCESS`
- Verify tickets haven't "expired" (they shouldn't with our config)

**Client resets connection:**
- Network buffer might be full - ensure `udp_service()` is called regularly
- Check timeout values are sufficient

## Memory Testing

To test with/without session storage for memory comparison:

```c
// Disable session resumption for baseline
#define TEST_WITHOUT_SESSIONS 0  // Set to 1 to disable

#if !TEST_WITHOUT_SESSIONS
    static WOLFSSL_SESSION* saved_session = NULL;
#endif
```

Compare memory usage:
- With sessions: ~2-4KB extra per saved session
- Without: Baseline memory only

Use wolfSSL static memory stats to measure:
```c
#ifdef WOLFSSL_STATIC_MEMORY
    wolfSSL_PrintStats(heap_hint);
#endif
```
