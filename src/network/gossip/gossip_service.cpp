#include "network/gossip/gossip_service.h"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace slonana {
namespace network {
namespace gossip {

GossipService::GossipService(const Config &config)
    : config_(config), crds_(std::make_unique<Crds>()), running_(false),
      shutdown_requested_(false), gossip_socket_(-1) {}

GossipService::~GossipService() { stop(); }

Result<bool> GossipService::start() {
  if (running_.load()) {
    return Result<bool>(std::string("Gossip service already running"));
  }

  std::cout << "Starting gossip service on " << config_.bind_address << ":"
            << config_.bind_port << std::endl;

  // Setup network socket
  if (!setup_socket()) {
    return Result<bool>(std::string("Failed to setup gossip socket"));
  }

  // Insert our own contact info
  ContactInfo self_info(config_.node_pubkey);
  self_info.shred_version = config_.shred_version;
  CrdsValue self_value(self_info);
  crds_->insert(self_value, timestamp(), GossipRoute::LocalMessage);

  running_.store(true);
  shutdown_requested_.store(false);

  // Start service threads
  threads_.emplace_back(&GossipService::receiver_thread, this);
  threads_.emplace_back(&GossipService::push_gossip_thread, this);
  threads_.emplace_back(&GossipService::pull_gossip_thread, this);
  threads_.emplace_back(&GossipService::trim_thread, this);

  if (config_.enable_ping_pong) {
    threads_.emplace_back(&GossipService::ping_pong_thread, this);
  }

  std::cout << "Gossip service started with " << threads_.size()
            << " threads" << std::endl;

  return Result<bool>(true);
}

void GossipService::stop() {
  if (!running_.load()) {
    return;
  }

  std::cout << "Stopping gossip service" << std::endl;
  shutdown_requested_.store(true);
  running_.store(false);

  // Join all threads
  for (auto &thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  threads_.clear();

  close_socket();
  std::cout << "Gossip service stopped" << std::endl;
}

Result<bool> GossipService::insert_local_value(const CrdsValue &value) {
  return crds_->insert(value, timestamp(), GossipRoute::LocalMessage);
}

std::vector<ContactInfo> GossipService::get_contact_infos() const {
  return crds_->get_contact_infos();
}

const ContactInfo *GossipService::get_contact_info(const PublicKey &pubkey) const {
  return crds_->get_contact_info(pubkey);
}

std::vector<PublicKey> GossipService::get_known_peers() const {
  auto contact_infos = crds_->get_contact_infos();
  std::vector<PublicKey> peers;
  peers.reserve(contact_infos.size());

  for (const auto &ci : contact_infos) {
    if (ci.pubkey != config_.node_pubkey) {
      peers.push_back(ci.pubkey);
    }
  }

  return peers;
}

std::vector<Vote> GossipService::get_votes(const PublicKey &pubkey) const {
  return crds_->get_votes(pubkey);
}

GossipService::Stats GossipService::get_stats() const {
  std::lock_guard<std::mutex> lock(stats_mutex_);
  Stats stats = stats_;
  stats.num_entries = crds_->len();
  stats.num_nodes = crds_->num_nodes();
  stats.num_votes = crds_->num_votes();
  return stats;
}

void GossipService::register_contact_info_callback(ContactInfoCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  contact_info_callbacks_.push_back(std::move(callback));
}

void GossipService::register_vote_callback(VoteCallback callback) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);
  vote_callbacks_.push_back(std::move(callback));
}

// Service threads
void GossipService::receiver_thread() {
  std::cout << "Gossip receiver thread started" << std::endl;

  while (!shutdown_requested_.load()) {
    auto result = receive_message();
    if (result.is_ok()) {
      Protocol msg = std::move(result).value();

      // Handle based on message type
      switch (msg.type()) {
      case Protocol::Type::PullRequest:
        handle_pull_request(msg, "");
        break;
      case Protocol::Type::PullResponse:
        handle_pull_response(msg);
        break;
      case Protocol::Type::PushMessage:
        handle_push_message(msg);
        break;
      case Protocol::Type::PruneMessage:
        handle_prune_message(msg);
        break;
      case Protocol::Type::PingMessage:
        handle_ping_message(msg, "");
        break;
      case Protocol::Type::PongMessage:
        handle_pong_message(msg);
        break;
      }

      std::lock_guard<std::mutex> lock(stats_mutex_);
      stats_.messages_received++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "Gossip receiver thread stopped" << std::endl;
}

void GossipService::push_gossip_thread() {
  std::cout << "Gossip push thread started" << std::endl;

  while (!shutdown_requested_.load()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.push_interval_ms));

    if (!shutdown_requested_.load()) {
      do_push_gossip();
    }
  }

  std::cout << "Gossip push thread stopped" << std::endl;
}

void GossipService::pull_gossip_thread() {
  std::cout << "Gossip pull thread started" << std::endl;

  while (!shutdown_requested_.load()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.pull_interval_ms));

