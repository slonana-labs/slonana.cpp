#include "network/turbine.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <random>
#include <functional>
#include <openssl/sha.h>

namespace slonana {
namespace network {

// TurbineTree implementation
TurbineTree::TurbineTree(const TurbineNode& self_node, uint32_t fanout)
    : self_node_(self_node), fanout_(fanout), max_retransmits_(MAX_RETRANSMIT_PEERS) {
}

std::string TurbineTree::node_key(const TurbineNode& node) const {
    // Create unique key from pubkey and address:port
    std::string key;
    for (auto byte : node.pubkey) {
        key += std::to_string(byte);
    }
    key += "_" + node.address + "_" + std::to_string(node.port);
    return key;
}

size_t TurbineTree::find_node_index(const TurbineNode& node) const {
    auto key = node_key(node);
    auto it = node_index_map_.find(key);
    return (it != node_index_map_.end()) ? it->second : SIZE_MAX;
}

void TurbineTree::build_index_map() {
    node_index_map_.clear();
    for (size_t i = 0; i < nodes_.size(); ++i) {
        node_index_map_[node_key(nodes_[i])] = i;
    }
}

std::vector<size_t> TurbineTree::calculate_children_indices(size_t node_index, uint32_t fanout) const {
    std::vector<size_t> children;
    
    if (node_index >= nodes_.size()) {
        return children;
    }
    
    // Calculate children using a tree structure
    size_t first_child = node_index * fanout + 1;
    
    for (uint32_t i = 0; i < fanout; ++i) {
        size_t child_index = first_child + i;
        if (child_index < nodes_.size()) {
            children.push_back(child_index);
        }
    }
    
    return children;
}

std::vector<size_t> TurbineTree::calculate_retransmit_indices(size_t node_index) const {
    std::vector<size_t> retransmit_peers;
    
    if (node_index >= nodes_.size()) {
        return retransmit_peers;
    }
    
    // Select retransmit peers using a different algorithm to avoid conflicts
    // Use a hash-based selection for consistency
    uint64_t node_hash = turbine_utils::hash_node(nodes_[node_index]);
    
    for (size_t i = 0; i < nodes_.size() && retransmit_peers.size() < MAX_RETRANSMIT_PEERS; ++i) {
        if (i == node_index) continue; // Skip self
        
        // Use hash to determine if this node should be a retransmit peer
        uint64_t peer_hash = turbine_utils::hash_node(nodes_[i]);
        uint64_t combined_hash = node_hash ^ peer_hash;
        
        if (combined_hash % nodes_.size() < MAX_RETRANSMIT_PEERS) {
            retransmit_peers.push_back(i);
        }
    }
    
    return retransmit_peers;
}

void TurbineTree::construct_tree(const std::vector<TurbineNode>& validators) {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    // Sort validators by stake weight (highest first) for optimal tree structure
    auto sorted_validators = turbine_utils::sort_by_stake(validators);
    
    // Ensure self node is included
    nodes_.clear();
    bool self_included = false;
    
    for (const auto& validator : sorted_validators) {
        nodes_.push_back(validator);
        if (validator == self_node_) {
            self_included = true;
        }
    }
    
    if (!self_included) {
        nodes_.insert(nodes_.begin(), self_node_);
    }
    
    // Build index map for fast lookups
    build_index_map();
    
    std::cout << "🌳 Turbine: Constructed tree with " << nodes_.size() 
              << " nodes, fanout=" << fanout_ << std::endl;
}

std::vector<TurbineNode> TurbineTree::get_children(const TurbineNode& node) const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    size_t node_index = find_node_index(node);
    if (node_index == SIZE_MAX) {
        return {};
    }
    
    auto child_indices = calculate_children_indices(node_index, fanout_);
    
    std::vector<TurbineNode> children;
    children.reserve(child_indices.size());
    
    for (size_t index : child_indices) {
        children.push_back(nodes_[index]);
    }
    
    return children;
}

std::vector<TurbineNode> TurbineTree::get_retransmit_peers(const TurbineNode& node) const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    size_t node_index = find_node_index(node);
    if (node_index == SIZE_MAX) {
        return {};
    }
    
    auto peer_indices = calculate_retransmit_indices(node_index);
    
    std::vector<TurbineNode> peers;
    peers.reserve(peer_indices.size());
    
    for (size_t index : peer_indices) {
        peers.push_back(nodes_[index]);
    }
    
    return peers;
}

std::optional<TurbineNode> TurbineTree::get_parent(const TurbineNode& node) const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    size_t node_index = find_node_index(node);
    if (node_index == SIZE_MAX || node_index == 0) {
        return std::nullopt; // Root has no parent
    }
    
    // Calculate parent index
    size_t parent_index = (node_index - 1) / fanout_;
    
    if (parent_index < nodes_.size()) {
        return nodes_[parent_index];
    }
    
    return std::nullopt;
}

