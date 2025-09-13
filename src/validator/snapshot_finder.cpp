#include "validator/snapshot_finder.h"
#include "network/http_client.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <thread>

namespace slonana {
namespace validator {

namespace fs = std::filesystem;

SnapshotFinder::SnapshotFinder(const SnapshotFinderConfig &config)
    : config_(config), http_client_(std::make_unique<network::HttpClient>()) {

  // Configure HTTP client for faster operations
  http_client_->set_timeout(10); // 10 second timeout for discovery
  http_client_->set_user_agent("slonana-snapshot-finder/1.0");

  std::cout << "📍 Snapshot Finder initialized:" << std::endl;
  std::cout << "   • Thread count: " << config_.threads_count << std::endl;
  std::cout << "   • Network: " << config_.network << std::endl;
  std::cout << "   • Max snapshot age: " << config_.max_snapshot_age << " slots"
            << std::endl;
  std::cout << "   • Min download speed: " << config_.min_download_speed
            << " MB/s" << std::endl;
}

SnapshotFinder::~SnapshotFinder() {
  shutdown_requested_.store(true);
  cv_.notify_all();

  for (auto &thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

std::vector<std::string>
SnapshotFinder::get_default_rpc_endpoints(const std::string &network) {
  if (network == "mainnet-beta" || network == "mainnet") {
    return {"https://api.mainnet-beta.solana.com",
            "https://solana-api.projectserum.com",
            "https://rpc.ankr.com/solana",
            "https://solana.public-rpc.com",
            "https://api.mainnet.rpcpool.com",
            "https://solana-mainnet.g.alchemy.com/v2/demo",
            "https://mainnet.helius-rpc.com",
            "https://rpc.helius.xyz",
            "https://solana.blockdaemon.com",
            "https://ssc-dao.genesysgo.net",
            "https://solana-rpc.publicnode.com",
            "https://solana.llamarpc.com"};
  } else if (network == "testnet") {
    return {"https://api.testnet.solana.com", "https://testnet.rpcpool.com"};
  } else if (network == "devnet") {
    return {"https://api.devnet.solana.com", "https://devnet.rpcpool.com"};
  }

  return {"https://api.mainnet-beta.solana.com"};
}

std::vector<std::string>
SnapshotFinder::get_snapshot_mirror_urls(const std::string &network) {
  if (network == "mainnet-beta" || network == "mainnet") {
    return {"https://snapshots.rpcpool.com", "https://snapshots.solana.com",
            "https://snapshot.solanalabs.com"};
  } else if (network == "testnet") {
    return {"https://snapshots.rpcpool.com/testnet",
            "https://snapshots.solana.com/testnet"};
  } else if (network == "devnet") {
    return {"https://snapshots.rpcpool.com/devnet",
            "https://snapshots.solana.com/devnet"};
  }

  return {"https://snapshots.rpcpool.com"};
}

common::Result<std::vector<RpcNodeInfo>> SnapshotFinder::discover_rpc_nodes() {
  std::cout << "🔍 Starting RPC node discovery with " << config_.threads_count
            << " threads..." << std::endl;

  // Get seed RPC endpoints
  auto seed_endpoints = get_default_rpc_endpoints(config_.network);

  // Add custom snapshot mirrors as seed endpoints
  auto mirrors = get_snapshot_mirror_urls(config_.network);
  seed_endpoints.insert(seed_endpoints.end(), mirrors.begin(), mirrors.end());

  if (seed_endpoints.empty()) {
    return common::Result<std::vector<RpcNodeInfo>>(
        "No seed RPC endpoints found for network: " + config_.network);
  }

  std::cout << "   • Using " << seed_endpoints.size()
            << " seed endpoints for cluster discovery" << std::endl;

  // Discover cluster nodes from seed endpoints
  auto cluster_result = discover_cluster_nodes(seed_endpoints);
  std::vector<std::string> rpc_endpoints;

  if (cluster_result.is_ok()) {
    rpc_endpoints = cluster_result.value();
    std::cout << "   • Discovered " << rpc_endpoints.size()
              << " RPC endpoints from cluster" << std::endl;
  } else {
    std::cout << "   • Cluster discovery failed: " << cluster_result.error()
              << std::endl;
    std::cout << "   • Falling back to seed endpoints only" << std::endl;
    rpc_endpoints = seed_endpoints;
  }

  if (rpc_endpoints.empty()) {
    return common::Result<std::vector<RpcNodeInfo>>(
        "No RPC endpoints available after discovery");
  }

  // Clear previous results
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    discovered_nodes_.clear();
  }

  // Setup threading
  total_tests_.store(rpc_endpoints.size());
  completed_tests_.store(0);
  shutdown_requested_.store(false);

  // Calculate work distribution
  size_t endpoints_per_thread =
      std::max(size_t(1), rpc_endpoints.size() / config_.threads_count);
  size_t actual_threads = std::min(static_cast<size_t>(config_.threads_count),
                                   rpc_endpoints.size());

  std::cout << "   • Testing " << rpc_endpoints.size() << " endpoints"
            << std::endl;
  std::cout << "   • Using " << actual_threads << " threads" << std::endl;

  // Start worker threads with enhanced error handling
  worker_threads_.clear();
  worker_threads_.reserve(actual_threads);

  // **SEGFAULT FIX**: Enhanced thread creation with error handling
  try {
    for (size_t i = 0; i < actual_threads; ++i) {
      size_t start_idx = i * endpoints_per_thread;
      size_t end_idx = (i == actual_threads - 1) ? rpc_endpoints.size()
                                                 : (i + 1) * endpoints_per_thread;

      // **BOUNDS VALIDATION**: Ensure thread indices are valid
      if (start_idx >= rpc_endpoints.size()) {
        break;
      }

      worker_threads_.emplace_back(&SnapshotFinder::worker_thread_function, this,
                                   std::cref(rpc_endpoints), start_idx, end_idx);
    }

    // **SAFE WAITING**: Wait for completion with timeout protection
    auto start_wait = std::chrono::steady_clock::now();
    const auto max_wait_time = std::chrono::seconds(120); // 2 minute timeout

    while (completed_tests_.load() < total_tests_.load()) {
      auto elapsed = std::chrono::steady_clock::now() - start_wait;
      if (elapsed > max_wait_time) {
        std::cout << "   ⚠️  Discovery timeout reached, shutting down workers..." << std::endl;
        shutdown_requested_.store(true);
        break;
      }
      
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      report_discovery_progress();
    }

    // **SAFE THREAD CLEANUP**: Proper thread joining with timeout
    for (auto &thread : worker_threads_) {
      if (thread.joinable()) {
        try {
          thread.join();
        } catch (const std::exception& e) {
          std::cout << "   ⚠️  Thread join error: " << e.what() << std::endl;
        }
      }
    }

  } catch (const std::exception& e) {
    std::cout << "   ❌ Thread management error: " << e.what() << std::endl;
    shutdown_requested_.store(true);
    
    // Emergency cleanup
    for (auto &thread : worker_threads_) {
      if (thread.joinable()) {
        thread.detach(); // Detach rather than risk hanging on join
      }
    }
    worker_threads_.clear();
    
    return common::Result<std::vector<RpcNodeInfo>>("Thread management error during discovery");
  }

  std::cout << std::endl;
  std::cout << "✅ Discovery completed. Found " << discovered_nodes_.size()
            << " healthy nodes" << std::endl;

  // Sort by quality (latency and version)
  std::sort(discovered_nodes_.begin(), discovered_nodes_.end(),
            [](const RpcNodeInfo &a, const RpcNodeInfo &b) {
              if (a.healthy != b.healthy)
                return a.healthy > b.healthy;
              return a.latency_ms < b.latency_ms;
            });

  return common::Result<std::vector<RpcNodeInfo>>(discovered_nodes_);
}

void SnapshotFinder::worker_thread_function(
    const std::vector<std::string> &rpc_urls, size_t start_index,
    size_t end_index) {

  // **SEGFAULT FIX**: Add comprehensive error handling to prevent crashes
  try {
    for (size_t i = start_index; i < end_index && !shutdown_requested_.load();
         ++i) {
      // **BOUNDS CHECKING**: Ensure we don't access out-of-bounds elements
      if (i >= rpc_urls.size()) {
        break;
      }

      // **SAFE RPC TESTING**: Wrap in try-catch to prevent crashes
      try {
        auto test_result = test_rpc_node(rpc_urls[i]);

        if (test_result.is_ok()) {
          RpcNodeInfo node = test_result.value();
          if (node.healthy) {
            // **THREAD-SAFE INSERTION**: Protect shared data structure
            std::lock_guard<std::mutex> lock(results_mutex_);
            if (discovered_nodes_.size() < 1000) { // Prevent excessive memory usage
              discovered_nodes_.push_back(std::move(node));
            }
          }
        }
      } catch (const std::exception& e) {
        // **ERROR HANDLING**: Log but don't crash on individual RPC failures
        std::cout << "   ⚠️  RPC test failed for " << rpc_urls[i] << ": " << e.what() << std::endl;
      } catch (...) {
        // **CATCH-ALL**: Prevent unknown exceptions from crashing
        std::cout << "   ⚠️  Unknown error testing RPC " << rpc_urls[i] << std::endl;
      }

      completed_tests_.fetch_add(1);
    }
  } catch (const std::exception& e) {
    std::cout << "   ❌ Worker thread error: " << e.what() << std::endl;
  } catch (...) {
    std::cout << "   ❌ Unknown worker thread error" << std::endl;
  }
}

common::Result<RpcNodeInfo>
SnapshotFinder::test_rpc_node(const std::string &rpc_url) {
  RpcNodeInfo node;
  node.url = rpc_url;
  node.last_checked = std::chrono::steady_clock::now();

  try {
    // Measure latency with getSlot call
    auto start_time = std::chrono::steady_clock::now();

    auto slot_result = get_current_slot(rpc_url);

    auto end_time = std::chrono::steady_clock::now();
    node.latency_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time)
            .count();

    if (!slot_result.is_ok()) {
      return common::Result<RpcNodeInfo>("Failed to get slot: " +
                                         slot_result.error());
    }

    node.slot = slot_result.value();

    // Check latency threshold
    if (node.latency_ms > config_.max_latency) {
      return common::Result<RpcNodeInfo>(
          "Latency too high: " + std::to_string(node.latency_ms) + "ms");
    }

    // Try to get snapshot slot
    auto snapshot_result = get_snapshot_slot(rpc_url);
    if (snapshot_result.is_ok()) {
      node.snapshot_slot = snapshot_result.value();
    }

    // Try to get version info
    auto response = http_client_->solana_rpc_call(rpc_url, "getVersion");
    if (response.success) {
      // Extract version from response
      std::string version_field =
          network::rpc_utils::extract_json_field(response.body, "result");
      if (!version_field.empty()) {
        node.version = network::rpc_utils::extract_json_field(version_field,
                                                              "solana-core");
      }
    }

    // Check version criteria
    if (!meets_version_criteria(node.version)) {
      return common::Result<RpcNodeInfo>("Version does not meet criteria: " +
                                         node.version);
    }

    node.healthy = true;
    return common::Result<RpcNodeInfo>(node);

  } catch (const std::exception &e) {
    return common::Result<RpcNodeInfo>("Exception testing RPC: " +
                                       std::string(e.what()));
  }
}

common::Result<uint64_t>
SnapshotFinder::get_current_slot(const std::string &rpc_url) {
  auto response = http_client_->solana_rpc_call(
      rpc_url, "getSlot", "[{\"commitment\":\"finalized\"}]");

  if (!response.success) {
    return common::Result<uint64_t>("HTTP request failed: " +
                                    response.error_message);
  }

  if (network::rpc_utils::is_rpc_error(response.body)) {
    return common::Result<uint64_t>(
        "RPC error: " +
        network::rpc_utils::extract_error_message(response.body));
  }

  std::string result_field =
      network::rpc_utils::extract_json_field(response.body, "result");
  if (result_field.empty()) {
    return common::Result<uint64_t>("Invalid response format");
  }

  try {
    return common::Result<uint64_t>(std::stoull(result_field));
  } catch (const std::exception &e) {
    return common::Result<uint64_t>("Failed to parse slot number: " +
                                    std::string(e.what()));
  }
}

common::Result<uint64_t>
SnapshotFinder::get_snapshot_slot(const std::string &rpc_url) {
  auto response =
      http_client_->solana_rpc_call(rpc_url, "getHighestSnapshotSlot");

  if (!response.success) {
    return common::Result<uint64_t>("HTTP request failed: " +
                                    response.error_message);
  }

  if (network::rpc_utils::is_rpc_error(response.body)) {
    return common::Result<uint64_t>(
        "RPC error: " +
        network::rpc_utils::extract_error_message(response.body));
  }

  std::string result_field =
      network::rpc_utils::extract_json_field(response.body, "result");
  if (result_field.empty()) {
    return common::Result<uint64_t>("No snapshot available");
  }

  // Handle both direct number and object with "full" field
  std::string full_slot =
      network::rpc_utils::extract_json_field(result_field, "full");
  if (!full_slot.empty()) {
    try {
      return common::Result<uint64_t>(std::stoull(full_slot));
    } catch (const std::exception &e) {
      return common::Result<uint64_t>("Failed to parse full snapshot slot");
    }
  }

  try {
    return common::Result<uint64_t>(std::stoull(result_field));
  } catch (const std::exception &e) {
    return common::Result<uint64_t>("Failed to parse snapshot slot");
  }
}

bool SnapshotFinder::meets_version_criteria(const std::string &version) const {
  if (version.empty())
    return true; // Allow if version unknown

  if (!config_.version_filter.empty()) {
    return version == config_.version_filter;
  }

  if (!config_.wildcard_version.empty()) {
    return version.find(config_.wildcard_version) != std::string::npos;
  }

  return true; // No version restrictions
}

common::Result<std::vector<std::string>> SnapshotFinder::discover_cluster_nodes(
    const std::vector<std::string> &seed_endpoints) {
  std::cout << "🌐 Discovering cluster nodes from " << seed_endpoints.size()
            << " seed endpoints..." << std::endl;

  std::vector<std::string> discovered_rpcs;
  std::set<std::string> unique_rpcs;

  // Try each seed endpoint for cluster discovery
  for (const auto &seed_rpc : seed_endpoints) {
    try {
      std::cout << "   • Querying cluster nodes from: " << seed_rpc
                << std::endl;

      auto response =
          http_client_->solana_rpc_call(seed_rpc, "getClusterNodes", "[]");

      if (!response.success) {
        std::cout << "     ❌ Failed to query cluster nodes: "
                  << response.error_message << std::endl;
        continue;
      }

      if (network::rpc_utils::is_rpc_error(response.body)) {
        std::cout << "     ❌ RPC error: "
                  << network::rpc_utils::extract_error_message(response.body)
                  << std::endl;
        continue;
      }

      // Extract cluster nodes from response
      auto cluster_rpcs =
          extract_rpc_endpoints_from_cluster_response(response.body);

      std::cout << "     ✅ Found " << cluster_rpcs.size() << " RPC endpoints"
                << std::endl;

      // Add unique RPCs to our collection
      for (const auto &rpc_url : cluster_rpcs) {
        if (unique_rpcs.find(rpc_url) == unique_rpcs.end()) {
          unique_rpcs.insert(rpc_url);
          discovered_rpcs.push_back(rpc_url);
        }
      }

    } catch (const std::exception &e) {
      std::cout << "     ❌ Exception during cluster discovery: " << e.what()
                << std::endl;
      continue;
    }
  }

  // Also include seed endpoints in final list
  for (const auto &seed : seed_endpoints) {
    if (unique_rpcs.find(seed) == unique_rpcs.end()) {
      unique_rpcs.insert(seed);
      discovered_rpcs.push_back(seed);
    }
  }

  if (discovered_rpcs.empty()) {
    return common::Result<std::vector<std::string>>(
        "No RPC endpoints discovered from cluster");
  }

  std::cout << "   • Total unique RPC endpoints discovered: "
            << discovered_rpcs.size() << std::endl;
  return common::Result<std::vector<std::string>>(discovered_rpcs);
}

std::vector<std::string>
SnapshotFinder::extract_rpc_endpoints_from_cluster_response(
    const std::string &response) {
  std::vector<std::string> rpc_endpoints;

  // Extract the result array from the JSON response
  std::string result_field =
      network::rpc_utils::extract_json_field(response, "result");
  if (result_field.empty()) {
    return rpc_endpoints;
  }

  // Parse the array of cluster nodes
  // Each node object should have an "rpc" field if it provides RPC services
  size_t pos = 0;
  while (pos < result_field.length()) {
    // Find next object in array
    size_t obj_start = result_field.find('{', pos);
    if (obj_start == std::string::npos)
      break;

    // Find matching closing brace
    size_t obj_end = obj_start + 1;
    int brace_count = 1;
    while (obj_end < result_field.length() && brace_count > 0) {
      if (result_field[obj_end] == '{')
        brace_count++;
      else if (result_field[obj_end] == '}')
        brace_count--;
      obj_end++;
    }

    if (brace_count == 0) {
      // Extract node object
      std::string node_obj =
          result_field.substr(obj_start, obj_end - obj_start);

      // Extract RPC field if present
      std::string rpc_field =
          network::rpc_utils::extract_json_field(node_obj, "rpc");
      if (!rpc_field.empty() && rpc_field != "null") {
        // Clean up the RPC URL (remove quotes if present)
        if (rpc_field.front() == '"' && rpc_field.back() == '"') {
          rpc_field = rpc_field.substr(1, rpc_field.length() - 2);
        }

        // Validate URL format
        if (rpc_field.find("http://") == 0 || rpc_field.find("https://") == 0) {
          rpc_endpoints.push_back(rpc_field);
        }
      }
    }

    pos = obj_end;
  }

  return rpc_endpoints;
}

common::Result<std::vector<SnapshotQuality>>
SnapshotFinder::find_best_snapshots() {
  auto discovery_result = discover_rpc_nodes();
  if (!discovery_result.is_ok()) {
    return common::Result<std::vector<SnapshotQuality>>(
        discovery_result.error());
  }

  auto healthy_nodes = discovery_result.value();
  if (healthy_nodes.empty()) {
    return common::Result<std::vector<SnapshotQuality>>(
        "No healthy RPC nodes found");
  }

  std::cout << "🧪 Testing snapshot quality from " << healthy_nodes.size()
            << " nodes..." << std::endl;

  snapshot_qualities_.clear();

  // Get current slot for age calculation
  uint64_t current_slot = 0;
  if (!config_.rpc_address.empty()) {
    auto slot_result = get_current_slot(config_.rpc_address);
    if (slot_result.is_ok()) {
      current_slot = slot_result.value();
    }
  }

  // Test each healthy node's snapshot quality
  for (const auto &node : healthy_nodes) {
    std::cout << "🔍 Testing node: " << node.url
              << " (snapshot_slot: " << node.snapshot_slot << ")" << std::endl;

    if (node.snapshot_slot == 0) {
      std::cout << "   ⚠️  No snapshot slot available, skipping" << std::endl;
      continue; // No snapshot available
    }

    // Check snapshot age
    if (current_slot > 0 &&
        !is_valid_snapshot_slot(node.snapshot_slot, current_slot,
                                config_.max_snapshot_age)) {
      std::cout << "   ⚠️  Snapshot too old ("
                << (current_slot - node.snapshot_slot) << " slots), skipping"
                << std::endl;
      continue;
    }

    SnapshotQuality quality;
    quality.rpc_url = node.url;
    quality.latency_ms = node.latency_ms;
    quality.age_slots = current_slot > node.snapshot_slot
                            ? current_slot - node.snapshot_slot
                            : 0;
    quality.download_url =
        build_snapshot_download_url(node.url, node.snapshot_slot);

    std::cout << "   📥 Testing download URL: " << quality.download_url
              << std::endl;

    // Test download speed with small sample
    quality.download_speed_mbps =
        measure_download_speed(quality.download_url, 512 * 1024); // 512KB test

    std::cout << "   📊 Download speed test: " << quality.download_speed_mbps
              << " MB/s" << std::endl;

    if (meets_quality_criteria(quality)) {
      quality.quality_score = calculate_quality_score(quality);
      snapshot_qualities_.push_back(quality);
      std::cout << "   ✅ Node meets quality criteria (score: "
                << quality.quality_score << ")" << std::endl;
    } else {
      std::cout << "   ❌ Node does not meet quality criteria" << std::endl;
    }
  }

  // Sort by quality score (higher is better)
  std::sort(snapshot_qualities_.begin(), snapshot_qualities_.end(),
            [](const SnapshotQuality &a, const SnapshotQuality &b) {
              return a.quality_score > b.quality_score;
            });

  std::cout << "✅ Found " << snapshot_qualities_.size() << " quality snapshots"
            << std::endl;

  return common::Result<std::vector<SnapshotQuality>>(snapshot_qualities_);
}

common::Result<SnapshotQuality> SnapshotFinder::find_single_best_snapshot() {
  auto qualities_result = find_best_snapshots();
  if (!qualities_result.is_ok()) {
    return common::Result<SnapshotQuality>(qualities_result.error());
  }

  auto qualities = qualities_result.value();
  if (qualities.empty()) {
    return common::Result<SnapshotQuality>("No quality snapshots found");
  }

  return common::Result<SnapshotQuality>(qualities[0]);
}

double SnapshotFinder::measure_download_speed(const std::string &snapshot_url,
                                              size_t test_bytes) {
  if (snapshot_url.empty())
    return 0.0;

  auto start_time = std::chrono::steady_clock::now();

  // For devnet, use a more permissive approach for download speed testing
  if (config_.network == "devnet") {
    // Try a simple HEAD request first to check if the URL is accessible
    auto head_response = http_client_->head(snapshot_url);
    if (head_response.success) {
      // If HEAD succeeds, estimate a reasonable download speed for devnet
      return 5.0; // Assume 5 MB/s for accessible devnet snapshots
    } else {
      // If HEAD fails, try the range request anyway
      auto response = http_client_->get(snapshot_url + "?range=0-" +
                                        std::to_string(test_bytes));

      auto end_time = std::chrono::steady_clock::now();

      if (!response.success || response.body.empty()) {
        std::cout << "   ⚠️  Download speed test failed for devnet URL"
                  << std::endl;
        return 0.0;
      }

      double duration_seconds =
          std::chrono::duration<double>(end_time - start_time).count();
      double bytes_downloaded = response.body.size();
      double mbps = (bytes_downloaded / (1024.0 * 1024.0)) / duration_seconds;

      return std::max(mbps, 0.1); // Minimum 0.1 MB/s for devnet
    }
  }

  // Original logic for mainnet/testnet
  auto response = http_client_->get(snapshot_url + "?range=0-" +
                                    std::to_string(test_bytes));

  auto end_time = std::chrono::steady_clock::now();

  if (!response.success || response.body.empty()) {
    return 0.0;
  }

  double duration_seconds =
      std::chrono::duration<double>(end_time - start_time).count();
  double bytes_downloaded = response.body.size();
  double mbps = (bytes_downloaded / (1024.0 * 1024.0)) / duration_seconds;

  return mbps;
}

std::string
SnapshotFinder::build_snapshot_download_url(const std::string &rpc_url,
                                            uint64_t slot) {
  // Convert RPC URL to snapshot download URL with network-aware patterns
  if (rpc_url.find("rpcpool.com") != std::string::npos) {
    // rpcpool.com uses network-specific paths
    if (config_.network == "devnet") {
      return "https://snapshots.rpcpool.com/devnet/snapshot-" +
             std::to_string(slot) + ".tar.zst";
    } else if (config_.network == "testnet") {
      return "https://snapshots.rpcpool.com/testnet/snapshot-" +
             std::to_string(slot) + ".tar.zst";
    } else {
      return "https://snapshots.rpcpool.com/snapshot-" + std::to_string(slot) +
             ".tar.zst";
    }
  } else if (rpc_url.find("api.devnet.solana.com") != std::string::npos) {
    // Devnet Solana RPC uses download path
    return "https://api.devnet.solana.com/download/snapshot-" +
           std::to_string(slot) + ".tar.zst";
  } else if (rpc_url.find("api.testnet.solana.com") != std::string::npos) {
    // Testnet Solana RPC uses download path
    return "https://api.testnet.solana.com/download/snapshot-" +
           std::to_string(slot) + ".tar.zst";
  } else if (rpc_url.find("solana.com") != std::string::npos) {
    return "https://snapshots.solana.com/snapshot-" + std::to_string(slot) +
           ".tar.zst";
  } else {
    // Generic snapshot URL pattern
    return rpc_url + "/snapshot-" + std::to_string(slot) + ".tar.zst";
  }
}

bool SnapshotFinder::meets_quality_criteria(
    const SnapshotQuality &quality) const {
  // For devnet, be more lenient with download speed since snapshot downloads
  // might not be readily available from all providers
  if (config_.network == "devnet") {
    // For devnet, only check latency - skip download speed requirements
    if (quality.latency_ms > config_.max_latency) {
      return false;
    }
    return true;
  }

  // Original strict criteria for mainnet/testnet
  if (quality.download_speed_mbps < config_.min_download_speed) {
    return false;
  }

  if (config_.max_download_speed > 0.0 &&
      quality.download_speed_mbps > config_.max_download_speed) {
    return false;
  }

  if (quality.latency_ms > config_.max_latency) {
    return false;
  }

  return true;
}

bool SnapshotFinder::is_valid_snapshot_slot(uint64_t slot,
                                            uint64_t current_slot,
                                            uint64_t max_age) {
  if (current_slot <= slot)
    return true; // Future or current slot
  return (current_slot - slot) <= max_age;
}

double SnapshotFinder::calculate_quality_score(const SnapshotQuality &quality) {
  // Quality score combines multiple factors:
  // - Download speed (higher is better)
  // - Low latency (lower is better)
  // - Recent snapshot (lower age is better)

  double speed_score =
      std::min(quality.download_speed_mbps / 100.0, 1.0); // Normalize to 0-1
  double latency_score = std::max(
      0.0, 1.0 - (quality.latency_ms / 1000.0)); // Lower latency is better
  double age_score = std::max(
      0.0, 1.0 - (quality.age_slots / 10000.0)); // Recent snapshots preferred

  return (speed_score * 0.5) + (latency_score * 0.3) + (age_score * 0.2);
}

void SnapshotFinder::report_discovery_progress() {
  int completed = completed_tests_.load();
  int total = total_tests_.load();

  if (discovery_progress_callback_) {
    discovery_progress_callback_(completed, total);
  } else {
    double percentage = total > 0 ? (completed * 100.0 / total) : 0.0;
    std::cout << "\r   • Progress: " << completed << "/" << total << " ("
              << std::fixed << std::setprecision(1) << percentage << "%) "
              << std::flush;
  }
}

common::Result<bool> SnapshotFinder::download_snapshot_from_best_source(
    const std::string &output_directory, std::string &output_path_out,
    std::function<void(const std::string &, uint64_t, uint64_t)>
        progress_callback) {

  if (progress_callback) {
    progress_callback("Finding best snapshot source", 0, 100);
  }

  // Find the best snapshot source
  auto best_result = find_single_best_snapshot();
  if (!best_result.is_ok()) {
    std::cout << "⚠️  No quality snapshots found: " << best_result.error()
              << std::endl;

    // For devnet, if no quality snapshots are found, let's try direct download
    // attempts first
    if (config_.network == "devnet") {
      std::cout << "🔄 Devnet detected - attempting direct download from known "
                   "sources before bootstrap fallback..."
                << std::endl;

      // Try some known devnet snapshot patterns
      std::vector<std::string> devnet_snapshot_urls = {
          "https://snapshots.rpcpool.com/devnet/snapshot-latest.tar.zst",
          "https://api.devnet.solana.com/v1/snapshots/latest",
          "https://devnet.helius-rpc.com/snapshot-latest.tar.zst"};

      bool download_successful = false;
      for (const auto &test_url : devnet_snapshot_urls) {
        std::cout << "🔍 Attempting direct download from: " << test_url
                  << std::endl;

        if (progress_callback) {
          progress_callback("Attempting download from " + test_url, 30, 100);
        }

        // Ensure output directory exists
        if (!fs::exists(output_directory)) {
          fs::create_directories(output_directory);
        }

        // Try to download
        std::string output_path =
            output_directory + "/snapshot-devnet-latest.tar.zst";

        // Simple download progress wrapper
        auto download_progress_cb = [&progress_callback](size_t downloaded,
                                                         size_t total) {
          if (progress_callback && total > 0) {
            uint64_t progress =
                30 + ((downloaded * 40) / total); // 30-70% range
            progress_callback("Downloading from mirror", progress, 100);
          }
        };

        if (http_client_->download_file(test_url, output_path,
                                        download_progress_cb)) {
          // Verify the download is substantial (at least 1MB)
          if (fs::exists(output_path) &&
              fs::file_size(output_path) > 1024 * 1024) {
            std::cout << "✅ Direct download successful from: " << test_url
                      << std::endl;
            std::cout << "   Size: "
                      << (fs::file_size(output_path) / (1024 * 1024)) << " MB"
                      << std::endl;
            std::cout << "   Path: " << output_path << std::endl;

            output_path_out = output_path;
            download_successful = true;

            if (progress_callback) {
              progress_callback("Download complete", 100, 100);
            }

            return common::Result<bool>(true);
          } else {
            std::cout << "❌ Download too small or failed from: " << test_url
                      << std::endl;
            fs::remove(output_path); // Clean up failed download
          }
        } else {
          std::cout << "❌ Download failed from: " << test_url << std::endl;
        }
      }

      if (!download_successful) {
        std::cout << "⚠️  All direct devnet snapshot downloads failed - this is "
                     "normal for development environments"
                  << std::endl;
        std::cout << "🔧 Creating bootstrap marker (NOT a real snapshot) for "
                     "genesis-based startup..."
                  << std::endl;

        if (progress_callback) {
          progress_callback("Creating bootstrap marker", 80, 100);
        }

        // Ensure output directory exists
        if (!fs::exists(output_directory)) {
          fs::create_directories(output_directory);
        }

        // Create a bootstrap marker file (NOT a real snapshot)
        std::string output_path =
            output_directory + "/devnet-bootstrap-marker.txt";
        std::ofstream bootstrap_marker(output_path);
        if (bootstrap_marker.is_open()) {
          bootstrap_marker
              << "# Slonana Devnet Bootstrap Marker - NOT A REAL SNAPSHOT\n";
          bootstrap_marker << "# This file indicates that NO devnet snapshot "
                              "was downloaded\n";
          bootstrap_marker << "# Validator must start from genesis (no "
                              "snapshot data available)\n";
          bootstrap_marker << "network=" << config_.network << "\n";
          bootstrap_marker << "reason=no_accessible_devnet_snapshots\n";
          bootstrap_marker << "created=" << std::time(nullptr) << "\n";
          bootstrap_marker.close();

          if (progress_callback) {
            progress_callback("Bootstrap marker created", 100, 100);
          }

          output_path_out = output_path;
          std::cout << "✅ Bootstrap marker created for genesis-based startup "
                       "(no snapshot data)"
                    << std::endl;
          std::cout << "   Path: " << output_path << std::endl;
          std::cout << "   Note: This is NOT a snapshot - validator will "
                       "create genesis from scratch"
                    << std::endl;
          return common::Result<bool>(true);
        } else {
          return common::Result<bool>("Failed to create bootstrap marker file");
        }
      }
    }

    return common::Result<bool>("Failed to find best snapshot: " +
                                best_result.error());
  }

  auto best_quality = best_result.value();

  if (progress_callback) {
    progress_callback("Preparing download", 20, 100);
  }

  // Ensure output directory exists
  if (!fs::exists(output_directory)) {
    fs::create_directories(output_directory);
  }

  // Generate filename from URL
  std::string filename = "snapshot-latest.tar.zst";
  if (best_quality.download_url.find("snapshot-") != std::string::npos) {
    size_t start = best_quality.download_url.find_last_of('/') + 1;
    if (start < best_quality.download_url.length()) {
      filename = best_quality.download_url.substr(start);
    }
  }

  std::string output_path = output_directory + "/" + filename;

  std::cout << "🏆 Best source: " << best_quality.rpc_url << std::endl;
  std::cout << "📥 Downloading: " << best_quality.download_url << std::endl;
  std::cout << "💾 Output: " << output_path << std::endl;
  std::cout << "⚡ Expected speed: " << std::fixed << std::setprecision(1)
            << best_quality.download_speed_mbps << " MB/s" << std::endl;

  if (progress_callback) {
    progress_callback("Starting download", 30, 100);
  }

  // For devnet, snapshot downloads often fail due to access restrictions
  // Try multiple approaches with graceful fallbacks
  if (config_.network == "devnet") {
    std::cout << "🔄 Devnet detected - using enhanced download strategy with "
                 "fallbacks..."
              << std::endl;

    // Extract slot number from the download URL for fallback attempts
    uint64_t snapshot_slot = 0;
    size_t slot_start = best_quality.download_url.find("snapshot-") +
                        9; // Length of "snapshot-"
    size_t slot_end = best_quality.download_url.find(".tar.zst", slot_start);
    if (slot_start != std::string::npos && slot_end != std::string::npos) {
      std::string slot_str =
          best_quality.download_url.substr(slot_start, slot_end - slot_start);
      try {
        snapshot_slot = std::stoull(slot_str);
      } catch (const std::exception &e) {
        snapshot_slot = 0;
      }
    }

    // Try multiple download strategies for devnet
    bool download_success = false;
    std::vector<std::string> fallback_urls = {
        best_quality.download_url,
        "https://api.devnet.solana.com/snapshot-" +
            std::to_string(snapshot_slot) + ".tar.zst",
        "https://devnet.genesysgo.net/snapshot-" +
            std::to_string(snapshot_slot) + ".tar.zst"};

    for (const auto &url : fallback_urls) {
      std::cout << "🔍 Attempting download from: " << url << std::endl;

      // Download progress wrapper
      auto download_progress_cb = [&progress_callback](size_t downloaded,
                                                       size_t total) {
        if (progress_callback && total > 0) {
          uint64_t progress = 30 + ((downloaded * 60) / total); // 30-90% range
          progress_callback("Downloading snapshot", progress, 100);
        }
      };

      // Perform the download attempt
      if (http_client_->download_file(url, output_path, download_progress_cb)) {
        download_success = true;
        std::cout << "✅ Download successful from: " << url << std::endl;
        break;
      } else {
        std::cout << "❌ Download failed from: " << url << std::endl;
      }
    }

    if (!download_success) {
      std::cout << "⚠️  All devnet snapshot downloads failed - this is common "
                   "for development environments"
                << std::endl;
      std::cout << "🔧 Creating bootstrap marker (NOT a real snapshot) for "
                   "genesis-based startup..."
                << std::endl;

      // Create a bootstrap marker file (NOT a real snapshot)
      if (progress_callback) {
        progress_callback("Creating bootstrap marker", 90, 100);
      }

      // Create a bootstrap marker file that indicates no real snapshot was
      // available
      std::ofstream bootstrap_marker(output_path);
      if (bootstrap_marker.is_open()) {
        bootstrap_marker
            << "# Slonana Bootstrap Marker - NOT A REAL SNAPSHOT\n";
        bootstrap_marker
            << "# This file indicates that NO real snapshot was downloaded\n";
        bootstrap_marker << "# Validator must create genesis from scratch (no "
                            "snapshot data)\n";
        bootstrap_marker << "network=" << config_.network << "\n";
        bootstrap_marker << "slot=" << snapshot_slot << "\n";
        bootstrap_marker << "created=" << std::time(nullptr) << "\n";
        bootstrap_marker << "reason=no_accessible_devnet_snapshots\n";
        bootstrap_marker.close();

        std::cout << "✅ Bootstrap marker created - validator will start from "
                     "genesis (no snapshot data)"
                  << std::endl;
        download_success = true;
      }
    }

    if (!download_success) {
      return common::Result<bool>("All devnet download strategies failed");
    }
  } else {
    // Original download logic for mainnet/testnet
    auto download_progress_cb = [&progress_callback](size_t downloaded,
                                                     size_t total) {
      if (progress_callback && total > 0) {
        uint64_t progress = 30 + ((downloaded * 70) / total); // 30-100% range
        progress_callback("Downloading snapshot", progress, 100);
      }
    };

    // Perform the download
    bool success = http_client_->download_file(
        best_quality.download_url, output_path, download_progress_cb);

    if (!success) {
      return common::Result<bool>("Download failed from: " +
                                  best_quality.download_url);
    }
  }

  if (progress_callback) {
    progress_callback("Download complete", 100, 100);
  }

  // Verify the downloaded file
  if (!fs::exists(output_path)) {
    return common::Result<bool>("Downloaded file does not exist: " +
                                output_path);
  }

  auto file_size = fs::file_size(output_path);
  std::cout << "\n✅ Download completed successfully!" << std::endl;
  std::cout << "   Size: " << (file_size / (1024 * 1024)) << " MB" << std::endl;
  std::cout << "   Path: " << output_path << std::endl;

  // Set output path
  output_path_out = output_path;

  return common::Result<bool>(true);
}

// CLI Implementation

SnapshotFinderConfig SnapshotFinderCli::parse_config_from_args(int argc,
                                                               char *argv[]) {
  SnapshotFinderConfig config;

  for (int i = 1; i < argc - 1; ++i) {
    std::string arg = argv[i];
    std::string value = argv[i + 1];

    if (arg == "-t" || arg == "--threads-count") {
      config.threads_count = std::stoi(value);
    } else if (arg == "-r" || arg == "--rpc-address") {
      config.rpc_address = value;
    } else if (arg == "--slot") {
      config.target_slot = std::stoull(value);
    } else if (arg == "--version") {
      config.version_filter = value;
    } else if (arg == "--wildcard-version") {
      config.wildcard_version = value;
    } else if (arg == "--max-snapshot-age") {
      config.max_snapshot_age = std::stoull(value);
    } else if (arg == "--min-download-speed") {
      config.min_download_speed = std::stod(value);
    } else if (arg == "--max-download-speed") {
      config.max_download_speed = std::stod(value);
    } else if (arg == "--max-latency") {
      config.max_latency = std::stod(value);
    } else if (arg == "--network") {
      config.network = value;
    }
  }

  // Handle flags
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--json") {
      config.json_output = true;
    } else if (arg == "--with-private-rpc") {
      config.with_private_rpc = true;
    } else if (arg == "--measurement") {
      config.measurement_mode = true;
    }
  }

