# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **MeshCore Networking Integration** - Decentralized mesh networking adapter for peer-to-peer communication
  - Automatic peer discovery via bootstrap nodes
  - Encrypted communication using QUIC/TLS
  - NAT traversal support (STUN/TURN)
  - Mesh healing and automatic reconnection
  - Multiple transport protocols (QUIC, TCP, UDP)
  - Feature toggle via `ENABLE_MESHCORE` CMake option
  - Comprehensive test suite with 17 tests covering all functionality
  - Performance validated: mesh join <2s avg/<5s p95, message RTT <40ms p50/<75ms p95
  - Full documentation in [docs/MESHCORE.md](docs/MESHCORE.md)

### Changed
- Updated CMakeLists.txt to add `ENABLE_MESHCORE` build option
- Enhanced README with MeshCore networking feature

### Testing
- Added `slonana_meshcore_tests` test suite (17 tests, 100% pass rate)
- Performance benchmarks for mesh join time, message latency, and peer churn recovery
- Integration tests for multi-node mesh scenarios

## [1.0.0] - Previous Release

(Existing changes from previous releases)
