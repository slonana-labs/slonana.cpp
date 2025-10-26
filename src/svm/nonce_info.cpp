#include "svm/nonce_info.h"
#include <cstring>
#include <iostream>

namespace slonana {
namespace svm {

// NonceData implementation
std::vector<uint8_t> NonceData::serialize() const {
  std::vector<uint8_t> result;
  result.reserve(32 + 32 + 8); // authority + blockhash + fee_calculator

  // Add authority (32 bytes)
  result.insert(result.end(), authority.begin(), authority.end());

  // Add blockhash (32 bytes)
  result.insert(result.end(), blockhash.begin(), blockhash.end());

  // Add fee_calculator (8 bytes, little-endian)
  for (int i = 0; i < 8; ++i) {
    result.push_back((fee_calculator >> (i * 8)) & 0xFF);
  }

  return result;
}

NonceData NonceData::deserialize(const std::vector<uint8_t> &data) {
  NonceData nonce_data;

  if (data.size() < 72) { // 32 + 32 + 8
    return nonce_data;    // Invalid data
  }

  // Extract authority
  nonce_data.authority.assign(data.begin(), data.begin() + 32);

  // Extract blockhash
  nonce_data.blockhash.assign(data.begin() + 32, data.begin() + 64);

  // Extract fee_calculator
  nonce_data.fee_calculator = 0;
  for (int i = 0; i < 8; ++i) {
    nonce_data.fee_calculator |= static_cast<Lamports>(data[64 + i]) << (i * 8);
  }

  return nonce_data;
}

// NonceInfo implementation
NonceInfo::NonceInfo(const PublicKey &address, const AccountInfo &account)
    : address_(address), account_(account) {
  parse_account_data();
}

bool NonceInfo::is_valid_authority(const PublicKey &authority) const {
  if (state_ != NonceState::INITIALIZED || !nonce_data_) {
    return false;
  }
  return nonce_data_->authority == authority;
}

bool NonceInfo::can_advance_nonce(const Hash &new_blockhash) const {
  if (state_ != NonceState::INITIALIZED || !nonce_data_) {
    return false;
  }
  // Nonce can be advanced if the new blockhash is different
  return nonce_data_->blockhash != new_blockhash;
}

bool NonceInfo::advance_nonce(const Hash &new_blockhash,
                              Lamports fee_calculator) {
  if (!can_advance_nonce(new_blockhash)) {
    return false;
  }

  nonce_data_->blockhash = new_blockhash;
  nonce_data_->fee_calculator = fee_calculator;
  modified_ = true;
  update_account_data();

  return true;
}

bool NonceInfo::initialize_nonce(const PublicKey &authority,
                                 const Hash &recent_blockhash,
                                 Lamports fee_calculator) {
  if (state_ == NonceState::INITIALIZED) {
    return false; // Already initialized
  }

  // Check account size and ownership (simplified)
  if (account_.data.size() < NONCE_ACCOUNT_SIZE) {
    return false;
  }

  nonce_data_ = NonceData(authority, recent_blockhash, fee_calculator);
  state_ = NonceState::INITIALIZED;
  modified_ = true;
  update_account_data();

  return true;
}

bool NonceInfo::authorize_nonce(const PublicKey &current_authority,
                                const PublicKey &new_authority) {
  if (!is_valid_authority(current_authority)) {
    return false;
  }

  nonce_data_->authority = new_authority;
  modified_ = true;
  update_account_data();

  return true;
}

bool NonceInfo::withdraw_nonce(const PublicKey &authority, Lamports amount,
                               Lamports remaining_balance) {
  if (!is_valid_authority(authority)) {
    return false;
  }

  if (account_.lamports < amount) {
    return false; // Insufficient funds
  }

  account_.lamports = remaining_balance;
  modified_ = true;

  return true;
}

bool NonceInfo::validate() const {
  // Check account ownership (should be system program)
  // For now, we skip this check (simplified)

  // Check account size
  if (account_.data.size() < NONCE_ACCOUNT_SIZE) {
    return false;
  }

  // If initialized, validate nonce data
  if (state_ == NonceState::INITIALIZED) {
    return nonce_data_.has_value();
  }

  return true;
}

AccountInfo NonceInfo::get_updated_account() const { return account_; }

bool NonceInfo::is_nonce_account(const AccountInfo &account) {
  // Check account size and ownership
  if (account.data.size() < NONCE_ACCOUNT_SIZE) {
    return false;
  }

  // Check that account is owned by system program
  PublicKey system_program_id = nonce_utils::get_system_program_id();
  return account.owner == system_program_id;
}

std::optional<NonceData>
NonceInfo::parse_nonce_data(const std::vector<uint8_t> &data) {
  if (data.size() < 4) {
    return std::nullopt; // Not enough data for state
  }

  // First 4 bytes indicate state (0 = uninitialized, 1 = initialized)
  uint32_t state = 0;
  for (int i = 0; i < 4; ++i) {
    state |= static_cast<uint32_t>(data[i]) << (i * 8);
  }

  if (state != 1) {
    return std::nullopt; // Not initialized
  }

  if (data.size() < 4 + 72) {
    return std::nullopt; // Not enough data for nonce data
  }

  std::vector<uint8_t> nonce_bytes(data.begin() + 4, data.begin() + 4 + 72);
  return NonceData::deserialize(nonce_bytes);
}

std::vector<uint8_t>
NonceInfo::create_nonce_account_data(const NonceData &nonce_data) {
  std::vector<uint8_t> result;
  result.reserve(NONCE_ACCOUNT_SIZE);

  // Add state (4 bytes) - 1 for initialized
  uint32_t state = 1;
  for (int i = 0; i < 4; ++i) {
    result.push_back((state >> (i * 8)) & 0xFF);
  }

  // Add nonce data
  auto nonce_bytes = nonce_data.serialize();
  result.insert(result.end(), nonce_bytes.begin(), nonce_bytes.end());

  // Pad to required size if needed
  while (result.size() < NONCE_ACCOUNT_SIZE) {
    result.push_back(0);
  }

  return result;
}

void NonceInfo::parse_account_data() {
  auto nonce_data_opt = parse_nonce_data(account_.data);
  if (nonce_data_opt) {
    state_ = NonceState::INITIALIZED;
    nonce_data_ = *nonce_data_opt;
  } else {
    state_ = NonceState::UNINITIALIZED;
    nonce_data_ = std::nullopt;
  }
}

void NonceInfo::update_account_data() {
  if (state_ == NonceState::INITIALIZED && nonce_data_) {
    account_.data = create_nonce_account_data(*nonce_data_);
  }
}

// Utility functions
namespace nonce_utils {

NonceValidationResult validate_nonce(const NonceInfo &nonce_info,
                                     const PublicKey &authority,
                                     const Hash &current_blockhash) {
  if (!nonce_info.validate()) {
    return NonceValidationResult::INVALID_ACCOUNT_DATA;
  }

  if (nonce_info.get_state() != NonceState::INITIALIZED) {
    return NonceValidationResult::NONCE_NOT_INITIALIZED;
  }

  if (!nonce_info.is_valid_authority(authority)) {
    return NonceValidationResult::INVALID_AUTHORITY;
  }

  if (!nonce_info.can_advance_nonce(current_blockhash)) {
    return NonceValidationResult::BLOCKHASH_NOT_EXPIRED;
  }

  return NonceValidationResult::SUCCESS;
}

PublicKey get_system_program_id() {
  // Return system program ID (all zeros for now)
  PublicKey system_id(32, 0);
  return system_id;
}

Lamports calculate_nonce_minimum_balance() {
  // Simplified calculation - in practice this would use RentCalculator
  return 890880; // Approximate minimum for nonce account on mainnet
}

std::string format_validation_result(NonceValidationResult result) {
  switch (result) {
  case NonceValidationResult::SUCCESS:
    return "Success";
  case NonceValidationResult::ACCOUNT_NOT_FOUND:
    return "Account not found";
  case NonceValidationResult::INVALID_ACCOUNT_OWNER:
    return "Invalid account owner";
  case NonceValidationResult::INVALID_ACCOUNT_DATA:
    return "Invalid account data";
  case NonceValidationResult::NONCE_NOT_INITIALIZED:
    return "Nonce not initialized";
  case NonceValidationResult::INVALID_AUTHORITY:
    return "Invalid authority";
  case NonceValidationResult::BLOCKHASH_NOT_EXPIRED:
    return "Blockhash not expired";
  case NonceValidationResult::INSUFFICIENT_FUNDS_FOR_FEE:
    return "Insufficient funds for fee";
  default:
    return "Unknown error";
  }
}

} // namespace nonce_utils

} // namespace svm
} // namespace slonana