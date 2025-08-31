#include "svm/rent_calculator.h"
#include <algorithm>
#include <sstream>

namespace slonana {
namespace svm {

RentCalculator::RentCalculator(const RentConfig& config) : config_(config) {
}

RentCalculator::RentCalculator() : config_(RentConfig()) {
}

Lamports RentCalculator::calculate_rent(size_t data_size) const {
    if (data_size == 0) {
        return 0;
    }
    
    // Calculate yearly rent based on data size
    Lamports yearly_rent = static_cast<Lamports>(data_size) * config_.lamports_per_byte_year;
    
    // Convert to per-slot rent (simplified - using epochs)
    double slots_per_year = SLOTS_PER_YEAR;
    double rent_per_slot = static_cast<double>(yearly_rent) / slots_per_year;
    
    // Return rent for one epoch
    return static_cast<Lamports>(rent_per_slot * config_.slots_per_epoch);
}

Lamports RentCalculator::minimum_balance(size_t data_size) const {
    if (data_size == 0) {
        return 0;
    }
    
    // Calculate yearly rent
    Lamports yearly_rent = static_cast<Lamports>(data_size) * config_.lamports_per_byte_year;
    
    // Apply exemption threshold (typically 2 years worth of rent)
    return static_cast<Lamports>(static_cast<double>(yearly_rent) * config_.exemption_threshold);
}

bool RentCalculator::is_rent_exempt(Lamports balance, size_t data_size) const {
    if (data_size == 0) {
        return true; // Zero-sized accounts are always rent exempt
    }
    
    Lamports min_balance = minimum_balance(data_size);
    return balance >= min_balance;
}

Lamports RentCalculator::calculate_rent_due(
    Lamports current_balance,
    size_t data_size,
    Slot current_slot,
    Slot rent_epoch_start_slot
) const {
    if (is_rent_exempt(current_balance, data_size)) {
        return 0; // Rent exempt accounts don't owe rent
    }
    
    if (current_slot <= rent_epoch_start_slot) {
        return 0; // No time has passed
    }
    
    Slot slots_elapsed = current_slot - rent_epoch_start_slot;
    double rent_multiplier = calculate_rent_multiplier(slots_elapsed);
    
    Lamports yearly_rent = static_cast<Lamports>(data_size) * config_.lamports_per_byte_year;
    return static_cast<Lamports>(static_cast<double>(yearly_rent) * rent_multiplier);
}

bool RentCalculator::needs_rent_collection(
    Lamports balance,
    size_t data_size,
    Slot current_slot,
    Slot last_rent_epoch
) const {
    if (is_rent_exempt(balance, data_size)) {
        return false;
    }
    
    // Check if enough time has passed to collect rent
    Slot rent_epoch = rent_utils::calculate_rent_epoch(current_slot, config_.slots_per_epoch);
    return rent_epoch > last_rent_epoch;
}

RentCalculator::RentCollection RentCalculator::collect_rent(
    Lamports current_balance,
    size_t data_size,
    Slot current_slot,
    Slot rent_epoch_start_slot
) const {
    RentCollection result;
    result.new_balance = current_balance;
    
    if (is_rent_exempt(current_balance, data_size)) {
        return result; // No rent to collect
    }
    
    Lamports rent_due = calculate_rent_due(current_balance, data_size, current_slot, rent_epoch_start_slot);
    
    if (rent_due == 0) {
        return result;
    }
    
    if (current_balance >= rent_due) {
        // Collect rent
        result.collected_rent = rent_due;
        result.new_balance = current_balance - rent_due;
    } else {
        // Insufficient balance - collect all remaining and mark for destruction
        result.collected_rent = current_balance;
        result.new_balance = 0;
        result.account_destroyed = true;
    }
    
    return result;
}

double RentCalculator::calculate_rent_multiplier(Slot slots_elapsed) const {
    // Calculate the fraction of a year that has elapsed
    double slots_per_year = SLOTS_PER_YEAR;
    return static_cast<double>(slots_elapsed) / slots_per_year;
}

// Utility functions
namespace rent_utils {

RentExemptStatus get_rent_status(
    const RentCalculator& calculator,
    Lamports balance,
    size_t data_size
) {
    if (data_size == 0) {
        return RentExemptStatus::EXEMPT;
    }
    
    return calculator.is_rent_exempt(balance, data_size) 
        ? RentExemptStatus::EXEMPT 
        : RentExemptStatus::NOT_EXEMPT;
}

std::string format_rent_collection(const RentCalculator::RentCollection& collection) {
    std::stringstream ss;
    ss << "RentCollection{";
    ss << "collected=" << collection.collected_rent;
    ss << ", new_balance=" << collection.new_balance;
    ss << ", destroyed=" << (collection.account_destroyed ? "true" : "false");
    ss << "}";
    return ss.str();
}

Slot calculate_rent_epoch(Slot slot, Slot slots_per_epoch) {
    if (slots_per_epoch == 0) {
        return 0;
    }
    return slot / slots_per_epoch;
}

} // namespace rent_utils

} // namespace svm
} // namespace slonana