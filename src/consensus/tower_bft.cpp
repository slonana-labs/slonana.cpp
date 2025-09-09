#include "consensus/tower_bft.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace slonana {
namespace consensus {

// Tower implementation
Tower::Tower() : root_slot_(0), last_vote_slot_(0) {
  tower_slots_.reserve(MAX_TOWER_HEIGHT);
}

Tower::Tower(uint64_t root_slot)
    : root_slot_(root_slot), last_vote_slot_(root_slot) {
  tower_slots_.reserve(MAX_TOWER_HEIGHT);
}

bool Tower::can_vote_on_slot(uint64_t slot) const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  // Cannot vote on slots before root
  if (slot < root_slot_) {
    return false;
  }

  // Cannot vote on slots we've already voted on
  if (slot <= last_vote_slot_) {
    return false;
  }

  // Check if slot is locked out by any existing vote
  for (const auto &tower_slot : tower_slots_) {
    uint64_t lockout_period =
        calculate_lockout_period(tower_slot.confirmation_count);
    // A slot is locked out if it's within the lockout period but after the
    // voted slot
    if (slot > tower_slot.slot && slot <= tower_slot.slot + lockout_period) {
      return false;
    }
  }

  return true;
}

uint64_t Tower::calculate_lockout_period(size_t confirmation_count) const {
  // Agave-compatible lockout calculation: 2^confirmation_count
  return std::min(static_cast<uint64_t>(1ULL << confirmation_count),
                  TOWER_BFT_MAX_LOCKOUT);
}

bool Tower::is_slot_locked_out(uint64_t slot) const {
  return !can_vote_on_slot(slot);
}

void Tower::record_vote(uint64_t slot) {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  // Input validation
  if (slot == 0) {
    throw std::invalid_argument("Cannot vote on slot 0");
  }

  // Check if voting is allowed (without acquiring lock again)
  if (slot < root_slot_ || slot <= last_vote_slot_) {
    throw std::runtime_error("Cannot vote on slot " + std::to_string(slot) +
                             " - violates Tower BFT rules");
  }

  // Check if slot is locked out by any existing vote
  for (const auto &tower_slot : tower_slots_) {
    uint64_t lockout_period =
        calculate_lockout_period(tower_slot.confirmation_count);
    if (slot > tower_slot.slot && slot <= tower_slot.slot + lockout_period) {
      throw std::runtime_error("Cannot vote on slot " + std::to_string(slot) +
                               " - locked out by slot " +
                               std::to_string(tower_slot.slot));
    }
  }

  // Add new tower slot
  tower_slots_.emplace_back(slot, 0);
  last_vote_slot_ = slot;

  // Maintain maximum tower height
  if (tower_slots_.size() > MAX_TOWER_HEIGHT) {
    // Remove oldest slot and update root
    auto oldest = tower_slots_.front();
    tower_slots_.erase(tower_slots_.begin());
    root_slot_ = oldest.slot;
  }

  std::cout << "🗳️  Tower BFT: Recorded vote on slot " << slot
            << " (tower height: " << tower_slots_.size() << ")" << std::endl;
}

uint64_t Tower::get_vote_threshold(uint64_t slot) const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  // Find the tower slot for this slot
  for (const auto &tower_slot : tower_slots_) {
    if (tower_slot.slot == slot) {
      // Threshold increases with confirmation count
      return TOWER_BFT_THRESHOLD + tower_slot.confirmation_count;
    }
  }

  return TOWER_BFT_THRESHOLD;
}

size_t Tower::get_tower_height() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);
  return tower_slots_.size();
}

uint64_t Tower::get_root_slot() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);
  return root_slot_;
}

uint64_t Tower::get_last_vote_slot() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);
  return last_vote_slot_;
}

std::vector<TowerSlot> Tower::get_tower_slots() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);
  return tower_slots_;
}

