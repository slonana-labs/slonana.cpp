#pragma once

#include "common/types.h"
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace slonana {
namespace network {

using namespace slonana::common;

/**
 * Turbine node representing a validator in the turbine tree
 */
struct TurbineNode {
  std::vector<uint8_t> pubkey; // 32-byte public key
  std::string address;         // IP address
  uint16_t port;               // Port number
  uint32_t stake_weight;       // Stake weight for tree construction

  TurbineNode() = default;
  TurbineNode(const std::vector<uint8_t> &pk, const std::string &addr,
              uint16_t p, uint32_t stake = 1)
      : pubkey(pk), address(addr), port(p), stake_weight(stake) {
    if (pubkey.size() != 32) {
      pubkey.resize(32, 0);
    }
  }

  std::string to_string() const { return address + ":" + std::to_string(port); }

  bool operator==(const TurbineNode &other) const {
    return pubkey == other.pubkey && address == other.address &&
           port == other.port;
  }

  bool operator<(const TurbineNode &other) const {
    if (pubkey != other.pubkey)
      return pubkey < other.pubkey;
    if (address != other.address)
      return address < other.address;
    return port < other.port;
  }
};

/**
 * Turbine tree for efficient shred distribution
 * Compatible with Agave's turbine implementation
 */
class TurbineTree {
private:
  std::vector<TurbineNode> nodes_;
  std::unordered_map<std::string, size_t> node_index_map_;
  uint32_t fanout_;
  uint32_t max_retransmits_;
  TurbineNode self_node_;
  mutable std::mutex tree_mutex_;

  // Agave-compatible tree parameters
  static constexpr uint32_t DATA_PLANE_FANOUT = 8;
  static constexpr uint32_t FORWARD_PLANE_FANOUT = 16;
  static constexpr uint32_t MAX_RETRANSMIT_PEERS = 4;

  // Helper methods
  std::string node_key(const TurbineNode &node) const;
  size_t find_node_index(const TurbineNode &node) const;
  void build_index_map();
  std::vector<size_t> calculate_children_indices(size_t node_index,
                                                 uint32_t fanout) const;
  std::vector<size_t> calculate_retransmit_indices(size_t node_index) const;

public:
  explicit TurbineTree(const TurbineNode &self_node,
                       uint32_t fanout = DATA_PLANE_FANOUT);
  ~TurbineTree() = default;

  /**
   * Construct turbine tree from a list of validators
   * @param validators List of validator nodes
   */
  void construct_tree(const std::vector<TurbineNode> &validators);

  /**
   * Get children nodes for a given node in the tree
   * @param node Node to get children for
   * @return vector of child nodes
   */
  std::vector<TurbineNode> get_children(const TurbineNode &node) const;

  /**
   * Get retransmit peers for a node
   * @param node Node to get retransmit peers for
   * @return vector of retransmit peer nodes
   */
  std::vector<TurbineNode> get_retransmit_peers(const TurbineNode &node) const;

  /**
   * Get parent node for a given node
   * @param node Node to get parent for
   * @return parent node, nullopt if no parent (root)
   */
  std::optional<TurbineNode> get_parent(const TurbineNode &node) const;

  /**
   * Check if this node is the root of the tree
   * @param node Node to check
   * @return true if node is root
   */
  bool is_root(const TurbineNode &node) const;

  /**
   * Get the root node of the tree
   * @return root node
   */
  TurbineNode get_root() const;

  /**
   * Get all nodes in the tree
   * @return vector of all nodes
   */
  std::vector<TurbineNode> get_all_nodes() const;

  /**
   * Get tree statistics
   */
  struct TreeStats {
    size_t total_nodes;
    size_t tree_height;
    uint32_t fanout;
    size_t max_children_per_node;
    double average_children_per_node;
  };

  TreeStats get_stats() const;

  /**
   * Update node stake weight and rebuild tree if needed
   * @param node Node to update
   * @param new_stake New stake weight
   * @return true if tree was rebuilt
   */
  bool update_node_stake(const TurbineNode &node, uint32_t new_stake);

  /**
   * Remove a node from the tree
   * @param node Node to remove
   * @return true if node was removed and tree rebuilt
   */
  bool remove_node(const TurbineNode &node);

  /**
   * Add a new node to the tree
   * @param node Node to add
   * @return true if node was added and tree rebuilt
   */
  bool add_node(const TurbineNode &node);

  /**
   * Validate tree structure
   * @return true if tree is valid
   */
  bool validate_tree() const;

  /**
   * Get the current fanout setting
   * @return fanout value
   */
  uint32_t get_fanout() const;

  /**
   * Set fanout and rebuild tree
   * @param new_fanout New fanout value
   */
  void set_fanout(uint32_t new_fanout);

  /**
   * Serialize tree structure
   * @return serialized tree data
   */
  std::vector<uint8_t> serialize() const;

  /**
   * Deserialize tree structure
   * @param data Serialized tree data
   * @return true if deserialization successful
   */
  bool deserialize(const std::vector<uint8_t> &data);
};

/**
 * Utility functions for turbine tree operations
 */
namespace turbine_utils {
/**
 * Calculate optimal fanout based on network size
 * @param network_size Number of nodes in network
 * @return optimal fanout value
 */
uint32_t calculate_optimal_fanout(size_t network_size);

/**
 * Estimate tree height for given parameters
 * @param network_size Number of nodes
 * @param fanout Fanout value
 * @return estimated tree height
 */
size_t estimate_tree_height(size_t network_size, uint32_t fanout);

/**
 * Sort nodes by stake weight (descending)
 * @param nodes Nodes to sort
 * @return sorted nodes
 */
std::vector<TurbineNode> sort_by_stake(const std::vector<TurbineNode> &nodes);

/**
 * Create a balanced distribution of nodes
 * @param nodes Input nodes
 * @param fanout Fanout for distribution
 * @return distributed nodes
 */
std::vector<std::vector<TurbineNode>>
distribute_nodes(const std::vector<TurbineNode> &nodes, uint32_t fanout);

/**
 * Generate a hash for consistent node ordering
 * @param node Node to hash
 * @param seed Hash seed
 * @return hash value
 */
uint64_t hash_node(const TurbineNode &node, uint64_t seed = 0);

/**
 * Validate node structure
 * @param node Node to validate
 * @return true if node is valid
 */
bool validate_node(const TurbineNode &node);
} // namespace turbine_utils

} // namespace network
} // namespace slonana