    if (!shutdown_requested_.load()) {
      do_pull_gossip();
    }
  }

  std::cout << "Gossip pull thread stopped" << std::endl;
}

void GossipService::trim_thread() {
  std::cout << "Gossip trim thread started" << std::endl;

  while (!shutdown_requested_.load()) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(config_.trim_interval_ms));

    if (!shutdown_requested_.load()) {
      uint64_t now = timestamp();
      size_t trimmed = crds_->trim(now, config_.entry_timeout_ms);
      if (trimmed > 0) {
        std::cout << "Trimmed " << trimmed << " expired entries" << std::endl;
      }
    }
  }

  std::cout << "Gossip trim thread stopped" << std::endl;
}

void GossipService::ping_pong_thread() {
  std::cout << "Ping/pong thread started" << std::endl;

  while (!shutdown_requested_.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    if (!shutdown_requested_.load()) {
      // Send pings to random peers
      auto peers = get_known_peers();
      if (!peers.empty() && peers.size() <= 10) {
        for (const auto &peer : peers) {
          PingMessage ping(config_.node_pubkey);
          ping.generate_token();
          Protocol ping_msg = Protocol::create_ping_message(ping);

          // In production: send to peer's gossip address
          // For now, just track it
          std::lock_guard<std::mutex> lock(stats_mutex_);
          stats_.ping_messages_sent++;
        }
      }
    }
  }

  std::cout << "Ping/pong thread stopped" << std::endl;
}

// Message handling
void GossipService::handle_pull_request(const Protocol &msg,
                                        const std::string &from_addr) {
  // In production: respond with values not in their filter
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.pull_responses_sent++;
}

void GossipService::handle_pull_response(const Protocol &msg) {
  const auto *values = msg.get_values();
  if (!values)
    return;

  for (const auto &value : *values) {
    crds_->insert(value, timestamp(), GossipRoute::PullResponse);
    notify_callbacks(value);
  }
}

void GossipService::handle_push_message(const Protocol &msg) {
  const auto *values = msg.get_values();
  if (!values)
    return;

  for (const auto &value : *values) {
    crds_->insert(value, timestamp(), GossipRoute::PushMessage);
    notify_callbacks(value);
  }
}

void GossipService::handle_prune_message(const Protocol &msg) {
  const auto *prune = msg.get_prune_data();
  if (!prune || !prune->verify())
    return;

  // Remove pruned peers from active set
  std::lock_guard<std::mutex> lock(push_state_.mutex);
  for (const auto &pruned : prune->prunes) {
    auto it = std::find(push_state_.active_set.begin(),
                        push_state_.active_set.end(), pruned);
    if (it != push_state_.active_set.end()) {
      push_state_.active_set.erase(it);
    }
  }
}

void GossipService::handle_ping_message(const Protocol &msg,
                                        const std::string &from_addr) {
  const auto *ping = msg.get_ping();
  if (!ping || !ping->verify())
    return;

  // Send pong response
  PongMessage pong(config_.node_pubkey, ping->token);
  Protocol pong_msg = Protocol::create_pong_message(pong);

  // In production: send to from_addr
  std::lock_guard<std::mutex> lock(stats_mutex_);
  stats_.pong_messages_sent++;
}

void GossipService::handle_pong_message(const Protocol &msg) {
  const auto *pong = msg.get_pong();
  if (!pong || !pong->verify())
    return;

  // Calculate and record latency
  // In production: match token and calculate round-trip time
}

// Push gossip logic
void GossipService::do_push_gossip() {
  update_active_set();

  auto push_peers = select_push_peers(config_.gossip_push_fanout);
  if (push_peers.empty())
    return;

  auto values = select_values_to_push();
  if (values.empty())
    return;

  // Split values into chunks if needed
  auto chunks = split_gossip_messages<CrdsValue>(
      PUSH_MESSAGE_MAX_PAYLOAD_SIZE, values);

  for (const auto &chunk : chunks) {
    Protocol push_msg =
        Protocol::create_push_message(config_.node_pubkey, chunk);

    for (const auto &peer : push_peers) {
      // In production: send to peer's gossip address
      std::lock_guard<std::mutex> lock(stats_mutex_);
      stats_.push_messages_sent++;
    }
  }

  push_state_.last_push_time = timestamp();
}

