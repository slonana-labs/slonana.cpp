#pragma once

#include "common/types.h"
#include <cmath>

namespace slonana {
namespace svm {

using namespace slonana::common;

/**
 * Rent calculation and validation
 * Equivalent to Agave's rent_calculator.rs
 */
class RentCalculator {
public:
    // Default rent configuration based on Solana mainnet
    static constexpr Lamports DEFAULT_LAMPORTS_PER_BYTE_YEAR = 3480;
    static constexpr double DEFAULT_EXEMPTION_THRESHOLD = 2.0;
    static constexpr Slot DEFAULT_SLOTS_PER_EPOCH = 432000;
    
    struct RentConfig {
        Lamports lamports_per_byte_year = DEFAULT_LAMPORTS_PER_BYTE_YEAR;
        double exemption_threshold = DEFAULT_EXEMPTION_THRESHOLD;
        Slot slots_per_epoch = DEFAULT_SLOTS_PER_EPOCH;
        
        RentConfig() = default;
        RentConfig(Lamports per_byte, double threshold, Slot slots)
            : lamports_per_byte_year(per_byte), exemption_threshold(threshold), slots_per_epoch(slots) {}
    };
    
    explicit RentCalculator(const RentConfig& config);
    RentCalculator(); // Default constructor
    ~RentCalculator() = default;
    
    /**
     * Calculate rent for account data size
     */
    Lamports calculate_rent(size_t data_size) const;
    
    /**
     * Calculate minimum balance for rent exemption
     */
    Lamports minimum_balance(size_t data_size) const;
    
    /**
     * Check if account is rent exempt
     */
    bool is_rent_exempt(Lamports balance, size_t data_size) const;
    
    /**
     * Calculate rent due for an account
     */
    Lamports calculate_rent_due(
        Lamports current_balance,
        size_t data_size,
        Slot current_slot,
        Slot rent_epoch_start_slot
    ) const;
    
    /**
     * Check if account needs rent collection
     */
    bool needs_rent_collection(
        Lamports balance,
        size_t data_size,
        Slot current_slot,
        Slot last_rent_epoch
    ) const;
    
    /**
     * Collect rent from account
     */
    struct RentCollection {
        Lamports collected_rent = 0;
        Lamports new_balance = 0;
        bool account_destroyed = false;
    };
    
    RentCollection collect_rent(
        Lamports current_balance,
        size_t data_size,
        Slot current_slot,
        Slot rent_epoch_start_slot
    ) const;
    
    /**
     * Get current rent configuration
     */
    const RentConfig& get_config() const { return config_; }
    
    /**
     * Update rent configuration
     */
    void update_config(const RentConfig& config) { config_ = config; }
    
    /**
     * Calculate slots per year (for rent calculations)
     */
    static constexpr double SLOTS_PER_YEAR = 365.25 * 24.0 * 60.0 * 60.0 * 2.0; // ~2 slots per second
    
private:
    RentConfig config_;
    
    double calculate_rent_multiplier(Slot slots_elapsed) const;
};

/**
 * Rent exempt status for accounts
 */
enum class RentExemptStatus {
    EXEMPT,
    NOT_EXEMPT,
    INSUFFICIENT_DATA_FOR_CALCULATION
};

/**
 * Helper functions for rent operations
 */
namespace rent_utils {
    /**
     * Check account rent status
     */
    RentExemptStatus get_rent_status(
        const RentCalculator& calculator,
        Lamports balance,
        size_t data_size
    );
    
    /**
     * Format rent collection result for logging
     */
    std::string format_rent_collection(const RentCalculator::RentCollection& collection);
    
    /**
     * Calculate rent epoch from slot
     */
    Slot calculate_rent_epoch(Slot slot, Slot slots_per_epoch);
}

} // namespace svm
} // namespace slonana