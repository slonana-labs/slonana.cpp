# Homebrew Slonana

This tap provides formulae for Slonana C++ Solana validator.

## Installation

```bash
brew tap slonana-labs/slonana
brew install slonana-validator
```

## Usage

```bash
# Start the validator service
brew services start slonana-validator

# Generate identity keys
slonana-genesis create-identity --output-dir $(brew --prefix)/var/lib/slonana

# Check status
brew services list | grep slonana
```

## Support

For issues and support, visit: https://github.com/slonana-labs/slonana.cpp
