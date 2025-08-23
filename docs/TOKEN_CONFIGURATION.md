# Configurable Token Names in Slonana

Slonana supports configurable token names, allowing you to customize both the main token symbol and the base unit name for your blockchain network.

## Overview

- **Token Symbol**: The main token symbol displayed to users (e.g., "SOL", "SLON", "MYTOKEN")
- **Base Unit Name**: The smallest denomination unit name (e.g., "lamports", "aldrins", "bits")

## Default Configurations

### Mainnet
- Token Symbol: `SLON`
- Base Unit Name: `aldrins`

### Testnet
- Token Symbol: `tSLON`
- Base Unit Name: `taldrins`

### Devnet
- Token Symbol: `SOL` (backward compatibility)
- Base Unit Name: `lamports` (backward compatibility)

## CLI Usage

### Creating Genesis Configuration with Custom Token Names

```bash
# Create mainnet genesis with custom token names
./slonana-genesis create-network \
  --network-type mainnet \
  --token-symbol SLON \
  --base-unit-name aldrins \
  --initial-supply 1000000000 \
  --output mainnet-genesis.json

# Create custom network with your own token
./slonana-genesis create-network \
  --network-type testnet \
  --token-symbol MYTOKEN \
  --base-unit-name bits \
  --initial-supply 500000000 \
  --output custom-genesis.json
```

### Viewing Token Configuration

```bash
# Display genesis information including token configuration
./slonana-genesis info genesis-config.json
```

Example output:
```
=== Economic Parameters ===
Token Symbol: SLON
Base Unit: aldrins
Total Supply: 1000000000 SLON
Min Validator Stake: 1000000 aldrins
Min Delegation: 1000 aldrins
```

## Configuration Files

Token names are stored in the genesis configuration JSON:

```json
{
  "economics": {
    "token_symbol": "SLON",
    "base_unit_name": "aldrins",
    "total_supply": 1000000000,
    "min_validator_stake": 1000000,
    "min_delegation": 1000
  }
}
```

## Use Cases

### Custom Blockchain Networks
- **DeFi Projects**: Use meaningful token names like "DEFI" with "cents"
- **Gaming**: Use themed names like "GOLD" with "pieces"
- **Enterprise**: Use branded names like "CORP" with "units"

### Regional Deployments
- **Asian Markets**: Use "SLON" with "aldrins" (inspired by astronaut Buzz Aldrin)
- **Testing**: Use prefixed names like "tSLON" with "taldrins"

## Examples

### Gaming Network
```bash
./slonana-genesis create-network \
  --network-type custom \
  --token-symbol GOLD \
  --base-unit-name pieces \
  --initial-supply 1000000000 \
  --output gaming-genesis.json
```

### DeFi Network
```bash
./slonana-genesis create-network \
  --network-type mainnet \
  --token-symbol DEFI \
  --base-unit-name cents \
  --initial-supply 2100000000 \
  --output defi-genesis.json
```

### Corporate Network
```bash
./slonana-genesis create-network \
  --network-type mainnet \
  --token-symbol CORP \
  --base-unit-name units \
  --initial-supply 500000000 \
  --output corporate-genesis.json
```

## Backward Compatibility

The system maintains full backward compatibility:
- Existing configurations without token names will use default values
- Devnet continues to use "SOL" and "lamports" by default
- All existing CLI commands work unchanged

## Technical Details

### Configuration Structure

The token configuration is part of the `EconomicConfig` structure:

```cpp
struct EconomicConfig {
    std::string token_symbol = "SOL";      // Main token symbol
    std::string base_unit_name = "lamports"; // Base unit name
    uint64_t total_supply = 1000000000;
    uint64_t min_validator_stake = 1000000; // In base units
    uint64_t min_delegation = 1000;         // In base units
    // ... other economic parameters
};
```

### CLI Integration

All CLI output uses the configured token names:
- Stake amounts are displayed with the base unit name
- Token supply is displayed with the token symbol
- Configuration info shows both token symbol and base unit name

This provides a fully customizable token experience while maintaining the underlying Slonana architecture.