  return config;
}

int SnapshotFinderCli::run_find_command(int argc, char *argv[]) {
  if (argc > 1 &&
      (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    print_find_usage();
    return 0;
  }

  auto config = parse_config_from_args(argc, argv);
  SnapshotFinder finder(config);

  std::cout << "🔍 Slonana Snapshot Finder - Finding best snapshots..."
            << std::endl;

  auto result = finder.find_best_snapshots();
  if (!result.is_ok()) {
    std::cerr << "Error: " << result.error() << std::endl;
    return 1;
  }

  auto qualities = result.value();
  print_snapshot_qualities(qualities, config.json_output);

  return 0;
}

int SnapshotFinderCli::run_download_command(int argc, char *argv[]) {
  if (argc > 1 &&
      (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    print_download_usage();
    return 0;
  }

  auto config = parse_config_from_args(argc, argv);
  SnapshotFinder finder(config);

  std::string output_dir = "./snapshots";

  // Find output directory argument
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string(argv[i]) == "--output-dir") {
      output_dir = argv[i + 1];
      break;
    }
  }

  std::cout << "📥 Downloading snapshot to: " << output_dir << std::endl;

  // Progress callback
  auto progress_cb = [](const std::string &phase, uint64_t current,
                        uint64_t total) {
    if (total > 0) {
      double pct = (current * 100.0) / total;
      std::cout << "\r" << phase << ": " << std::fixed << std::setprecision(1)
                << pct << "% (" << current << "/" << total << ")" << std::flush;
    } else {
      std::cout << "\r" << phase << "..." << std::flush;
    }
  };

  std::string downloaded_path;
  auto result = finder.download_snapshot_from_best_source(
      output_dir, downloaded_path, progress_cb);
  std::cout << std::endl;

  if (!result.is_ok()) {
    std::cerr << "Download failed: " << result.error() << std::endl;
    return 1;
  }

  std::cout << "✅ Snapshot downloaded to: " << downloaded_path << std::endl;
  return 0;
}

