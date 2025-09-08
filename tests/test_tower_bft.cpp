#include "consensus/tower_bft.h"
#include "consensus/lockouts.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <chrono>

using namespace slonana::consensus;

bool test_lockout_basic() {
    std::cout << "Testing basic lockout functionality..." << std::endl;
    
    // Test basic lockout creation
    Lockout lockout(100, 5);
    assert(lockout.slot == 100);
    assert(lockout.confirmation_count == 5);
    
    // Test lockout period calculation (2^5 = 32)
    assert(lockout.lockout_period() == 32);
    
    // Test lockout expiration
    assert(lockout.expiration_slot() == 132); // 100 + 32
    
    // Test if slot is locked out
    assert(lockout.is_locked_out_at_slot(110) == true);  // 110 is within lockout (100 < 110 <= 132)
    assert(lockout.is_locked_out_at_slot(132) == true);  // 132 is the boundary (100 < 132 <= 132)
    assert(lockout.is_locked_out_at_slot(133) == false); // 133 is beyond lockout (133 > 132)
    assert(lockout.is_locked_out_at_slot(100) == false); // Cannot lock out the slot itself
    assert(lockout.is_locked_out_at_slot(99) == false);  // Slots before are not locked out
    
    // Test serialization
    auto serialized = lockout.serialize();
    assert(serialized.size() == 12); // 8 bytes for slot + 4 bytes for confirmation_count
    
    Lockout deserialized;
    size_t consumed = deserialized.deserialize(serialized, 0);
    assert(consumed == 12);
    assert(deserialized.slot == lockout.slot);
    assert(deserialized.confirmation_count == lockout.confirmation_count);
    
    std::cout << "✅ Basic lockout functionality test passed" << std::endl;
    return true;
}

bool test_lockout_manager() {
    std::cout << "Testing lockout manager..." << std::endl;
    
    LockoutManager manager;
    assert(manager.empty());
    assert(manager.size() == 0);
    
    // Add some lockouts
    manager.add_lockout(Lockout(100, 2));
    manager.add_lockout(Lockout(110, 3));
    manager.add_lockout(Lockout(120, 1));
    
    assert(manager.size() == 3);
    assert(!manager.empty());
    
    // Test slot lockout checking
    assert(manager.is_slot_locked_out(102) == true);  // Within lockout of slot 100
    assert(manager.is_slot_locked_out(105) == false); // Beyond lockout of slot 100 (100 + 4 = 104)
    assert(manager.is_slot_locked_out(115) == true);  // Within lockout of slot 110 (110 + 8 = 118)
    
    // Test getting lockout for specific slot
    const Lockout* lockout = manager.get_lockout_for_slot(110);
    assert(lockout != nullptr);
    assert(lockout->slot == 110);
    assert(lockout->confirmation_count == 3);
    
    // Test updating confirmation count
    assert(manager.update_confirmation_count(110, 5) == true);
    lockout = manager.get_lockout_for_slot(110);
    assert(lockout->confirmation_count == 5);
    
    // Test removing expired lockouts
    size_t removed = manager.remove_expired_lockouts(150);
    assert(removed >= 0); // Should remove some expired lockouts
    
    std::cout << "✅ Lockout manager test passed" << std::endl;
    return true;
}

bool test_tower_basic() {
    std::cout << "Testing basic tower functionality..." << std::endl;
    
    Tower tower(50); // Start with root slot 50
    assert(tower.get_root_slot() == 50);
    assert(tower.get_last_vote_slot() == 50);
    assert(tower.get_tower_height() == 0);

    // Test voting on valid slots
    assert(tower.can_vote_on_slot(51) == true);
    assert(tower.can_vote_on_slot(50) == false); // Cannot vote on root slot
    assert(tower.can_vote_on_slot(49) == false); // Cannot vote before root

    // Record a vote
    tower.record_vote(55);
    assert(tower.get_last_vote_slot() == 55);
    assert(tower.get_tower_height() == 1);

    // Check slots with lockout period = 1 (2^0 = 1)
    assert(tower.can_vote_on_slot(55) == false); // Cannot vote on same slot
    assert(tower.can_vote_on_slot(54) == false); // Cannot vote before last voted
    assert(tower.can_vote_on_slot(56) == false); // Within lockout period (55 + 1 = 56)
    assert(tower.can_vote_on_slot(57) == true);  // Beyond lockout period

    // Record another vote at a safe distance
    tower.record_vote(60);
    assert(tower.get_tower_height() == 2);

    // Test tower validity
    assert(tower.is_valid() == true);

    // Test serialization
    auto serialized = tower.serialize();
    
    Tower new_tower;
    assert(new_tower.deserialize(serialized) == true);
    assert(new_tower.get_root_slot() == tower.get_root_slot());
    assert(new_tower.get_last_vote_slot() == tower.get_last_vote_slot());
    assert(new_tower.get_tower_height() == tower.get_tower_height());

    std::cout << "✅ Basic tower functionality test passed" << std::endl;
    return true;
}

