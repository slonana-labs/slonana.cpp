# Performance Optimization Guide

This guide covers performance optimization strategies for the Slonana validator with ML inference capabilities.

## Table of Contents
1. [Profiling Methodology](#profiling-methodology)
2. [CPU Optimizations](#cpu-optimizations)
3. [Memory Optimizations](#memory-optimizations)
4. [Network Optimizations](#network-optimizations)
5. [BPF-Specific Optimizations](#bpf-specific-optimizations)
6. [Benchmarking](#benchmarking)

## Profiling Methodology

### CPU Profiling

**Using perf:**
```bash
# Record CPU profile
sudo perf record -g -F 999 ./slonana_validator

# Generate report
sudo perf report

# Generate flamegraph
git clone https://github.com/brendangregg/FlameGraph
sudo perf script | ./FlameGraph/stackcollapse-perf.pl | ./FlameGraph/flamegraph.pl > flame.svg
```

**Using valgrind (callgrind):**
```bash
valgrind --tool=callgrind --callgrind-out-file=callgrind.out ./slonana_validator
kcachegrind callgrind.out
```

### Memory Profiling

**Using valgrind (massif):**
```bash
valgrind --tool=massif --massif-out-file=massif.out ./slonana_validator
ms_print massif.out
```

**Using heaptrack:**
```bash
heaptrack ./slonana_validator
heaptrack_gui heaptrack.slonana_validator.*.gz
```

### Network Profiling

```bash
# Monitor network traffic
sudo iftop -i eth0

# Packet capture
sudo tcpdump -i eth0 -w capture.pcap port 8899 or port 8001

# Analyze with wireshark
wireshark capture.pcap
```

## CPU Optimizations

### SIMD Instructions

Use AVX2/AVX-512 for matrix operations:

```cpp
// Before: Scalar multiplication
for (int i = 0; i < size; i++) {
    result[i] = a[i] * b[i];
}

// After: AVX2 SIMD (8x 32-bit integers at once)
#include <immintrin.h>

void simd_multiply(int32_t* a, int32_t* b, int32_t* result, int size) {
    int i = 0;
    for (; i + 8 <= size; i += 8) {
        __m256i va = _mm256_loadu_si256((__m256i*)&a[i]);
        __m256i vb = _mm256_loadu_si256((__m256i*)&b[i]);
        __m256i vr = _mm256_mullo_epi32(va, vb);
        _mm256_storeu_si256((__m256i*)&result[i], vr);
    }
    // Handle remaining elements
    for (; i < size; i++) {
        result[i] = a[i] * b[i];
    }
}
```

### Loop Unrolling

```cpp
// Before: Simple loop
for (int i = 0; i < size; i++) {
    sum += data[i];
}

// After: Unrolled loop (4x)
int i = 0;
for (; i + 4 <= size; i += 4) {
    sum += data[i] + data[i+1] + data[i+2] + data[i+3];
}
for (; i < size; i++) {
    sum += data[i];
}
```

### Cache Line Alignment

```cpp
// Align structs to cache line boundaries (64 bytes)
struct alignas(64) OptimizedState {
    int32_t position;
    int32_t pnl;
    uint64_t timestamp;
    // ... other fields
};
```

### Branch Prediction Hints

```cpp
// Help the compiler predict likely branches
if __builtin_expect(condition, 1) { // Likely true
    fast_path();
} else {
    slow_path();
}
```

## Memory Optimizations

### Arena Allocators

```cpp
class ArenaAllocator {
    char* buffer;
    size_t size;
    size_t offset;
    
public:
    ArenaAllocator(size_t sz) : size(sz), offset(0) {
        buffer = new char[size];
    }
    
    void* allocate(size_t n) {
        if (offset + n > size) return nullptr;
        void* ptr = buffer + offset;
        offset += n;
        return ptr;
    }
    
    void reset() { offset = 0; }
};

// Use for transaction processing
ArenaAllocator tx_arena(1024 * 1024); // 1MB arena
for (auto& tx : transactions) {
    void* mem = tx_arena.allocate(sizeof(Transaction));
    // Process transaction
}
tx_arena.reset(); // Bulk deallocation
```

### Object Pooling

```cpp
template<typename T>
class ObjectPool {
    std::vector<T*> available;
    std::vector<std::unique_ptr<T>> owned;
    
public:
    T* acquire() {
        if (available.empty()) {
            owned.push_back(std::make_unique<T>());
            return owned.back().get();
        }
        T* obj = available.back();
        available.pop_back();
        return obj;
    }
    
    void release(T* obj) {
        available.push_back(obj);
    }
};

// Use for frequent allocations
ObjectPool<Transaction> tx_pool;
```

### Zero-Copy Packet Handling

```cpp
// Avoid copying packet data
struct Packet {
    uint8_t* data; // Points to network buffer
    size_t length;
    
    // Parse in-place
    void parse() {
        // Read directly from data pointer
    }
};
```

## Network Optimizations

### TCP Tuning

```bash
# Increase TCP buffer sizes
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 67108864"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 67108864"

# Enable TCP BBR congestion control
sudo sysctl -w net.ipv4.tcp_congestion_control=bbr

# Increase connection backlog
sudo sysctl -w net.core.somaxconn=4096
```

### QUIC Optimization

```cpp
// Configure QUIC parameters
quic::Config config;
config.max_idle_timeout = 30000; // 30 seconds
config.max_udp_payload_size = 1350; // MTU - overhead
config.initial_max_data = 10485760; // 10MB
config.initial_max_stream_data_bidi_local = 1048576; // 1MB
config.initial_max_streams_bidi = 100;
```

### UDP Batch Processing

```cpp
// Process multiple UDP packets in one syscall
struct mmsghdr msgs[BATCH_SIZE];
int count = recvmmsg(sockfd, msgs, BATCH_SIZE, 0, nullptr);
for (int i = 0; i < count; i++) {
    process_packet(msgs[i]);
}
```

## BPF-Specific Optimizations

### Verifier Hints

```c
// Help the verifier understand bounded loops
#define MAX_ITERATIONS 100
for (int i = 0; i < features_count && i < MAX_ITERATIONS; i++) {
    // Loop body
}
```

### Map Preallocation

```c
// Preallocate BPF maps to avoid runtime allocation
BPF_MAP_DEF(agent_state_map) = {
    .type = BPF_MAP_TYPE_HASH,
    .key_size = sizeof(uint32_t),
    .value_size = sizeof(struct AgentState),
    .max_entries = 10000,
    .map_flags = BPF_F_NO_PREALLOC, // Or preallocate based on needs
};
```

### Tail Call Optimization

```c
// Use tail calls to exceed complexity limits
BPF_ARRAY(prog_array, 10);

SEC("classifier/stage1")
int stage1(struct __sk_buff *skb) {
    // Process stage 1
    bpf_tail_call(skb, &prog_array, 1); // Jump to stage 2
    return 0;
}

SEC("classifier/stage2")
int stage2(struct __sk_buff *skb) {
    // Process stage 2
    return 0;
}
```

### Program Complexity Reduction

```c
// Before: Complex nested logic
if (condition1) {
    if (condition2) {
        if (condition3) {
            // Deep nesting
        }
    }
}

// After: Early returns
if (!condition1) return 0;
if (!condition2) return 0;
if (!condition3) return 0;
// Flat logic
```

## Benchmarking

### Target Metrics

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Throughput | 10,000 TPS | TBD | 🎯 |
| Latency (P50) | <100ms | TBD | 🎯 |
| Latency (P99) | <1s | TBD | 🎯 |
| ML Inference (Decision Tree) | <100ns | 9.7ns | ✅ |
| ML Inference (Dense 32x32) | <10μs | 634ns | ✅ |
| Activation (ReLU 64) | <100ns | 9.1ns | ✅ |
| Timer Creation | <1μs | 0.07μs | ✅ |
| Watcher Check (100) | <50μs | 19.6μs | ✅ |

### Benchmarking Tools

**CPU Benchmark:**
```bash
# Stress test
stress-ng --cpu 16 --timeout 60s --metrics

# Sysbench
sysbench cpu --threads=16 run
```

**Network Benchmark:**
```bash
# iperf3 throughput test
iperf3 -c target_host -P 10 -t 60

# netperf latency test
netperf -H target_host -t TCP_RR
```

**Disk I/O:**
```bash
# fio sequential write
fio --name=seqwrite --rw=write --bs=1M --size=10G --numjobs=1

# fio random read
fio --name=randread --rw=randread --bs=4k --size=10G --numjobs=4
```

### Continuous Profiling

```bash
# Setup automated profiling
crontab -e

# Add: Profile every hour
0 * * * * /opt/slonana/scripts/profile.sh

# profile.sh:
#!/bin/bash
DATE=$(date +%Y%m%d_%H%M%S)
sudo perf record -g -o /var/log/slonana/perf_$DATE.data -p $(pidof slonana_validator) sleep 30
```

## Performance Checklist

- [ ] Enable compiler optimizations (-O3, -march=native)
- [ ] Use SIMD instructions for matrix operations
- [ ] Implement arena allocators for transaction processing
- [ ] Configure TCP/QUIC parameters for network performance
- [ ] Add verifier hints to BPF programs
- [ ] Profile CPU hotspots with perf/flamegraphs
- [ ] Monitor memory allocations with heaptrack
- [ ] Benchmark against target metrics
- [ ] Set up continuous profiling
- [ ] Document performance characteristics

## References

- [Linux Performance Tools](http://www.brendangregg.com/linuxperf.html)
- [eBPF Performance Guide](https://ebpf.io/what-is-ebpf/#performance)
- [QUIC Performance Best Practices](https://quicwg.org/)
- [Intel Optimization Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