bool TurbineTree::is_root(const TurbineNode& node) const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    if (nodes_.empty()) {
        return false;
    }
    
    return nodes_[0] == node;
}

TurbineNode TurbineTree::get_root() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    if (nodes_.empty()) {
        return TurbineNode();
    }
    
    return nodes_[0];
}

std::vector<TurbineNode> TurbineTree::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    return nodes_;
}

TurbineTree::TreeStats TurbineTree::get_stats() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    TreeStats stats;
    stats.total_nodes = nodes_.size();
    stats.fanout = fanout_;
    
    if (nodes_.empty()) {
        return stats;
    }
    
    // Calculate tree height
    stats.tree_height = static_cast<size_t>(std::ceil(std::log(nodes_.size()) / std::log(fanout_)));
    
    // Calculate children statistics
    size_t total_children = 0;
    size_t max_children = 0;
    
    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto children = calculate_children_indices(i, fanout_);
        total_children += children.size();
        max_children = std::max(max_children, children.size());
    }
    
    stats.max_children_per_node = max_children;
    stats.average_children_per_node = nodes_.size() > 0 ? 
        static_cast<double>(total_children) / nodes_.size() : 0.0;
    
    return stats;
}

bool TurbineTree::update_node_stake(const TurbineNode& node, uint32_t new_stake) {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    // Find and update the node
    for (auto& n : nodes_) {
        if (n == node) {
            n.stake_weight = new_stake;
            
            // Rebuild tree with new stakes
            auto current_nodes = nodes_;
            lock.~lock_guard(); // Release lock before recursive call
            construct_tree(current_nodes);
            return true;
        }
    }
    
    return false;
}

bool TurbineTree::remove_node(const TurbineNode& node) {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    auto it = std::find(nodes_.begin(), nodes_.end(), node);
    if (it != nodes_.end()) {
        nodes_.erase(it);
        build_index_map();
        std::cout << "🌳 Turbine: Removed node " << node.to_string() 
                  << ", tree now has " << nodes_.size() << " nodes" << std::endl;
        return true;
    }
    
    return false;
}

bool TurbineTree::add_node(const TurbineNode& node) {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    // Check if node already exists
    if (find_node_index(node) != SIZE_MAX) {
        return false;
    }
    
    nodes_.push_back(node);
    
    // Re-sort by stake weight
    std::sort(nodes_.begin(), nodes_.end(), 
              [](const TurbineNode& a, const TurbineNode& b) {
                  return a.stake_weight > b.stake_weight;
              });
    
    build_index_map();
    std::cout << "🌳 Turbine: Added node " << node.to_string() 
              << ", tree now has " << nodes_.size() << " nodes" << std::endl;
    return true;
}

bool TurbineTree::validate_tree() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    if (nodes_.empty()) {
        return true;
    }
    
    // Check that all nodes are unique
    for (size_t i = 0; i < nodes_.size(); ++i) {
        for (size_t j = i + 1; j < nodes_.size(); ++j) {
            if (nodes_[i] == nodes_[j]) {
                return false;
            }
        }
    }
    
    // Check that tree structure is valid
    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto children = calculate_children_indices(i, fanout_);
        
        // Check that all children indices are valid
        for (size_t child_index : children) {
            if (child_index >= nodes_.size()) {
                return false;
            }
        }
    }
    
    return true;
}

uint32_t TurbineTree::get_fanout() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    return fanout_;
}

void TurbineTree::set_fanout(uint32_t new_fanout) {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    if (new_fanout != fanout_) {
        fanout_ = new_fanout;
        if (!nodes_.empty()) {
            build_index_map();
            std::cout << "🌳 Turbine: Updated fanout to " << fanout_ << std::endl;
        }
    }
}

std::vector<uint8_t> TurbineTree::serialize() const {
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    std::vector<uint8_t> result;
    
    // Serialize fanout (4 bytes)
    for (int i = 0; i < 4; ++i) {
        result.push_back(static_cast<uint8_t>((fanout_ >> (i * 8)) & 0xFF));
    }
    
    // Serialize node count (4 bytes)
    uint32_t node_count = static_cast<uint32_t>(nodes_.size());
    for (int i = 0; i < 4; ++i) {
        result.push_back(static_cast<uint8_t>((node_count >> (i * 8)) & 0xFF));
    }
    
    // Serialize each node
    for (const auto& node : nodes_) {
        // Pubkey (32 bytes)
        result.insert(result.end(), node.pubkey.begin(), node.pubkey.end());
        
        // Address length + address
        uint16_t addr_len = static_cast<uint16_t>(node.address.length());
        for (int i = 0; i < 2; ++i) {
            result.push_back(static_cast<uint8_t>((addr_len >> (i * 8)) & 0xFF));
        }
        result.insert(result.end(), node.address.begin(), node.address.end());
        
        // Port (2 bytes)
        for (int i = 0; i < 2; ++i) {
            result.push_back(static_cast<uint8_t>((node.port >> (i * 8)) & 0xFF));
        }
        
        // Stake weight (4 bytes)
        for (int i = 0; i < 4; ++i) {
            result.push_back(static_cast<uint8_t>((node.stake_weight >> (i * 8)) & 0xFF));
        }
    }
    
    return result;
}

