#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include <stdint.h>

// ============================================================================
// Performance Measurement Utilities for RISC-V Bare-Metal
// ============================================================================
// Uses RISC-V mcycle CSR (cycle counter) for high-precision timing
// Designed for evaluation criteria measurement (latency, throughput, CPU)
// ============================================================================

// CPU Frequency (approximate from LiteX simulator)
#define PERF_CPU_FREQ_MHZ 1  // ~1MHz VexRISCV

// ============================================================================
// RISC-V Cycle Counter Access
// ============================================================================

// Read 64-bit cycle counter (mcycle + mcycleh on RV32)
static inline uint64_t perf_get_cycles(void) {
    uint32_t lo, hi, hi2;
    
    // Handle potential rollover by reading hi twice
    do {
        asm volatile ("rdcycleh %0" : "=r" (hi));
        asm volatile ("rdcycle %0" : "=r" (lo));
        asm volatile ("rdcycleh %0" : "=r" (hi2));
    } while (hi != hi2);  // Retry if high word changed
    
    return ((uint64_t)hi << 32) | lo;
}

// ============================================================================
// Time Conversion Functions (Integer-only for RV32IM without FPU)
// ============================================================================

// Convert cycles to milliseconds (returns integer ms)
static inline uint32_t perf_cycles_to_ms_int(uint64_t cycles) {
    // At 1MHz: 1000 cycles = 1 millisecond
    return (uint32_t)(cycles / (PERF_CPU_FREQ_MHZ * 1000));
}

// Convert cycles to seconds (returns integer seconds)  
static inline uint32_t perf_cycles_to_sec_int(uint64_t cycles) {
    return (uint32_t)(cycles / (PERF_CPU_FREQ_MHZ * 1000000));
}

// ============================================================================
// Performance Metrics Storage
// ============================================================================

typedef struct {
    // Latency metrics (in cycles)
    uint64_t handshake_start;
    uint64_t handshake_end;
    uint64_t handshake_cycles;
    
    // Throughput metrics
    uint32_t throughput_bytes;
    uint32_t throughput_iterations;
    uint64_t throughput_start;
    uint64_t throughput_end;
    uint32_t throughput_bps;  // bytes per second 
    
    // Memory metrics (from wolfSSL)
    uint32_t peak_ram_bytes;
    uint32_t current_ram_bytes;
    
    // Connection type
    int is_resumed;  // 0 = full handshake, 1 = resumed
} perf_metrics_t;

// ============================================================================
// Convenience Macros
// ============================================================================

#define PERF_START(var) do { var = perf_get_cycles(); } while(0)
#define PERF_END(var) do { var = perf_get_cycles(); } while(0)
#define PERF_ELAPSED(start, end) ((end) - (start))

// ============================================================================
// Display Functions
// ============================================================================

// Print performance metrics summary
void perf_print_metrics(const perf_metrics_t* metrics);

// Print comparison between two connections (full vs resumed)
void perf_print_comparison(const perf_metrics_t* conn1, const perf_metrics_t* conn2);

#endif // PERFORMANCE_H
