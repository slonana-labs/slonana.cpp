# Snapshot Bootstrap for Devnet RPC Nodes

This document describes the snapshot bootstrap functionality that enables slonana.cpp to run as an RPC node for Solana devnet with fast ledger synchronization.

## Overview

The snapshot bootstrap feature allows slonana.cpp to automatically discover, download, and apply Solana snapshots to quickly synchronize with the devnet. This is essential for RPC nodes that need to serve current blockchain state without waiting for full block replay.

## New CLI Options

### `--snapshot-source SOURCE`
Controls how snapshots are obtained:
- `auto` (default): Auto-discover snapshots from upstream RPC
- `mirror`: Use a specific snapshot mirror URL
- `none`: Disable snapshot bootstrap

### `--snapshot-mirror URL`
Specifies a custom snapshot mirror URL when using `--snapshot-source mirror`.

### `--upstream-rpc-url URL`
Upstream RPC endpoint for snapshot discovery and catch-up operations. Used for:
- Discovering the latest snapshot via `getHighestSnapshotSlot`
- Querying current slot for bootstrap requirement detection
- Future: Real-time catch-up after snapshot application

### `--allow-stale-rpc`
Allow RPC server to start before the node is fully caught up. By default, RPC is only enabled after the node has synchronized to near the current slot.

## Usage Examples

### Basic devnet RPC node with auto snapshot bootstrap:
```bash
./slonana_validator --network-id devnet --snapshot-source auto
```

### Using a custom upstream RPC:
```bash
./slonana_validator --network-id devnet \
  --upstream-rpc-url https://api.devnet.solana.com \
  --snapshot-source auto
```

### Using a custom snapshot mirror:
```bash
./slonana_validator --network-id devnet \
  --snapshot-source mirror \
  --snapshot-mirror https://custom-mirror.example.com
```

### Disable snapshot bootstrap:
```bash
./slonana_validator --network-id devnet --snapshot-source none
```

## Bootstrap Process

When snapshot bootstrap is enabled (`--snapshot-source auto` or `mirror`), the validator performs the following steps:

1. **Bootstrap Decision**: Checks if bootstrap is needed by comparing local ledger state with upstream
2. **Snapshot Discovery**: Queries upstream RPC for the latest full snapshot slot
3. **Download**: Downloads the snapshot archive from the mirror
4. **Verification**: Validates snapshot integrity
5. **Application**: Extracts and applies the snapshot to the local ledger
6. **Graceful Fallback**: If any step fails, continues without snapshot bootstrap

## Architecture

### Key Components

- **SnapshotBootstrapManager** (`src/validator/snapshot_bootstrap.cpp`): Main bootstrap orchestration
- **HttpClient** (`src/network/http_client.cpp`): HTTP client for RPC calls and downloads
- **Integration**: Bootstrap runs during validator initialization, before RPC server startup

### Configuration

Bootstrap is automatically enabled for devnet RPC nodes and integrates with the existing snapshot management system. The configuration is stored in `ValidatorConfig` and passed through the validator initialization chain.

### Error Handling

The bootstrap process is designed to be robust:
- Network failures are handled gracefully
- Invalid responses don't prevent validator startup
- Bootstrap failures fall back to normal ledger replay
- Progress callbacks provide visibility into the process

## Testing

The snapshot bootstrap functionality includes comprehensive tests covering:
- Configuration parsing and validation
- Bootstrap manager initialization
- Bootstrap requirement detection
- Graceful error handling
- Directory management
- Progress tracking

Run tests with:
```bash
cd build && ./slonana_snapshot_tests
```

## Future Enhancements

The current implementation provides the foundation for more advanced features:

1. **Incremental Snapshots**: Support for incremental snapshot consumption
2. **Resume Downloads**: Resume interrupted snapshot downloads
3. **Multiple Mirrors**: Failover between multiple snapshot mirrors
4. **Verification**: Enhanced snapshot verification with signatures
5. **Gossip Integration**: Discover snapshots via gossip protocol
6. **Real-time Catch-up**: Implement post-snapshot catch-up mechanisms

## Troubleshooting

### Common Issues

**Bootstrap fails with "Invalid RPC response format"**:
- Check that `--upstream-rpc-url` points to a valid Solana RPC endpoint
- Verify network connectivity
- The validator will continue without bootstrap

**Snapshot download fails**:
- Verify the snapshot mirror is accessible
- Check available disk space in the ledger directory
- The validator will fall back to normal operation

**Bootstrap is skipped unexpectedly**:
- Ensure `--network-id devnet` is specified
- Check that `--snapshot-source` is not set to `none`
- RPC must be enabled (`--enable-rpc` is default)

### Debug Output

Enable verbose logging to see bootstrap progress:
```bash
./slonana_validator --network-id devnet --snapshot-source auto --log-level debug
```

Bootstrap progress is logged with `[Bootstrap]` prefix, making it easy to track the process.