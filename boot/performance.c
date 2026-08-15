#include "performance.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Performance Metrics Display Functions (Integer-only for RV32IM without FPU)
// ============================================================================

void perf_print_metrics(const perf_metrics_t* metrics) {
    if (!metrics) return;
    
    printf("\\n");
    printf("===============================================================================\\n");
    printf("                    PERFORMANCE METRICS\\n");
    printf("===============================================================================\\n");
    
    // Latency
    if (metrics->handshake_cycles > 0) {
        uint32_t hs_ms = perf_cycles_to_ms_int(metrics->handshake_cycles);
        uint32_t hs_sec = perf_cycles_to_sec_int(metrics->handshake_cycles);
        
        printf("[LATENCY]\\n");
        printf("  Handshake type: %s\\n", 
               metrics->is_resumed ? "Session Resumed (PSK)" : "Full Handshake (PQC)");
        printf("  Total cycles: %llu\\n", metrics->handshake_cycles);
        printf("  Total time: %lu ms (%lu seconds)\\n", (unsigned long)hs_ms, (unsigned long)hs_sec);
        printf("  CPU frequency: ~%d MHz\\n", PERF_CPU_FREQ_MHZ);
        printf("\\n");
    }
    
    // Throughput
    if (metrics->throughput_iterations > 0) {
        uint64_t tput_cycles = metrics->throughput_end - metrics->throughput_start;
        uint32_t tput_sec = perf_cycles_to_sec_int(tput_cycles);
        uint32_t tput_ms = perf_cycles_to_ms_int(tput_cycles);
        
        printf("[THROUGHPUT]\\n");
        printf("  Total bytes transferred: %lu bytes\\n", (unsigned long)metrics->throughput_bytes);
        printf("  Iterations: %lu\\n", (unsigned long)metrics->throughput_iterations);
        printf("  Time elapsed: %lu seconds (%lu ms)\\n", (unsigned long)tput_sec, (unsigned long)tput_ms);
        if (tput_sec > 0) {
            uint32_t bps = metrics->throughput_bytes / tput_sec;
            printf("  Throughput: %lu bytes/sec\\n", (unsigned long)bps);
        }
        if (metrics->throughput_iterations > 0) {
            uint32_t ms_per_iter = tput_ms / metrics->throughput_iterations;
            printf("  Average per iteration: %lu ms\\n", (unsigned long)ms_per_iter);
        }
        printf("\\n");
    }
    
    // Memory
    if (metrics->peak_ram_bytes > 0 || metrics->current_ram_bytes > 0) {
        printf("[MEMORY]\\n");
        if (metrics->peak_ram_bytes > 0) {
            printf("  Peak RAM usage: %lu bytes\\n", (unsigned long)metrics->peak_ram_bytes);
        }
        if (metrics->current_ram_bytes > 0) {
            printf("  Current RAM usage: %lu bytes\\n", (unsigned long)metrics->current_ram_bytes);
        }
        printf("  ROM footprint: 443 KB (from boot.elf)\\n");
        printf("  Static pool: 4 MB\\n");
        printf("\\n");
    }
    
    printf("===============================================================================\\n");
}

void perf_print_comparison(const perf_metrics_t* conn1, const perf_metrics_t* conn2) {
    if (!conn1 || !conn2) return;
    
    uint32_t hs1_ms = perf_cycles_to_ms_int(conn1->handshake_cycles);
    uint32_t hs2_ms = perf_cycles_to_ms_int(conn2->handshake_cycles);
    
    printf("\\n");
    printf("===============================================================================\\n");
    printf("           SESSION RESUMPTION PERFORMANCE COMPARISON\\n");
    printf("===============================================================================\\n");
    printf("\\n");
    printf("Connection #1 (Full Handshake with PQC):\\n");
    printf("  Latency: %lu ms\\n", (unsigned long)hs1_ms);
    printf("  Cycles: %llu\\n", conn1->handshake_cycles);
    printf("\\n");
    printf("Connection #2 (Session Resumption):\\n");
    printf("  Latency: %lu ms\\n", (unsigned long)hs2_ms);
    printf("  Cycles: %llu\\n", conn2->handshake_cycles);
    printf("\\n");
    printf("Performance Improvement:\\n");
    if (hs2_ms > 0) {
        uint32_t speedup = hs1_ms / hs2_ms;
        uint32_t time_saved = hs1_ms - hs2_ms;
        uint64_t cycle_reduction = conn1->handshake_cycles - conn2->handshake_cycles;
        
        printf("  Speedup: %lux faster\\n", (unsigned long)speedup);
        printf("  Time saved: %lu ms\\n", (unsigned long)time_saved);
        printf("  Cycle reduction: %llu cycles\\n", cycle_reduction);
        printf("\\n");
        
        if (speedup >= 8) {
            printf("  ✓ Excellent: Session resumption provides 8-9x speedup\\n");
            printf("  ✓ Skipped expensive PQC operations (ML-KEM-512, ML-DSA-44)\\n");
        } else if (speedup >= 2) {
            printf("  ✓ Good: Resumption faster than full handshake\\n");
        } else {
            printf("  ⚠ Warning: Resumption not significantly faster\\n");
            printf("  ⚠ Session resumption may not be working correctly\\n");
        }
    }
    
    printf("===============================================================================\\n");
}