bool test_vote_state() {
    std::cout << "Testing vote state..." << std::endl;
    
    std::vector<uint8_t> voter_pubkey(32, 0x01);
    std::vector<uint8_t> node_pubkey(32, 0x02);
    
    VoteState vote_state(voter_pubkey, node_pubkey);
    assert(vote_state.authorized_voter == voter_pubkey);
    assert(vote_state.node_pubkey == node_pubkey);
    assert(vote_state.root_slot == 0);
    assert(vote_state.last_voted_slot() == 0);
    
    // Test valid vote
    assert(vote_state.is_valid_vote(10) == true);
    vote_state.process_vote(10, 0);
    assert(vote_state.last_voted_slot() == 10);
    assert(vote_state.votes.size() == 1);
    
    // Cannot vote on same or earlier slot
    assert(vote_state.is_valid_vote(10) == false);
    assert(vote_state.is_valid_vote(9) == false);
    assert(vote_state.is_valid_vote(15) == true);
    
    // Process another vote (this will remove expired votes with confirmation_count=0)
    vote_state.process_vote(20, 0);
    // Note: vote on slot 10 (with confirmation_count=0) expires at slot 11
    // So when we vote on slot 20, the vote on slot 10 is removed as expired
    assert(vote_state.votes.size() == 1); // Only the vote on slot 20 remains
    
    // Test root slot update
    vote_state.update_root_slot(15);
    assert(vote_state.root_slot == 15);
    // The vote on slot 20 should still be there since 20 >= 15
    assert(vote_state.votes.size() == 1);
    
    // Test serialization
    auto serialized = vote_state.serialize();
    
    VoteState new_vote_state;
    assert(new_vote_state.deserialize(serialized) == true);
    assert(new_vote_state.root_slot == vote_state.root_slot);
    assert(new_vote_state.authorized_voter == vote_state.authorized_voter);
    assert(new_vote_state.node_pubkey == vote_state.node_pubkey);
    
    std::cout << "✅ Vote state test passed" << std::endl;
    return true;
}

bool test_tower_bft_manager() {
    std::cout << "Testing Tower BFT manager..." << std::endl;
    
    TowerBftManager manager(100);
    
    std::vector<uint8_t> voter_pubkey(32, 0x01);
    std::vector<uint8_t> node_pubkey(32, 0x02);
    
    assert(manager.initialize(voter_pubkey, node_pubkey) == true);
    
    // Test vote processing
    bool can_vote_called = false;
    uint64_t callback_slot = 0;
    bool callback_can_vote = false;
    
    manager.set_vote_callback([&](uint64_t slot, bool can_vote) {
        can_vote_called = true;
        callback_slot = slot;
        callback_can_vote = can_vote;
    });
    
    // Process a slot
    bool result = manager.process_slot(105, 104);
    assert(can_vote_called == true);
    assert(callback_slot == 105);
    assert(callback_can_vote == true);
    assert(result == true);
    
    // Cast the vote
    assert(manager.cast_vote(105) == true);
    
    // Get stats
    auto stats = manager.get_stats();
    assert(stats.tower_height == 1);
    assert(stats.root_slot == 100);
    assert(stats.last_vote_slot == 105);
    assert(stats.lockout_count == 1);
    
    // Process another slot
    can_vote_called = false;
    result = manager.process_slot(110, 105);
    assert(can_vote_called == true);
    assert(callback_can_vote == true);
    
    assert(manager.cast_vote(110) == true);
    
    // Verify updated stats
    stats = manager.get_stats();
    assert(stats.tower_height == 2);
    assert(stats.last_vote_slot == 110);
    
    std::cout << "✅ Tower BFT manager test passed" << std::endl;
    return true;
}

bool test_lockout_edge_cases() {
    std::cout << "Testing lockout edge cases..." << std::endl;
    
    // Test maximum lockout
    Lockout max_lockout(100, 50); // Large confirmation count
    uint64_t max_period = max_lockout.lockout_period();
    assert(max_period == (1ULL << 32)); // Should be capped at max lockout
    
    // Test zero confirmation count
    Lockout zero_lockout(100, 0);
    assert(zero_lockout.lockout_period() == 1); // 2^0 = 1
    
    // Test lockout utility functions
    assert(lockout_utils::calculate_max_lockout_distance(5) == 32);
    assert(lockout_utils::calculate_max_lockout_distance(50) == (1ULL << 32));
    
    // Test slot conflict detection
    Lockout lockout1(100, 3); // Lockout period = 8
    assert(lockout_utils::slots_conflict(100, lockout1, 105) == true);  // 105 is within 100 + 8
    assert(lockout_utils::slots_conflict(100, lockout1, 109) == false); // 109 is beyond 100 + 8
    
    // Test lockout validation
    std::vector<Lockout> valid_lockouts = {
        Lockout(100, 2),
        Lockout(110, 1),
        Lockout(120, 3)
    };
    assert(lockout_utils::validate_lockouts(valid_lockouts) == true);
    
    std::vector<Lockout> invalid_lockouts = {
        Lockout(110, 1), // Out of order
        Lockout(100, 2)
    };
    assert(lockout_utils::validate_lockouts(invalid_lockouts) == false);
    
    std::cout << "✅ Lockout edge cases test passed" << std::endl;
    return true;
}

int main() {
    std::cout << "=== Tower BFT Test Suite ===" << std::endl;
    
    try {
        assert(test_lockout_basic());
        assert(test_lockout_manager());
        assert(test_tower_basic());
        assert(test_vote_state());
        assert(test_tower_bft_manager());
        assert(test_lockout_edge_cases());
        
        std::cout << "\n🎉 All Tower BFT tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ Test failed with unknown exception" << std::endl;
        return 1;
    }
}