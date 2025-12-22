# Solana Snapshot Loader - Complete Architecture Specification

## Document Version
- **Version**: 2.0
- **Last Updated**: 2025-12-22
- **Agave Compatibility**: v2.0.x - v2.3.x
- **Snapshot Version**: 1.2.0
- **Target Implementation**: slonana.cpp validator

## Executive Summary

This document provides complete technical specifications for implementing a custom Solana snapshot loader compatible with the Agave validator. It covers all binary formats, serialization details, hash algorithms, and architecture necessary to bootstrap a node from snapshot data.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Snapshot Archive Format](#2-snapshot-archive-format)
3. [Manifest File Format (Bincode)](#3-manifest-file-format-bincode)
4. [AppendVec Binary Format](#4-appendvec-binary-format)
5. [Accounts Index Architecture](#5-accounts-index-architecture)
6. [Hash Algorithms and Verification](#6-hash-algorithms-and-verification)
7. [Bank State Reconstruction](#7-bank-state-reconstruction)
8. [Snapshot Download Protocol](#8-snapshot-download-protocol)
9. [Incremental Snapshot Handling](#9-incremental-snapshot-handling)
10. [Parallelization Strategy](#10-parallelization-strategy)
11. [Memory Management](#11-memory-management)
12. [Error Handling and Recovery](#12-error-handling-and-recovery)
13. [Integration with Turbine](#13-integration-with-turbine)
14. [Implementation Checklist](#14-implementation-checklist)

---

## Integration with slonana.cpp

This specification complements slonana.cpp's existing snapshot infrastructure:

**Existing Implementation Files:**
- `include/validator/snapshot.h` - SnapshotManager class and data structures
- `src/validator/snapshot.cpp` - Current snapshot creation and restoration
- `src/validator/snapshot_bootstrap.cpp` - Bootstrap orchestration
- `src/validator/snapshot_finder.cpp` - Snapshot discovery from RPC nodes
- `tests/test_snapshot.cpp` - Snapshot system tests

**Integration Points:**

The `SnapshotManager` class in slonana.cpp provides the high-level API. This specification details the low-level binary formats and algorithms needed to enhance the implementation with full Agave compatibility.

```cpp
// Existing slonana.cpp interface (from include/validator/snapshot.h)
namespace slonana::validator {
  class SnapshotManager {
  public:
    bool restore_from_snapshot(const std::string &snapshot_path,
                               const std::string &ledger_path);
    // Implementation will use specifications from sections 2-7
  };
}
```

**Implementation Strategy:**
1. Use binary format specifications (Sections 2-4) to parse Agave-generated snapshots
2. Integrate accounts index architecture (Section 5) with existing AccountSnapshot structures
3. Apply hash verification (Section 6) for snapshot integrity
4. Leverage parallelization strategies (Section 10) for performance

---

## 1. Overview

### 1.1 Purpose

A snapshot is a serialized dump of the complete Solana account state at a specific slot. Loading a snapshot is required before a validator can:
- Participate in consensus
- Receive Turbine shreds
- Process transactions
- Serve RPC requests

### 1.2 Components

```
Snapshot Archive
├── Compressed Layer (zstd or lz4)
├── TAR Archive Layer (GNU format)
└── Contents:
    ├── version                    # Text file: "1.2.0"
    ├── snapshots/{slot}/{slot}    # Manifest (bincode serialized)
    ├── snapshots/{slot}/status_cache  # Transaction status cache
    └── accounts/{slot}.{id}       # AppendVec files (raw binary)
```

### 1.3 Key Statistics (Mainnet, Nov 2025)

| Metric | Value |
|--------|-------|
| Full snapshot size (compressed) | ~100-120 GB |
| Full snapshot size (uncompressed) | ~350-450 GB |
| Incremental snapshot size | ~500 MB - 2 GB |
| Total accounts | ~1.0-1.2 billion |
| Full snapshot frequency | Every 25,000 slots (~3 hours) |
| Incremental frequency | Every 100-500 slots |
| Bootstrap time (Agave, 68 threads) | 80-120 minutes |
| Target bootstrap time (240 cores) | 25-40 minutes |

---

## 2. Snapshot Archive Format

### 2.1 File Naming Convention

**Full Snapshot:**
```
snapshot-{SLOT}-{HASH}.tar.zst
snapshot-{SLOT}-{HASH}.tar.lz4

Example: snapshot-382765050-7L39D4Zw7h3DBduWxmsBUFPYMLE7nrrm3kbzXkvkiiWd.tar.zst

Regex: ^snapshot-(?P<slot>[[:digit:]]+)-(?P<hash>[[:alnum:]]+)\.(?P<ext>tar\.zst|tar\.lz4)$
```

**Incremental Snapshot:**
```
incremental-snapshot-{BASE_SLOT}-{SLOT}-{HASH}.tar.zst
incremental-snapshot-{BASE_SLOT}-{SLOT}-{HASH}.tar.lz4

Example: incremental-snapshot-382624923-382637436-FFi4iL6wybq9rQrFSWBjgFQFkQRgcGa7g5kzrC2pEsFv.tar.zst

Regex: ^incremental-snapshot-(?P<base>[[:digit:]]+)-(?P<slot>[[:digit:]]+)-(?P<hash>[[:alnum:]]+)\.(?P<ext>tar\.zst|tar\.lz4)$
```

### 2.2 Compression Formats

**Zstandard (Primary - tar.zst):**
```c
// Decompression parameters
ZSTD_DCtx* dctx = ZSTD_createDCtx();
ZSTD_DCtx_setParameter(dctx, ZSTD_d_nbWorkers, num_threads);  // Multi-threaded

// Window size for snapshots: 128 MB (2^27)
// Compression level used: 1-3 (fast)
// Dictionary: None
```

**LZ4 (Fallback - tar.lz4):**
```c
// LZ4 frame decompression
LZ4F_decompressionContext_t dctx;
LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
```

### 2.3 TAR Archive Structure

**TAR Format:** GNU (OLDGNU) format with ustar extensions

**Entry Ordering (Critical for streaming):**
1. `version` - MUST be first
2. `snapshots/{slot}/{slot}` - Manifest file
3. `snapshots/{slot}/status_cache` - Optional status cache
4. `accounts/` entries - Interleaved by size:
   - Pattern: 4 small files + 1 large file (optimizes I/O)
   - Small: < 1 MB
   - Large: >= 1 MB

**TAR Header Structure (512 bytes):**
```c
struct tar_header {
    char name[100];      // File name
    char mode[8];        // File mode (octal)
    char uid[8];         // Owner UID (octal)
    char gid[8];         // Owner GID (octal)
    char size[12];       // File size (octal)
    char mtime[12];      // Modification time (octal)
    char checksum[8];    // Header checksum
    char typeflag;       // File type ('0' = regular file)
    char linkname[100];  // Link name
    char magic[6];       // "ustar\0"
    char version[2];     // "00"
    char uname[32];      // Owner username
    char gname[32];      // Owner group name
    char devmajor[8];    // Device major
    char devminor[8];    // Device minor
    char prefix[155];    // Filename prefix
    char padding[12];    // Padding to 512 bytes
};
```

### 2.4 Version File

**Location:** Archive root as `version`
**Content:** ASCII string `1.2.0\n` (with newline)
**Size:** 6 bytes

```c
// Version validation
const char* EXPECTED_VERSION = "1.2.0";
bool validate_version(const char* version_content) {
    return strncmp(version_content, EXPECTED_VERSION, 5) == 0;
}
```


---

## 3. Manifest File Format (Bincode)

### 3.1 Overview

The manifest file contains serialized bank state and account storage metadata. It's located at `snapshots/{slot}/{slot}` within the archive.

**Serialization:** Bincode (little-endian, length-prefixed collections)

### 3.2 Top-Level Structure

```rust
// Root structure in manifest file
struct SnapshotManifest {
    bank: DeserializableVersionedBank,    // Bank state
    accounts_db: AccountsDbFields,        // Account storage info
    lamports_per_signature: u64,          // Fee rate
}
```

### 3.3 Bincode Serialization Rules

**Primitive Types:**
```
bool        -> 1 byte (0x00 or 0x01)
u8/i8       -> 1 byte
u16/i16     -> 2 bytes, little-endian
u32/i32     -> 4 bytes, little-endian
u64/i64     -> 8 bytes, little-endian
u128/i128   -> 16 bytes, little-endian
f32         -> 4 bytes, IEEE 754
f64         -> 8 bytes, IEEE 754
```

**Complex Types:**
```
Option<T>   -> 1 byte tag (0=None, 1=Some) + T if Some
Vec<T>      -> 8 bytes length (u64) + N*sizeof(T) elements
String      -> 8 bytes length (u64) + UTF-8 bytes
HashMap<K,V> -> 8 bytes length (u64) + N*(K,V) pairs
[T; N]      -> N*sizeof(T) (fixed-size, no length prefix)
```

**Enum/Tagged Union:**
```
enum Foo { A, B(u32), C { x: u64 } }
A    -> 4 bytes: variant index (0)
B(5) -> 4 bytes: variant index (1) + 4 bytes: u32 value
C    -> 4 bytes: variant index (2) + 8 bytes: u64 value
```

### 3.4 Bank Fields Structure

```rust
struct DeserializableVersionedBank {
    blockhash_queue: BlockhashQueue,
    ancestors: HashMap<Slot, usize>,
    hash: Hash,                         // 32 bytes
    parent_hash: Hash,                  // 32 bytes
    parent_slot: Slot,                  // u64
    hard_forks: HardForks,
    transaction_count: u64,
    tick_height: u64,
    signature_count: u64,
    capitalization: u64,                // Total lamports in circulation
    max_tick_height: u64,
    hashes_per_tick: Option<u64>,
    ticks_per_slot: u64,
    ns_per_slot: u128,
    genesis_creation_time: i64,         // Unix timestamp
    slots_per_year: f64,
    accounts_data_len: u64,
    slot: Slot,                         // u64
    epoch: Epoch,                       // u64
    block_height: u64,
    collector_id: Pubkey,               // 32 bytes - slot leader
    collector_fees: u64,
    fee_rate_governor: FeeRateGovernor,
    rent_collector: RentCollector,
    epoch_schedule: EpochSchedule,
    inflation: Inflation,
    stakes: Stakes,
    unused_accounts: UnusedAccounts,    // Empty, for compatibility
    is_delta: bool,
}
```

### 3.5 AccountsDbFields Structure

```rust
struct AccountsDbFields {
    // Map of slot -> list of storage entries
    storages: HashMap<Slot, Vec<AccountStorageEntry>>,

    // Obsolete, kept for compatibility
    write_version: u64,

    // Minimum slot in cache
    min_cached_slot: Slot,

    // Bank hash information
    bank_hash_info: BankHashInfo,

    // Recent root slots
    roots_within_last_epoch: Vec<Slot>,
    roots_with_hash: Vec<(Slot, Hash)>,
}

struct AccountStorageEntry {
    id: u32,              // AppendVec file ID
    slot: Slot,           // Slot number (u64)
    accounts_count: u64,  // Number of accounts in file
    file_size: u64,       // CRITICAL: Valid data length (not file size!)
}

struct BankHashInfo {
    obsolete_accounts_delta_hash: [u8; 32],
    obsolete_accounts_hash: [u8; 32],
    stats: BankHashStats,
}
```

### 3.6 Stakes Structure

```rust
struct Stakes {
    vote_accounts: VoteAccounts,
    stake_delegations: HashMap<Pubkey, Delegation>,
    epoch: Epoch,
    stake_history: StakeHistory,
}

struct VoteAccounts {
    // pubkey -> (stake_amount, VoteAccount)
    accounts: HashMap<Pubkey, (u64, VoteAccount)>,
}

struct Delegation {
    voter_pubkey: Pubkey,        // 32 bytes
    stake: u64,
    activation_epoch: Epoch,     // u64
    deactivation_epoch: Epoch,   // u64
    warmup_cooldown_rate: f64,   // Deprecated, always 0.25
}
```

### 3.7 Critical Manifest Parsing Notes

1. **file_size is NOT the actual file size** - It's the valid data length within the AppendVec. Data after this offset is garbage.

2. **Selective parsing is impossible** - Bincode requires sequential deserialization of all fields.

3. **Version compatibility** - Manifest structure varies between Solana versions (v1.14, v1.17, v2.0). Must handle all variants.

4. **Stream size limit** - Maximum 32 GiB for safety.

### 3.8 C++ Integration with slonana.cpp

The manifest data can be integrated with slonana.cpp's existing `SnapshotMetadata` structure:

```cpp
// From include/validator/snapshot.h
namespace slonana::validator {
  struct SnapshotMetadata {
    uint64_t slot;                    // Maps to manifest.bank.slot
    std::string block_hash;           // Maps to manifest.bank.hash (base58)
    uint64_t timestamp;               // Maps to manifest.bank.unix_timestamp
    uint64_t lamports_total;          // Maps to manifest.bank.capitalization
    uint64_t account_count;           // Sum of accounts_count from all storages
    uint64_t compressed_size;         // Archive file size
    uint64_t uncompressed_size;       // Sum of AppendVec file_size values
    std::string version;              // From version file: "1.2.0"
    bool is_incremental;              // From filename pattern
    uint64_t base_slot;               // For incremental snapshots
  };
}

// Example: Populating SnapshotMetadata from parsed manifest
SnapshotMetadata metadata;
metadata.slot = manifest.bank.slot;
metadata.lamports_total = manifest.bank.capitalization;
metadata.account_count = 0;
for (const auto& [slot, storages] : manifest.accounts_db.storages) {
  for (const auto& storage : storages) {
    metadata.account_count += storage.accounts_count;
  }
}
```

---

## 4. AppendVec Binary Format

### 4.1 Overview

AppendVec files (also called "account files") store serialized account data. Each file contains multiple accounts concatenated sequentially.

**File naming:** `accounts/{slot}.{id}`
**Maximum file size:** 16 GiB (MAXIMUM_APPEND_VEC_FILE_SIZE)
**Typical file size:** Up to 16 MB for memory-mapping efficiency

### 4.2 File Structure

```
┌─────────────────────────────────────────────────────────────┐
│ Account Entry 0                                              │
├─────────────────────────────────────────────────────────────┤
│ Account Entry 1                                              │
├─────────────────────────────────────────────────────────────┤
│ ...                                                          │
├─────────────────────────────────────────────────────────────┤
│ Account Entry N-1                                            │
├─────────────────────────────────────────────────────────────┤
│ [GARBAGE DATA - DO NOT PARSE]                                │
│ (from file_size offset to end of physical file)              │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 Account Entry Structure (136 bytes header + variable data)

```
Offset  Size  Field                 Type        Description
──────────────────────────────────────────────────────────────
0x00    8     write_version         u64         Obsolete, ignored
0x08    8     data_len              u64         Account data length
0x10    32    pubkey                [u8; 32]    Account public key
0x30    8     lamports              u64         Balance in lamports
0x38    8     rent_epoch            u64         Epoch for rent
0x40    32    owner                 [u8; 32]    Owner program pubkey
0x60    1     executable            u8          Is executable (0 or 1)
0x61    7     _padding              [u8; 7]     Alignment padding
0x68    32    hash                  [u8; 32]    Obsolete account hash
0x88    var   data                  [u8; N]     Account data (N = data_len)
──────────────────────────────────────────────────────────────
Total: 136 + data_len bytes (before alignment)
```

### 4.4 C/C++ Structure Definition

```c
#pragma pack(push, 1)

// Total: 40 bytes
struct StoredMeta {
    uint64_t write_version_obsolete;  // 0x00: Ignored
    uint64_t data_len;                // 0x08: Account data length
    uint8_t  pubkey[32];              // 0x10: Account pubkey
};

// Total: 48 bytes
struct AccountMeta {
    uint64_t lamports;                // 0x30: Balance
    uint64_t rent_epoch;              // 0x38: Rent epoch
    uint8_t  owner[32];               // 0x40: Owner program
    uint8_t  executable;              // 0x60: Is executable
    uint8_t  _padding[7];             // 0x61: Alignment
};

// Total: 32 bytes (obsolete, but must be present)
struct ObsoleteAccountHash {
    uint8_t  hash[32];                // 0x68: Unused
};

// Complete header: 136 bytes
struct AccountHeader {
    StoredMeta stored_meta;           // 40 bytes
    AccountMeta account_meta;         // 48 bytes
    ObsoleteAccountHash hash;         // 32 bytes
    // Followed by: uint8_t data[stored_meta.data_len]
};

#pragma pack(pop)

// Constants
#define STORE_META_OVERHEAD 136
#define ALIGN_BOUNDARY 8

// Calculate total entry size (aligned to 8 bytes)
static inline size_t aligned_stored_size(uint64_t data_len) {
    size_t size = STORE_META_OVERHEAD + data_len;
    return (size + ALIGN_BOUNDARY - 1) & ~(ALIGN_BOUNDARY - 1);
}
```



### 4.5 Account Entry Parsing Algorithm

```c
struct ParsedAccount {
    uint8_t  pubkey[32];
    uint64_t lamports;
    uint64_t rent_epoch;
    uint8_t  owner[32];
    bool     executable;
    uint8_t* data;
    size_t   data_len;
    size_t   file_offset;  // Offset within AppendVec
};

// Parse all accounts from an AppendVec
int parse_appendvec(const uint8_t* mmap_data, size_t valid_length,
                    ParsedAccount** accounts_out, size_t* count_out) {
    size_t offset = 0;
    size_t capacity = 1024;
    size_t count = 0;
    ParsedAccount* accounts = malloc(capacity * sizeof(ParsedAccount));

    while (offset + STORE_META_OVERHEAD <= valid_length) {
        const AccountHeader* header = (const AccountHeader*)(mmap_data + offset);
        uint64_t data_len = header->stored_meta.data_len;

        // Validate data_len
        size_t entry_size = aligned_stored_size(data_len);
        if (offset + entry_size > valid_length) {
            break;  // Reached end of valid data
        }

        // Check for zero account (slot boundary marker)
        bool all_zero = true;
        for (int i = 0; i < 32 && all_zero; i++) {
            if (header->stored_meta.pubkey[i] != 0) all_zero = false;
        }
        if (all_zero && header->account_meta.lamports == 0) {
            offset += entry_size;
            continue;  // Skip padding entries
        }

        // Grow array if needed
        if (count >= capacity) {
            capacity *= 2;
            accounts = realloc(accounts, capacity * sizeof(ParsedAccount));
        }

        // Copy account data
        ParsedAccount* acc = &accounts[count++];
        memcpy(acc->pubkey, header->stored_meta.pubkey, 32);
        acc->lamports = header->account_meta.lamports;
        acc->rent_epoch = header->account_meta.rent_epoch;
        memcpy(acc->owner, header->account_meta.owner, 32);
        acc->executable = header->account_meta.executable & 0x01;  // Only low bit
        acc->data_len = data_len;
        acc->data = (uint8_t*)(mmap_data + offset + STORE_META_OVERHEAD);
        acc->file_offset = offset;

        offset += entry_size;
    }

    *accounts_out = accounts;
    *count_out = count;
    return 0;
}
```

### 4.6 Zero-Lamport Account Constraints

Zero-lamport accounts have special requirements:
```c
// A zero-lamport account MUST have these exact values:
struct ZeroLamportAccount {
    uint64_t lamports;     // MUST be 0
    uint64_t data_len;     // MUST be 0
    bool     executable;   // MUST be false
    uint64_t rent_epoch;   // MUST be 0
    uint8_t  owner[32];    // MUST be all zeros (Pubkey::default())
};
```

### 4.7 Executable Byte Handling

```c
// The executable field is 1 byte but only the lowest bit is valid
// High 7 bits MUST be zero for valid accounts
bool is_executable(uint8_t executable_byte) {
    return (executable_byte & 0x01) != 0;
}

bool validate_executable(uint8_t executable_byte) {
    // If executable is true, only value 0x01 is valid
    // If executable is false, only value 0x00 is valid
    return executable_byte == 0x00 || executable_byte == 0x01;
}
```

### 4.8 C++ Integration: Converting to AccountSnapshot

The parsed account data can be converted to slonana.cpp's `AccountSnapshot` structure:

```cpp
// From include/validator/snapshot.h
namespace slonana::validator {
  struct AccountSnapshot {
    common::PublicKey pubkey;        // std::vector<uint8_t>
    uint64_t lamports;
    std::vector<uint8_t> data;
    common::PublicKey owner;         // std::vector<uint8_t>
    bool executable;
    uint64_t rent_epoch;
  };
}

// Conversion function: AppendVec entry -> AccountSnapshot
AccountSnapshot parse_account_from_appendvec(const uint8_t* entry_ptr) {
  const AccountHeader* header = reinterpret_cast<const AccountHeader*>(entry_ptr);
  
  AccountSnapshot account;
  
  // Copy pubkey (32 bytes)
  account.pubkey.assign(header->stored_meta.pubkey, 
                        header->stored_meta.pubkey + 32);
  
  // Copy metadata
  account.lamports = header->account_meta.lamports;
  account.rent_epoch = header->account_meta.rent_epoch;
  account.executable = (header->account_meta.executable & 0x01) != 0;
  
  // Copy owner (32 bytes)
  account.owner.assign(header->account_meta.owner,
                       header->account_meta.owner + 32);
  
  // Copy account data
  const uint8_t* data_ptr = entry_ptr + STORE_META_OVERHEAD;
  account.data.assign(data_ptr, data_ptr + header->stored_meta.data_len);
  
  return account;
}

// Integration with SnapshotManager::load_accounts_from_snapshot()
std::vector<AccountSnapshot> load_from_appendvec(const std::string& path, 
                                                  size_t valid_length) {
  std::vector<AccountSnapshot> accounts;
  
  // Memory-map the file
  int fd = open(path.c_str(), O_RDONLY);
  void* mmap_data = mmap(nullptr, valid_length, PROT_READ, MAP_PRIVATE, fd, 0);
  
  size_t offset = 0;
  while (offset + STORE_META_OVERHEAD <= valid_length) {
    const uint8_t* entry_ptr = static_cast<const uint8_t*>(mmap_data) + offset;
    const AccountHeader* header = reinterpret_cast<const AccountHeader*>(entry_ptr);
    
    size_t entry_size = aligned_stored_size(header->stored_meta.data_len);
    if (offset + entry_size > valid_length) break;
    
    // Convert and add to vector
    accounts.push_back(parse_account_from_appendvec(entry_ptr));
    offset += entry_size;
  }
  
  munmap(mmap_data, valid_length);
  close(fd);
  
  return accounts;
}
```

---

## 5. Accounts Index Architecture

### 5.1 Overview

The accounts index maps `Pubkey -> [(Slot, AccountLocation)]`, allowing fast lookup of account data across multiple slots.

### 5.2 Bin Partitioning

**Number of bins:** 8192 (2^13)
**Bin assignment:** First 3 bytes of pubkey

```c
#define NUM_BINS 8192
#define SHIFT_BITS (24 - 13)  // = 11

// Bin assignment: uses first 24 bits of pubkey, shifted to get bin index
static inline uint32_t get_bin_index(const uint8_t* pubkey) {
    uint32_t first_24_bits = ((uint32_t)pubkey[0] << 16) |
                            ((uint32_t)pubkey[1] << 8) |
                            ((uint32_t)pubkey[2]);
    return first_24_bits >> SHIFT_BITS;
}

// Example:
// pubkey[0:3] = [0x12, 0x34, 0x56, ...]
// first_24_bits = 0x123456
// bin = 0x123456 >> 11 = 0x0091 = 145
```

### 5.3 Index Entry Structure

```c
// Reference to an account's location in storage
struct AccountInfo {
    uint32_t store_id;     // AppendVec file ID
    uint64_t offset;       // Offset within AppendVec
    uint64_t lamports;     // Cached for fast filtering
};

// Entry in the index (one per unique pubkey)
struct AccountIndexEntry {
    uint8_t pubkey[32];

    // Slot list: all slots where this account exists
    // Sorted by slot descending (newest first)
    struct {
        uint64_t slot;
        AccountInfo info;
    }* slot_list;
    size_t slot_list_len;
    size_t slot_list_capacity;

    // Reference count for cleanup
    uint64_t ref_count;
};

// Bin structure (one of 8192)
struct AccountIndexBin {
    pthread_rwlock_t lock;

    // Hash table within bin
    struct {
        uint8_t pubkey[32];
        AccountIndexEntry* entry;
    }* entries;
    size_t count;
    size_t capacity;
};

// Complete index
struct AccountsIndex {
    AccountIndexBin bins[NUM_BINS];

    // Statistics
    uint64_t total_accounts;
    uint64_t total_entries;
};
```

---

## 6. Hash Algorithms and Verification

### 6.1 Lattice Hash (LtHash)

Agave uses LtHash for incremental account hashing. LtHash allows O(1) updates when accounts change.

```c
// LtHash is a 2048-byte structure (1024 x u16)
struct LtHash {
    uint16_t elements[1024];
};

// Identity LtHash (all zeros)
static const LtHash LT_HASH_IDENTITY = {0};

// Initialize LtHash from account data using Blake3 XOF
void lt_hash_from_account(LtHash* out, const ParsedAccount* account) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    // Hash account fields
    blake3_hasher_update(&hasher, &account->lamports, 8);
    blake3_hasher_update(&hasher, &account->rent_epoch, 8);
    blake3_hasher_update(&hasher, account->data, account->data_len);
    uint8_t exec_byte = account->executable ? 1 : 0;
    blake3_hasher_update(&hasher, &exec_byte, 1);
    blake3_hasher_update(&hasher, account->owner, 32);
    blake3_hasher_update(&hasher, account->pubkey, 32);

    // Extract 2048 bytes using Blake3 XOF (extended output)
    uint8_t xof_output[2048];
    blake3_hasher_finalize_xof(&hasher, xof_output, 2048);

    // Convert to u16 array (little-endian)
    for (int i = 0; i < 1024; i++) {
        out->elements[i] = (uint16_t)xof_output[i*2] |
                          ((uint16_t)xof_output[i*2+1] << 8);
    }
}

// Mix in another LtHash (element-wise wrapping add)
void lt_hash_mix_in(LtHash* accumulator, const LtHash* other) {
    for (int i = 0; i < 1024; i++) {
        accumulator->elements[i] += other->elements[i];  // Wrapping add
    }
}

// Compute checksum (final 32-byte hash)
void lt_hash_checksum(const LtHash* hash, uint8_t checksum[32]) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, hash->elements, 2048);
    blake3_hasher_finalize(&hasher, checksum, 32);
}
```

---

## 7. Bank State Reconstruction

### 7.1 Required Components

After loading snapshot, the bank needs:

```c
struct Bank {
    // Core state
    uint64_t slot;
    uint64_t parent_slot;
    uint64_t block_height;
    uint64_t epoch;

    // Hashes
    uint8_t hash[32];
    uint8_t parent_hash[32];

    // Economics
    uint64_t capitalization;          // Total lamports
    uint64_t collector_fees;
    FeeRateGovernor fee_rate_governor;

    // Time
    int64_t genesis_creation_time;
    int64_t unix_timestamp;

    // Consensus
    BlockhashQueue blockhash_queue;   // Recent blockhashes for tx validation
    EpochSchedule epoch_schedule;
    Stakes stakes;                     // Validator stakes for consensus

    // Rent
    RentCollector rent_collector;

    // Accounts
    AccountsIndex* accounts_index;

    // Transaction processing
    uint64_t transaction_count;
    uint64_t signature_count;
    uint64_t tick_height;
    uint64_t max_tick_height;
};
```

### 7.2 Blockhash Queue

```c
#define MAX_RECENT_BLOCKHASHES 300

struct BlockhashEntry {
    uint8_t blockhash[32];
    uint64_t fee_per_signature;  // Lamports
    uint64_t timestamp;          // Slot when added
};

struct BlockhashQueue {
    BlockhashEntry entries[MAX_RECENT_BLOCKHASHES];
    size_t count;
    uint64_t last_hash_index;
};

// Check if a blockhash is recent (for transaction validation)
bool is_blockhash_valid(BlockhashQueue* queue, const uint8_t blockhash[32]) {
    for (size_t i = 0; i < queue->count; i++) {
        if (memcmp(queue->entries[i].blockhash, blockhash, 32) == 0) {
            return true;
        }
    }
    return false;
}
```

---

## 8. Snapshot Download Protocol

### 8.1 RPC Endpoints

**Get Highest Snapshot Slot:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "getHighestSnapshotSlot",
  "params": []
}

Response:
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "full": 382765050,
    "incremental": 382767550
  }
}
```

### 8.2 Download Implementation

```c
// HTTP download with progress tracking
struct DownloadProgress {
    uint64_t total_bytes;
    uint64_t downloaded_bytes;
    double speed_mbps;
    uint64_t eta_seconds;
    void (*callback)(const DownloadProgress* progress);
};

int download_snapshot(const char* url, const char* output_path,
                     DownloadProgress* progress) {
    CURL* curl = curl_easy_init();
    if (!curl) return -1;

    FILE* fp = fopen(output_path, "wb");
    if (!fp) {
        curl_easy_cleanup(curl);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    fclose(fp);
    curl_easy_cleanup(curl);

    return res == CURLE_OK ? 0 : -1;
}
```

---

## 9. Incremental Snapshot Handling

### 9.1 Overview

Incremental snapshots contain only account changes since the base full snapshot. They significantly reduce download size and application time.

### 9.2 Incremental Application Algorithm

```c
// Apply incremental snapshot on top of full snapshot
int apply_incremental_snapshot(AccountsIndex* index,
                               const char* full_snapshot_path,
                               const char* incremental_snapshot_path) {
    // 1. Load full snapshot
    if (load_full_snapshot(index, full_snapshot_path) != 0) {
        return -1;
    }

    // 2. Parse incremental snapshot manifest
    SnapshotManifest incr_manifest;
    if (parse_manifest(incremental_snapshot_path, &incr_manifest) != 0) {
        return -1;
    }

    // 3. Load incremental AppendVec files and update index
    for (size_t i = 0; i < incr_manifest.accounts_db.storages.size; i++) {
        AccountStorageEntry* storage = &incr_manifest.accounts_db.storages.entries[i];

        // Load accounts from incremental file
        ParsedAccount* accounts;
        size_t account_count;
        load_appendvec(storage, &accounts, &account_count);

        // Update index (newer accounts overwrite older ones)
        for (size_t j = 0; j < account_count; j++) {
            update_account_in_index(index, &accounts[j], storage->slot);
        }
    }

    return 0;
}
```

---

## 10. Parallelization Strategy

### 10.1 Multi-Threaded Decompression

```c
// Decompress using multiple threads
int decompress_parallel(const char* compressed_path, const char* output_path,
                       int num_threads) {
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    ZSTD_DCtx_setParameter(dctx, ZSTD_d_nbWorkers, num_threads);

    // Perform decompression with multiple threads
    // ... implementation details

    ZSTD_freeDCtx(dctx);
    return 0;
}
```

### 10.2 Parallel AppendVec Processing

```c
// Process multiple AppendVec files in parallel
void process_appendvecs_parallel(AccountStorageEntry* storages, size_t count,
                                 AccountsIndex* index, int num_threads) {
    #pragma omp parallel for num_threads(num_threads) schedule(dynamic)
    for (size_t i = 0; i < count; i++) {
        AccountStorageEntry* storage = &storages[i];

        // Load and parse AppendVec
        ParsedAccount* accounts;
        size_t account_count;
        load_appendvec(storage, &accounts, &account_count);

        // Insert into index (bin-level locking handles concurrency)
        for (size_t j = 0; j < account_count; j++) {
            insert_account_into_index(index, &accounts[j], storage->slot);
        }

        free(accounts);
    }
}
```

---

## 11. Memory Management

### 11.1 Memory-Mapped Files

```c
// Memory-map an AppendVec file for zero-copy access
struct MappedFile {
    void* data;
    size_t size;
    int fd;
};

int mmap_appendvec(const char* path, size_t valid_length, MappedFile* mapped) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) return -1;

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return -1;
    }

    // Map file into memory
    void* data = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return -1;
    }

    // Advise kernel about access pattern
    madvise(data, st.st_size, MADV_SEQUENTIAL);

    mapped->data = data;
    mapped->size = st.st_size;
    mapped->fd = fd;

    return 0;
}
```

---

## 12. Error Handling and Recovery

### 12.1 Corruption Detection

```c
// Verify AppendVec integrity
int verify_appendvec_integrity(const uint8_t* data, size_t valid_length) {
    size_t offset = 0;
    size_t account_count = 0;

    while (offset + STORE_META_OVERHEAD <= valid_length) {
        const AccountHeader* header = (const AccountHeader*)(data + offset);
        uint64_t data_len = header->stored_meta.data_len;

        // Check for impossibly large data_len
        if (data_len > (1ULL << 32)) {  // 4 GiB limit per account
            fprintf(stderr, "Invalid data_len at offset %zu: %lu\n",
                    offset, data_len);
            return -1;
        }

        size_t entry_size = aligned_stored_size(data_len);

        // Check bounds
        if (offset + entry_size > valid_length) {
            break;  // Expected at end of valid data
        }

        // Validate executable byte
        if (!validate_executable(header->account_meta.executable)) {
            fprintf(stderr, "Invalid executable byte at offset %zu: 0x%02x\n",
                    offset, header->account_meta.executable);
            return -1;
        }

        account_count++;
        offset += entry_size;
    }

    printf("Verified %zu accounts in AppendVec\n", account_count);
    return 0;
}
```

---

## 13. Integration with Turbine

### 13.1 Stake-Weighted Retransmission

After loading snapshot, stake information is used for Turbine block propagation:

```c
// Build Turbine retransmission tree based on stake weights
struct TurbineNode {
    uint8_t pubkey[32];
    uint64_t stake;
    struct TurbineNode** children;
    size_t child_count;
};

TurbineNode* build_turbine_tree(Stakes* stakes, const uint8_t root_pubkey[32]) {
    // Sort validators by stake (descending)
    // Build tree with fanout = 200 (Turbine default)
    // Assign children level by level
    // ... implementation details
    return NULL;  // Placeholder
}
```

---

## 14. Implementation Checklist

### 14.1 Core Components

- [ ] TAR archive parser with streaming support
- [ ] Zstandard/LZ4 decompressor (multi-threaded)
- [ ] Bincode deserializer for manifest
- [ ] AppendVec parser with bounds checking
- [ ] Accounts index with 8192 bins
- [ ] Memory-mapped file support
- [ ] Blake3 hash implementation
- [ ] LtHash implementation
- [ ] Base58 decoder for hashes

### 14.2 Snapshot Loading

- [ ] Download snapshot from RPC/mirror
- [ ] Verify snapshot hash
- [ ] Extract TAR contents
- [ ] Parse manifest file
- [ ] Load all AppendVec files
- [ ] Build accounts index
- [ ] Verify accounts hash
- [ ] Reconstruct bank state

### 14.3 Incremental Snapshots

- [ ] Apply incremental on top of full
- [ ] Update existing accounts in index
- [ ] Handle account deletions
- [ ] Merge bank state changes

### 14.4 Optimization

- [ ] Parallel decompression
- [ ] Parallel AppendVec processing
- [ ] Parallel index building
- [ ] Parallel hash computation
- [ ] Memory pressure monitoring
- [ ] Disk I/O optimization (readahead, madvise)

### 14.5 Error Handling

- [ ] Corrupted file detection
- [ ] Graceful degradation
- [ ] Progress reporting
- [ ] Retry logic for downloads
- [ ] Cleanup on failure

### 14.6 Integration

- [ ] Bank state initialization
- [ ] Stake information extraction
- [ ] Turbine tree construction
- [ ] RPC server initialization
- [ ] Consensus participation

### 14.7 Testing

- [ ] Unit tests for each parser
- [ ] Integration test with real snapshot
- [ ] Performance benchmarks
- [ ] Memory usage profiling
- [ ] Corruption resistance tests
- [ ] Mainnet snapshot compatibility

---

## Appendix A: Binary Format Examples

### Example AppendVec Entry (System Program Account)

```
Offset  Hex Data                          Interpretation
──────────────────────────────────────────────────────────────
0x0000  00 00 00 00 00 00 00 00          write_version = 0
0x0008  00 00 00 00 00 00 00 00          data_len = 0
0x0010  00 00 00 00 00 00 00 00          pubkey[0:8]
        00 00 00 00 00 00 00 00          pubkey[8:16]
        00 00 00 00 00 00 00 00          pubkey[16:24]
        00 00 00 00 00 00 00 01          pubkey[24:32] = System Program
0x0030  E8 03 00 00 00 00 00 00          lamports = 1000
0x0038  00 00 00 00 00 00 00 00          rent_epoch = 0
0x0040  00 00 00 00 00 00 00 00          owner[0:8] = Native Loader
        00 00 00 00 00 00 00 00          owner[8:16]
        00 00 00 00 00 00 00 00          owner[16:24]
        00 00 00 00 00 00 00 02          owner[24:32]
0x0060  00                               executable = false
0x0061  00 00 00 00 00 00 00             padding
0x0068  [32 bytes of zeros]              obsolete hash
0x0088  [aligned to next 8-byte boundary]
```

---

## Appendix B: Reference Implementation Pseudocode

```python
def load_full_snapshot(snapshot_path: str) -> Bank:
    # 1. Download if needed
    if is_url(snapshot_path):
        snapshot_path = download_snapshot(snapshot_path)

    # 2. Decompress
    decompressed = decompress_archive(snapshot_path, num_threads=os.cpu_count())

    # 3. Extract TAR
    extracted_dir = extract_tar(decompressed)

    # 4. Verify version
    version = read_file(f"{extracted_dir}/version")
    assert version.strip() == "1.2.0"

    # 5. Parse manifest
    manifest_path = find_manifest(extracted_dir)
    manifest = parse_bincode_manifest(manifest_path)

    # 6. Load AppendVec files
    accounts_index = AccountsIndex(num_bins=8192)

    for slot, storage_entries in manifest.accounts_db.storages.items():
        for storage in storage_entries:
            appendvec_path = f"{extracted_dir}/accounts/{slot}.{storage.id}"
            accounts = parse_appendvec(appendvec_path, storage.file_size)

            for account in accounts:
                accounts_index.insert(account.pubkey, slot, account)

    # 7. Verify accounts hash
    computed_hash = compute_accounts_hash(accounts_index)
    assert computed_hash == manifest.bank.hash

    # 8. Reconstruct bank
    bank = Bank()
    bank.slot = manifest.bank.slot
    bank.epoch = manifest.bank.epoch
    bank.blockhash_queue = manifest.bank.blockhash_queue
    bank.stakes = manifest.bank.stakes
    bank.accounts_index = accounts_index

    return bank
```

---

## Appendix C: Performance Tuning

### Recommended System Configuration

**For 240-core system targeting 25-40 minute bootstrap:**

```bash
# CPU affinity
numactl --cpunodebind=0-3 --membind=0-3 ./snapshot_loader

# I/O scheduler
echo "none" > /sys/block/nvme0n1/queue/scheduler

# Huge pages
echo 100000 > /proc/sys/vm/nr_hugepages

# File descriptor limit
ulimit -n 1048576

# Increase read-ahead
blockdev --setra 8192 /dev/nvme0n1
```

**Thread allocation:**
- Decompression: 16 threads
- TAR extraction: 8 threads
- AppendVec parsing: 128 threads (parallel files)
- Index building: 64 threads (bin-parallel)
- Hash verification: 24 threads

**Memory requirements:**
- Accounts index: ~80 GB
- AppendVec mmaps: ~400 GB
- Working buffers: ~20 GB
- Total: ~500 GB RAM recommended

---

## Document Changelog

**v2.0 (2025-11-27):**
- Complete rewrite with binary format specifications
- Added Bincode serialization details
- Included AppendVec format with C structures
- Added LtHash algorithm implementation
- Expanded parallelization strategies
- Added Turbine integration section
- Included comprehensive implementation checklist

**v1.0 (2025-09-15):**
- Initial architecture overview
- Basic snapshot structure
- High-level loading process

---

## References

1. Agave Validator Source Code: https://github.com/anza-xyz/agave
2. Solana Runtime Documentation: https://docs.solana.com/runtime
3. Bincode Specification: https://github.com/bincode-org/bincode
4. Blake3 Hash: https://github.com/BLAKE3-team/BLAKE3
5. Zstandard: https://github.com/facebook/zstd
6. Turbine Protocol: https://docs.solana.com/cluster/turbine-block-propagation

### slonana.cpp-Specific References

7. slonana.cpp Snapshot Implementation: `include/validator/snapshot.h`
8. Snapshot Manager Source: `src/validator/snapshot.cpp`
9. Snapshot Bootstrap: `src/validator/snapshot_bootstrap.cpp`
10. Snapshot Tests: `tests/test_snapshot.cpp`
11. Snapshot Download Tests: `tests/test_snapshot_download.cpp`

---

## Appendix D: Testing with slonana.cpp

### Unit Tests

Test individual components using the existing test framework:

```cpp
// From tests/test_snapshot.cpp - Example test structure
#include "validator/snapshot.h"
#include "tests/test_framework.h"

TEST(SnapshotTest, ParseAppendVecBinaryFormat) {
  // Create test AppendVec data with known format
  std::vector<uint8_t> test_data = create_test_appendvec();
  
  // Parse using specification from Section 4.5
  auto accounts = parse_appendvec_entries(test_data.data(), test_data.size());
  
  // Verify parsed accounts match expected values
  EXPECT_EQ(accounts.size(), 10);
  EXPECT_EQ(accounts[0].lamports, 1000000);
}

TEST(SnapshotTest, VerifyAccountHash) {
  // Test LtHash implementation from Section 6.1
  AccountSnapshot account = create_test_account();
  
  LtHash hash = compute_account_lthash(account);
  
  // Verify hash properties
  EXPECT_NE(hash.elements[0], 0);  // Non-zero hash
}
```

### Integration Tests

Test with real Agave snapshots:

```bash
# Download devnet snapshot for testing
cd tests/fixtures
wget https://api.devnet.solana.com/snapshot.tar.zst -O devnet-snapshot.tar.zst

# Run integration test
./slonana_snapshot_tests --test=AgaveCompatibility
```

### Running Snapshot Tests

```bash
# Build tests
cd build
cmake .. -DBUILD_TESTS=ON
make slonana_snapshot_tests

# Run all snapshot tests
./slonana_snapshot_tests

# Run specific test categories
./slonana_snapshot_tests --gtest_filter="SnapshotTest.*"
./slonana_snapshot_tests --gtest_filter="*AppendVec*"
./slonana_snapshot_tests --gtest_filter="*Agave*"
```

### Validation Checklist

Before deploying snapshot loader implementation:

- [ ] Parse version file correctly (Section 2.4)
- [ ] Decompress zstd archives (Section 2.2)
- [ ] Extract TAR with correct ordering (Section 2.3)
- [ ] Parse Bincode manifest (Section 3)
- [ ] Parse all AppendVec entries with correct alignment (Section 4.3)
- [ ] Build accounts index with 8192 bins (Section 5.2)
- [ ] Verify LtHash matches expected value (Section 6.1)
- [ ] Handle zero-lamport accounts correctly (Section 4.6)
- [ ] Validate executable byte constraints (Section 4.7)
- [ ] Test with real Agave mainnet snapshot
- [ ] Measure bootstrap time and compare to targets (Section 1.3)
- [ ] Verify memory usage within bounds (Section 11)

---

*This document is maintained by the Slonana development team. For questions or corrections, please open an issue on GitHub.*
