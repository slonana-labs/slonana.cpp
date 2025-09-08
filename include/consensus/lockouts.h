#pragma once

#include "common/types.h"
#include <vector>
#include <chrono>

namespace slonana {
namespace consensus {

using namespace slonana::common;

/**
 * Lockout structure representing a vote with lockout period
 * Compatible with Agave's lockout implementation
 */
struct Lockout {
    uint64_t slot;
    uint32_t confirmation_count;
    
    Lockout() = default;
    Lockout(uint64_t slot_num, uint32_t conf_count = 0) 
        : slot(slot_num), confirmation_count(conf_count) {}
    
    /**
     * Calculate the lockout period for this vote
     * @return lockout period in ticks
     */
    uint64_t lockout_period() const {
        // Agave-compatible lockout calculation: 2^confirmation_count
        static constexpr uint64_t TOWER_BFT_MAX_LOCKOUT = 1ULL << 32;
        return std::min(static_cast<uint64_t>(1ULL << confirmation_count), TOWER_BFT_MAX_LOCKOUT);
    }
    
    /**
     * Check if this lockout prevents voting on another slot
     * @param other_slot Slot to check against
     * @return true if other_slot is locked out
     */
    bool is_locked_out_at_slot(uint64_t other_slot) const {
        // A slot is locked out if it's within the lockout period but after the voted slot
        return other_slot > slot && other_slot <= slot + lockout_period();
    }
    
    /**
     * Check if this lockout expires after a given slot
     * @param current_slot Current slot number
     * @return true if lockout has expired
     */
    bool is_expired_at_slot(uint64_t current_slot) const {
        return current_slot >= slot + lockout_period();
    }
    
    /**
     * Get the slot where this lockout expires
     * @return expiration slot
     */
    uint64_t expiration_slot() const {
        return slot + lockout_period();
    }
    
    /**
     * Serialize lockout to bytes
     * @return serialized lockout data
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * Deserialize lockout from bytes
     * @param data Serialized data
     * @param offset Offset in data to start reading
     * @return number of bytes consumed, 0 on error
     */
    size_t deserialize(const std::vector<uint8_t>& data, size_t offset = 0);
    
    /**
     * Compare lockouts by slot
     */
    bool operator<(const Lockout& other) const {
        return slot < other.slot;
    }
    
    bool operator==(const Lockout& other) const {
        return slot == other.slot && confirmation_count == other.confirmation_count;
    }
};

/**
 * Lockout manager for handling collections of lockouts
 */
class LockoutManager {
private:
    std::vector<Lockout> lockouts_;
    
public:
    /**
     * Add a new lockout
     * @param lockout Lockout to add
     */
    void add_lockout(const Lockout& lockout);
    
    /**
     * Remove expired lockouts
     * @param current_slot Current slot number
     * @return number of lockouts removed
     */
    size_t remove_expired_lockouts(uint64_t current_slot);
    
    /**
     * Check if any lockout prevents voting on a slot
     * @param slot Slot to check
     * @return true if slot is locked out
     */
    bool is_slot_locked_out(uint64_t slot) const;
    
    /**
     * Get all active lockouts at a given slot
     * @param current_slot Current slot number
     * @return vector of active lockouts
     */
    std::vector<Lockout> get_active_lockouts(uint64_t current_slot) const;
    
    /**
     * Get lockout for a specific slot
     * @param slot Slot to find lockout for
     * @return pointer to lockout, nullptr if not found
     */
    const Lockout* get_lockout_for_slot(uint64_t slot) const;
    
    /**
     * Update confirmation count for a slot
     * @param slot Slot to update
     * @param new_count New confirmation count
     * @return true if update successful
     */
    bool update_confirmation_count(uint64_t slot, uint32_t new_count);
    
    /**
     * Get all lockouts
     * @return const reference to lockouts vector
     */
    const std::vector<Lockout>& get_lockouts() const {
        return lockouts_;
    }
    
    /**
     * Clear all lockouts
     */
    void clear() {
        lockouts_.clear();
    }
    
    /**
     * Get the number of lockouts
     * @return number of lockouts
     */
    size_t size() const {
        return lockouts_.size();
    }
    
    /**
     * Check if there are any lockouts
     * @return true if no lockouts
     */
    bool empty() const {
        return lockouts_.empty();
    }
    
    /**
     * Serialize all lockouts
     * @return serialized lockout data
     */
    std::vector<uint8_t> serialize() const;
    
    /**
     * Deserialize lockouts
     * @param data Serialized data
     * @return true if deserialization successful
     */
    bool deserialize(const std::vector<uint8_t>& data);
};

/**
 * Utility functions for lockout calculations
 */
namespace lockout_utils {
    /**
     * Calculate the maximum lockout distance for a given confirmation count
     * @param confirmation_count Number of confirmations
     * @return maximum lockout distance
     */
    uint64_t calculate_max_lockout_distance(uint32_t confirmation_count);
    
    /**
     * Check if two slots conflict based on lockout rules
     * @param slot1 First slot
     * @param lockout1 Lockout for first slot
     * @param slot2 Second slot
     * @return true if slots conflict
     */
    bool slots_conflict(uint64_t slot1, const Lockout& lockout1, uint64_t slot2);
    
    /**
     * Find the optimal confirmation count for a given situation
     * @param current_slot Current slot
     * @param target_slot Target slot to vote on
     * @param existing_lockouts Existing lockouts to consider
     * @return optimal confirmation count
     */
    uint32_t find_optimal_confirmation_count(
        uint64_t current_slot,
        uint64_t target_slot,
        const std::vector<Lockout>& existing_lockouts
    );
    
    /**
     * Validate a set of lockouts for consistency
     * @param lockouts Lockouts to validate
     * @return true if lockouts are consistent
     */
    bool validate_lockouts(const std::vector<Lockout>& lockouts);
}

} // namespace consensus
} // namespace slonana