bool Tower::is_valid() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  // Check that slots are in ascending order
  for (size_t i = 1; i < tower_slots_.size(); ++i) {
    if (tower_slots_[i].slot <= tower_slots_[i - 1].slot) {
      return false;
    }
  }

  // Check that no slots conflict with lockouts
  for (size_t i = 0; i < tower_slots_.size(); ++i) {
    for (size_t j = i + 1; j < tower_slots_.size(); ++j) {
      uint64_t lockout_period =
          calculate_lockout_period(tower_slots_[i].confirmation_count);
      // A later slot conflicts if it's within the lockout period of an earlier
      // slot
      if (tower_slots_[j].slot > tower_slots_[i].slot &&
          tower_slots_[j].slot <= tower_slots_[i].slot + lockout_period) {
        return false;
      }
    }
  }

  return true;
}

void Tower::reset_to_root(uint64_t new_root_slot) {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  root_slot_ = new_root_slot;
  last_vote_slot_ = new_root_slot;
  tower_slots_.clear();

  std::cout << "🔄 Tower BFT: Reset to root slot " << new_root_slot
            << std::endl;
}

uint64_t Tower::get_slot_lockout(uint64_t slot) const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  for (const auto &tower_slot : tower_slots_) {
    if (tower_slot.slot == slot) {
      return calculate_lockout_period(tower_slot.confirmation_count);
    }
  }

  return 0;
}

bool Tower::can_switch_to_fork(uint64_t slot) const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  // Check if switching to this slot would violate any lockouts
  for (const auto &tower_slot : tower_slots_) {
    uint64_t lockout_period =
        calculate_lockout_period(tower_slot.confirmation_count);
    uint64_t lockout_end = tower_slot.slot + lockout_period;

    // If the new slot is within the lockout period, we cannot switch
    if (slot <= lockout_end && slot > tower_slot.slot) {
      return false;
    }
  }

  return true;
}

void Tower::update_confirmation_count(uint64_t slot, uint64_t new_count) {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  for (auto &tower_slot : tower_slots_) {
    if (tower_slot.slot == slot) {
      tower_slot.confirmation_count = new_count;
      std::cout << "📈 Tower BFT: Updated confirmation count for slot " << slot
                << " to " << new_count << std::endl;
      break;
    }
  }
}

std::vector<uint8_t> Tower::serialize() const {
  std::lock_guard<std::mutex> lock(tower_mutex_);

  std::vector<uint8_t> result;

  // Serialize root slot (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((root_slot_ >> (i * 8)) & 0xFF));
  }

  // Serialize last vote slot (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((last_vote_slot_ >> (i * 8)) & 0xFF));
  }

  // Serialize tower slots count (4 bytes)
  uint32_t slot_count = static_cast<uint32_t>(tower_slots_.size());
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((slot_count >> (i * 8)) & 0xFF));
  }

  // Serialize each tower slot
  for (const auto &tower_slot : tower_slots_) {
    // Slot number (8 bytes)
    for (int i = 0; i < 8; ++i) {
      result.push_back(
          static_cast<uint8_t>((tower_slot.slot >> (i * 8)) & 0xFF));
    }

    // Confirmation count (8 bytes)
    for (int i = 0; i < 8; ++i) {
      result.push_back(static_cast<uint8_t>(
          (tower_slot.confirmation_count >> (i * 8)) & 0xFF));
    }
  }

  return result;
}