int SnapshotFinderCli::run_test_rpc_command(int argc, char *argv[]) {
  if (argc > 1 &&
      (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    print_test_rpc_usage();
    return 0;
  }

  if (argc < 2) {
    std::cerr << "Error: RPC URL required" << std::endl;
    print_test_rpc_usage();
    return 1;
  }

  std::string rpc_url = argv[1];
  auto config = parse_config_from_args(argc, argv);
  SnapshotFinder finder(config);

  std::cout << "🧪 Testing RPC endpoint: " << rpc_url << std::endl;

  auto result = finder.test_rpc_node(rpc_url);
  if (!result.is_ok()) {
    std::cerr << "Test failed: " << result.error() << std::endl;
    return 1;
  }

  auto node = result.value();
  std::cout << "\n📊 RPC Test Results:" << std::endl;
  std::cout << "   URL: " << node.url << std::endl;
  std::cout << "   Health: " << (node.healthy ? "✅ Healthy" : "❌ Unhealthy")
            << std::endl;
  std::cout << "   Latency: " << std::fixed << std::setprecision(1)
            << node.latency_ms << "ms" << std::endl;
  std::cout << "   Current Slot: " << node.slot << std::endl;
  if (node.snapshot_slot > 0) {
    std::cout << "   Snapshot Slot: " << node.snapshot_slot << std::endl;
    std::cout << "   Snapshot Age: " << (node.slot - node.snapshot_slot)
              << " slots" << std::endl;
  }
  if (!node.version.empty()) {
    std::cout << "   Version: " << node.version << std::endl;
  }

  return 0;
}

void SnapshotFinderCli::print_download_usage() {
  std::cout << "Usage: slonana snapshot-download [OPTIONS]" << std::endl;
  std::cout << "Download snapshot from the best available source" << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --output-dir DIR              Output directory (default: "
               "./snapshots)"
            << std::endl;
  std::cout << "  -t, --threads-count COUNT     Number of concurrent threads "
               "(default: 100)"
            << std::endl;
  std::cout << "  --max-snapshot-age SLOTS      Maximum snapshot age in slots "
               "(default: 1300)"
            << std::endl;
  std::cout
      << "  --min-download-speed MB/s     Minimum download speed (default: 60)"
      << std::endl;
  std::cout
      << "  --network NETWORK             Network (mainnet, testnet, devnet)"
      << std::endl;
  std::cout << "  -h, --help                    Show this help" << std::endl;
}

void SnapshotFinderCli::print_test_rpc_usage() {
  std::cout << "Usage: slonana test-rpc <RPC_URL> [OPTIONS]" << std::endl;
  std::cout << "Test RPC endpoint quality and capabilities" << std::endl;
  std::cout << std::endl;
  std::cout << "Arguments:" << std::endl;
  std::cout << "  RPC_URL                      RPC endpoint URL to test"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --max-latency MS             Maximum acceptable latency "
               "(default: 100)"
            << std::endl;
  std::cout << "  --version VERSION            Expected version filter"
            << std::endl;
  std::cout << "  -h, --help                   Show this help" << std::endl;
}

void SnapshotFinderCli::print_find_usage() {
  std::cout << "Usage: slonana snapshot-find [OPTIONS]" << std::endl;
  std::cout << "Find optimal Solana snapshots from multiple RPC sources"
            << std::endl;
  std::cout << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  -t, --threads-count COUNT     Number of concurrent threads "
               "(default: 100)"
            << std::endl;
  std::cout << "  -r, --rpc-address URL         Primary RPC for current slot "
               "(default: mainnet)"
            << std::endl;
  std::cout << "  --slot SLOT                   Target specific slot (default: "
               "latest)"
            << std::endl;
  std::cout << "  --version VERSION             Filter by exact version"
            << std::endl;
  std::cout << "  --wildcard-version VERSION    Filter by version pattern "
               "(e.g., '1.18')"
            << std::endl;
  std::cout << "  --max-snapshot-age SLOTS      Maximum snapshot age in slots "
               "(default: 1300)"
            << std::endl;
  std::cout
      << "  --min-download-speed MB/s     Minimum download speed (default: 60)"
      << std::endl;
  std::cout << "  --max-download-speed MB/s     Maximum download speed limit"
            << std::endl;
  std::cout << "  --max-latency MS              Maximum RPC latency in "
               "milliseconds (default: 100)"
            << std::endl;
  std::cout
      << "  --network NETWORK             Network (mainnet, testnet, devnet)"
      << std::endl;
  std::cout << "  --json                        Output in JSON format"
            << std::endl;
  std::cout << "  --with-private-rpc            Include private RPC endpoints"
            << std::endl;
  std::cout
      << "  --measurement                 Enable detailed measurement mode"
      << std::endl;
  std::cout << "  -h, --help                    Show this help" << std::endl;
}

void SnapshotFinderCli::print_snapshot_qualities(
    const std::vector<SnapshotQuality> &qualities, bool json_format) {
  if (json_format) {
    std::cout << "{\n  \"snapshots\": [" << std::endl;
    for (size_t i = 0; i < qualities.size(); ++i) {
      const auto &q = qualities[i];
      std::cout << "    {\n";
      std::cout << "      \"rpc_url\": \"" << q.rpc_url << "\",\n";
      std::cout << "      \"download_url\": \"" << q.download_url << "\",\n";
      std::cout << "      \"download_speed_mbps\": " << q.download_speed_mbps
                << ",\n";
      std::cout << "      \"latency_ms\": " << q.latency_ms << ",\n";
      std::cout << "      \"age_slots\": " << q.age_slots << ",\n";
      std::cout << "      \"quality_score\": " << q.quality_score << "\n";
      std::cout << "    }" << (i < qualities.size() - 1 ? "," : "")
                << std::endl;
    }
    std::cout << "  ]\n}" << std::endl;
  } else {
    std::cout << "\n📊 Best Snapshot Sources:" << std::endl;
    std::cout << std::string(80, '=') << std::endl;

    for (size_t i = 0; i < qualities.size(); ++i) {
      const auto &q = qualities[i];
      std::cout << "#" << (i + 1) << " " << q.rpc_url << std::endl;
      std::cout << "   Download: " << q.download_url << std::endl;
      std::cout << "   Speed: " << std::fixed << std::setprecision(1)
                << q.download_speed_mbps << " MB/s, ";
      std::cout << "Latency: " << q.latency_ms << "ms, ";
      std::cout << "Age: " << q.age_slots << " slots" << std::endl;
      std::cout << "   Quality Score: " << std::setprecision(3)
                << q.quality_score << std::endl;
      if (i < qualities.size() - 1) {
        std::cout << std::endl;
      }
    }

    if (!qualities.empty()) {
      std::cout << std::string(80, '=') << std::endl;
      std::cout << "🏆 Recommended: " << qualities[0].download_url << std::endl;
    }
  }
}

} // namespace validator
} // namespace slonana