bool TurbineTree::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 8) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(tree_mutex_);
    
    size_t offset = 0;
    
    // Deserialize fanout
    fanout_ = 0;
    for (int i = 0; i < 4; ++i) {
        fanout_ |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    offset += 4;
    
    // Deserialize node count
    uint32_t node_count = 0;
    for (int i = 0; i < 4; ++i) {
        node_count |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
    }
    offset += 4;
    
    // Deserialize nodes
    nodes_.clear();
    nodes_.reserve(node_count);
    
    for (uint32_t i = 0; i < node_count; ++i) {
        if (offset + 38 > data.size()) { // Minimum node size
            return false;
        }
        
        TurbineNode node;
        
        // Pubkey
        node.pubkey.assign(data.begin() + offset, data.begin() + offset + 32);
        offset += 32;
        
        // Address length
        uint16_t addr_len = 0;
        for (int j = 0; j < 2; ++j) {
            addr_len |= static_cast<uint16_t>(data[offset + j]) << (j * 8);
        }
        offset += 2;
        
        if (offset + addr_len + 6 > data.size()) {
            return false;
        }
        
        // Address
        node.address.assign(data.begin() + offset, data.begin() + offset + addr_len);
        offset += addr_len;
        
        // Port
        node.port = 0;
        for (int j = 0; j < 2; ++j) {
            node.port |= static_cast<uint16_t>(data[offset + j]) << (j * 8);
        }
        offset += 2;
        
        // Stake weight
        node.stake_weight = 0;
        for (int j = 0; j < 4; ++j) {
            node.stake_weight |= static_cast<uint32_t>(data[offset + j]) << (j * 8);
        }
        offset += 4;
        
        nodes_.push_back(node);
    }
    
    build_index_map();
    return true;
}

// Utility functions implementation
namespace turbine_utils {

uint32_t calculate_optimal_fanout(size_t network_size) {
    if (network_size <= 8) return 2;
    if (network_size <= 64) return 4;
    if (network_size <= 512) return 8;
    return 16;
}

size_t estimate_tree_height(size_t network_size, uint32_t fanout) {
    if (network_size <= 1 || fanout <= 1) {
        return 1;
    }
    return static_cast<size_t>(std::ceil(std::log(network_size) / std::log(fanout)));
}

std::vector<TurbineNode> sort_by_stake(const std::vector<TurbineNode>& nodes) {
    auto sorted = nodes;
    std::sort(sorted.begin(), sorted.end(),
              [](const TurbineNode& a, const TurbineNode& b) {
                  return a.stake_weight > b.stake_weight;
              });
    return sorted;
}

std::vector<std::vector<TurbineNode>> distribute_nodes(
    const std::vector<TurbineNode>& nodes, uint32_t fanout) {
    
    std::vector<std::vector<TurbineNode>> distribution;
    
    if (nodes.empty() || fanout == 0) {
        return distribution;
    }
    
    size_t nodes_per_group = std::max(size_t(1), nodes.size() / fanout);
    
    for (size_t i = 0; i < nodes.size(); i += nodes_per_group) {
        std::vector<TurbineNode> group;
        for (size_t j = i; j < std::min(i + nodes_per_group, nodes.size()); ++j) {
            group.push_back(nodes[j]);
        }
        distribution.push_back(group);
    }
    
    return distribution;
}

uint64_t hash_node(const TurbineNode& node, uint64_t seed) {
    // Simple hash function for node consistency
    std::hash<std::string> hasher;
    
    std::string node_str = node.address + ":" + std::to_string(node.port) + "_" + std::to_string(seed);
    for (auto byte : node.pubkey) {
        node_str += std::to_string(byte);
    }
    
    return hasher(node_str);
}

bool validate_node(const TurbineNode& node) {
    // Check pubkey size
    if (node.pubkey.size() != 32) {
        return false;
    }
    
    // Check address is not empty
    if (node.address.empty()) {
        return false;
    }
    
    // Check port is reasonable
    if (node.port == 0) {
        return false;
    }
    
    return true;
}

} // namespace turbine_utils

} // namespace network
} // namespace slonana