bool Tower::deserialize(const std::vector<uint8_t> &data) {
  if (data.size() < 20) { // Minimum size: 8 + 8 + 4 = 20 bytes
    return false;
  }

  std::lock_guard<std::mutex> lock(tower_mutex_);

  size_t offset = 0;

  // Deserialize root slot
  root_slot_ = 0;
  for (int i = 0; i < 8; ++i) {
    root_slot_ |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;

  // Deserialize last vote slot
  last_vote_slot_ = 0;
  for (int i = 0; i < 8; ++i) {
    last_vote_slot_ |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;

  // Deserialize slot count
  uint32_t slot_count = 0;
  for (int i = 0; i < 4; ++i) {
    slot_count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }
  offset += 4;

  // Check if we have enough data
  if (data.size() < offset + (slot_count * 16)) {
    return false;
  }

  // Deserialize tower slots
  tower_slots_.clear();
  tower_slots_.reserve(slot_count);

  for (uint32_t i = 0; i < slot_count; ++i) {
    uint64_t slot = 0;
    for (int j = 0; j < 8; ++j) {
      slot |= static_cast<uint64_t>(data[offset + j]) << (j * 8);
    }
    offset += 8;

    uint64_t confirmation_count = 0;
    for (int j = 0; j < 8; ++j) {
      confirmation_count |= static_cast<uint64_t>(data[offset + j]) << (j * 8);
    }
    offset += 8;

    tower_slots_.emplace_back(slot, confirmation_count);
  }

  return true;
}

// VoteState implementation
VoteState::VoteState() : root_slot(0), commission(0) {
  authorized_voter.resize(32, 0);
  node_pubkey.resize(32, 0);
}

VoteState::VoteState(const std::vector<uint8_t> &voter_pubkey,
                     const std::vector<uint8_t> &node_pubkey_param)
    : root_slot(0), commission(0) {
  authorized_voter = voter_pubkey;
  if (authorized_voter.size() != 32) {
    authorized_voter.resize(32, 0);
  }

  node_pubkey = node_pubkey_param;
  if (node_pubkey.size() != 32) {
    node_pubkey.resize(32, 0);
  }
}

bool VoteState::is_valid_vote(uint64_t slot) const {
  // Cannot vote on slots before root
  if (slot < root_slot) {
    return false;
  }

  // Cannot vote on slots we've already voted on
  uint64_t last_slot = last_voted_slot();
  if (last_slot > 0 && slot <= last_slot) {
    return false;
  }

  // Check if slot conflicts with any existing lockouts
  for (const auto &lockout : votes) {
    if (lockout.is_locked_out_at_slot(slot)) {
      return false;
    }
  }

  return true;
}

uint64_t VoteState::last_voted_slot() const {
  if (votes.empty()) {
    return 0;
  }

  uint64_t max_slot = 0;
  for (const auto &vote : votes) {
    if (vote.slot > max_slot) {
      max_slot = vote.slot;
    }
  }

  return max_slot;
}

void VoteState::process_vote(uint64_t slot, uint64_t timestamp) {
  if (!is_valid_vote(slot)) {
    throw std::runtime_error("Invalid vote on slot " + std::to_string(slot));
  }

  // Add new vote with initial confirmation count of 0
  votes.emplace_back(slot, 0);

  // Remove expired votes
  auto current_time = std::chrono::steady_clock::now();
  votes.erase(std::remove_if(votes.begin(), votes.end(),
                             [slot](const Lockout &lockout) {
                               return lockout.is_expired_at_slot(slot);
                             }),
              votes.end());

  std::cout << "✅ Vote State: Processed vote on slot " << slot
            << " (active votes: " << votes.size() << ")" << std::endl;
}

std::vector<uint64_t> VoteState::slots_in_lockout(uint64_t current_slot) const {
  std::vector<uint64_t> locked_slots;

  for (const auto &vote : votes) {
    if (!vote.is_expired_at_slot(current_slot)) {
      locked_slots.push_back(vote.slot);
    }
  }

  return locked_slots;
}

void VoteState::update_root_slot(uint64_t new_root_slot) {
  root_slot = new_root_slot;

  // Remove votes for slots before the new root
  votes.erase(std::remove_if(votes.begin(), votes.end(),
                             [new_root_slot](const Lockout &lockout) {
                               return lockout.slot < new_root_slot;
                             }),
              votes.end());

  std::cout << "🌳 Vote State: Updated root slot to " << new_root_slot
            << " (remaining votes: " << votes.size() << ")" << std::endl;
}

std::vector<uint8_t> VoteState::serialize() const {
  std::vector<uint8_t> result;

  // Root slot (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((root_slot >> (i * 8)) & 0xFF));
  }

  // Commission (8 bytes)
  for (int i = 0; i < 8; ++i) {
    result.push_back(static_cast<uint8_t>((commission >> (i * 8)) & 0xFF));
  }

  // Authorized voter (32 bytes)
  result.insert(result.end(), authorized_voter.begin(), authorized_voter.end());

  // Node pubkey (32 bytes)
  result.insert(result.end(), node_pubkey.begin(), node_pubkey.end());

  // Vote count (4 bytes)
  uint32_t vote_count = static_cast<uint32_t>(votes.size());
  for (int i = 0; i < 4; ++i) {
    result.push_back(static_cast<uint8_t>((vote_count >> (i * 8)) & 0xFF));
  }

  // Votes
  for (const auto &vote : votes) {
    auto vote_data = vote.serialize();
    result.insert(result.end(), vote_data.begin(), vote_data.end());
  }

  return result;
}

bool VoteState::deserialize(const std::vector<uint8_t> &data) {
  if (data.size() < 84) { // Minimum size: 8 + 8 + 32 + 32 + 4 = 84 bytes
    return false;
  }

  size_t offset = 0;

  // Root slot
  root_slot = 0;
  for (int i = 0; i < 8; ++i) {
    root_slot |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;

  // Commission
  commission = 0;
  for (int i = 0; i < 8; ++i) {
    commission |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
  }
  offset += 8;

  // Authorized voter
  authorized_voter.assign(data.begin() + offset, data.begin() + offset + 32);
  offset += 32;

  // Node pubkey
  node_pubkey.assign(data.begin() + offset, data.begin() + offset + 32);
  offset += 32;

  // Vote count
  uint32_t vote_count = 0;
  for (int i = 0; i < 4; ++i) {
    vote_count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  }
  offset += 4;

  // Votes
  votes.clear();
  votes.reserve(vote_count);

  for (uint32_t i = 0; i < vote_count; ++i) {
    Lockout lockout;
    size_t consumed = lockout.deserialize(data, offset);
    if (consumed == 0) {
      return false;
    }
    votes.push_back(lockout);
    offset += consumed;
  }

  return true;
}

// TowerBftManager implementation
TowerBftManager::TowerBftManager(uint64_t initial_root_slot) {
  tower_ = std::make_unique<Tower>(initial_root_slot);
  vote_state_ = std::make_unique<VoteState>();
}

bool TowerBftManager::initialize(const std::vector<uint8_t> &voter_pubkey,
                                 const std::vector<uint8_t> &node_pubkey) {
  std::lock_guard<std::mutex> lock(manager_mutex_);

  vote_state_ = std::make_unique<VoteState>(voter_pubkey, node_pubkey);

  std::cout << "🏗️  Tower BFT Manager: Initialized with root slot "
            << tower_->get_root_slot() << std::endl;

  return true;
}

bool TowerBftManager::process_slot(uint64_t slot, uint64_t parent_slot) {
  std::lock_guard<std::mutex> lock(manager_mutex_);

  bool can_vote =
      tower_->can_vote_on_slot(slot) && vote_state_->is_valid_vote(slot);

  if (vote_callback_) {
    vote_callback_(slot, can_vote);
  }

  return can_vote;
}

bool TowerBftManager::cast_vote(uint64_t slot) {
  std::lock_guard<std::mutex> lock(manager_mutex_);

  try {
    if (!tower_->can_vote_on_slot(slot) || !vote_state_->is_valid_vote(slot)) {
      return false;
    }

    tower_->record_vote(slot);
    vote_state_->process_vote(
        slot, 0); // timestamp not used in current implementation

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Error casting vote on slot " << slot << ": " << e.what()
              << std::endl;
    return false;
  }
}

void TowerBftManager::set_vote_callback(
    std::function<void(uint64_t slot, bool can_vote)> callback) {
  std::lock_guard<std::mutex> lock(manager_mutex_);
  vote_callback_ = std::move(callback);
}

TowerBftManager::TowerStats TowerBftManager::get_stats() const {
  std::lock_guard<std::mutex> lock(manager_mutex_);

  TowerStats stats;
  stats.tower_height = tower_->get_tower_height();
  stats.root_slot = tower_->get_root_slot();
  stats.last_vote_slot = tower_->get_last_vote_slot();
  stats.lockout_count = vote_state_->votes.size();

  return stats;
}

const Tower &TowerBftManager::get_tower() const { return *tower_; }

const VoteState &TowerBftManager::get_vote_state() const {
  return *vote_state_;
}

} // namespace consensus
} // namespace slonana