std::vector<PublicKey> GossipService::select_push_peers(size_t count) {
  std::lock_guard<std::mutex> lock(push_state_.mutex);

  std::vector<PublicKey> selected;
  size_t available = std::min(count, push_state_.active_set.size());

  for (size_t i = 0; i < available; ++i) {
    selected.push_back(push_state_.active_set[i]);
  }

  return selected;
}

std::vector<CrdsValue> GossipService::select_values_to_push() {
  // Get recent entries from CRDS
  auto entries = crds_->get_entries_after(0, 100);
  
  std::vector<CrdsValue> values;
  for (const auto &entry : entries) {
    values.push_back(entry.value);
    if (values.size() >= 10)
      break; // Limit per push
  }

  return values;
}

// Pull gossip logic
void GossipService::do_pull_gossip() {
  auto pull_peers = select_pull_peers(config_.gossip_pull_fanout);
  if (pull_peers.empty())
    return;

  Protocol pull_request = build_pull_request();

  for (const auto &peer : pull_peers) {
    // In production: send to peer's gossip address
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.pull_requests_sent++;
  }

  pull_state_.last_pull_time = timestamp();
}

std::vector<PublicKey> GossipService::select_pull_peers(size_t count) {
  auto all_peers = get_known_peers();
  std::vector<PublicKey> selected;

  size_t available = std::min(count, all_peers.size());
  for (size_t i = 0; i < available; ++i) {
    selected.push_back(all_peers[i]);
  }

  return selected;
}

Protocol GossipService::build_pull_request() {
  // Build filter from our CRDS table
  std::lock_guard<std::mutex> lock(pull_state_.mutex);

  auto labels = crds_->get_labels();
  pull_state_.filter = CrdsFilter(labels.size());

  for (const auto &label : labels) {
    auto *entry = crds_->get(label);
    if (entry) {
      pull_state_.filter.add(entry->value.hash());
    }
  }

  // Create our contact info
  ContactInfo self_info(config_.node_pubkey);
  self_info.shred_version = config_.shred_version;
  CrdsValue self_value(self_info);

  return Protocol::create_pull_request(pull_state_.filter, self_value);
}

// Network operations
bool GossipService::send_message(const Protocol &msg,
                                 const std::string &dest_addr) {
  // In production: serialize and send via UDP
  return true;
}

Result<Protocol> GossipService::receive_message() {
  // In production: receive and deserialize UDP message
  // For now, just sleep
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return Result<Protocol>(std::string("No message"));
}

// Helper methods
void GossipService::update_active_set() {
  std::lock_guard<std::mutex> lock(push_state_.mutex);

  // Update active set with known peers
  push_state_.active_set.clear();
  auto peers = get_known_peers();

  // Add up to fanout * 2 peers to active set
  size_t max_active = config_.gossip_push_fanout * 2;
  for (size_t i = 0; i < std::min(peers.size(), max_active); ++i) {
    push_state_.active_set.push_back(peers[i]);
  }
}

void GossipService::notify_callbacks(const CrdsValue &value) {
  std::lock_guard<std::mutex> lock(callbacks_mutex_);

  if (std::holds_alternative<ContactInfo>(value.data())) {
    const auto &ci = std::get<ContactInfo>(value.data());
    for (const auto &callback : contact_info_callbacks_) {
      callback(ci);
    }
  } else if (std::holds_alternative<Vote>(value.data())) {
    const auto &vote = std::get<Vote>(value.data());
    for (const auto &callback : vote_callbacks_) {
      callback(vote);
    }
  }
}

bool GossipService::setup_socket() {
  gossip_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (gossip_socket_ < 0) {
    std::cerr << "Failed to create gossip socket" << std::endl;
    return false;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config_.bind_port);
  addr.sin_addr.s_addr = inet_addr(config_.bind_address.c_str());

  if (bind(gossip_socket_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "Failed to bind gossip socket" << std::endl;
    close(gossip_socket_);
    gossip_socket_ = -1;
    return false;
  }

  return true;
}

void GossipService::close_socket() {
  if (gossip_socket_ >= 0) {
    close(gossip_socket_);
    gossip_socket_ = -1;
  }
}

} // namespace gossip
} // namespace network
} // namespace slonana
