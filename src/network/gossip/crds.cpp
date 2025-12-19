#include "network/gossip/crds.h"
#include <algorithm>
#include <iostream>

namespace slonana {
namespace network {
namespace gossip {

Crds::Crds() : ordinal_counter_(0) {}

Crds::~Crds() {}

Result<bool> Crds::insert(const CrdsValue &value, uint64_t now,
                          GossipRoute route) {
  std::lock_guard<std::mutex> lock(mutex_);

  CrdsValueLabel label = value.label();
  PublicKey pk = value.pubkey();

  // Check if value exists
  auto it = table_.find(label);

  if (it == table_.end()) {
    // New entry - insert it
    VersionedCrdsValue versioned(value, ordinal_counter_++, now);
    versioned.from_pull_response = (route == GossipRoute::PullResponse);
    versioned.num_push_recv = (route == GossipRoute::PushMessage) ? 1 : 0;

    table_[label] = versioned;
    update_indices(label, versioned);

    stats_.num_inserts++;
    return Result<bool>(true);

  } else {
    // Entry exists - check if we should update
    if (!value.overrides(it->second.value)) {
      // Value doesn't override existing - reject
      stats_.num_failures++;
      return Result<bool>(std::string("Value does not override existing"));
    }

    // Update the entry
    VersionedCrdsValue versioned(value, ordinal_counter_++, now);
    versioned.from_pull_response = (route == GossipRoute::PullResponse);
    versioned.num_push_recv = (route == GossipRoute::PushMessage)
                                  ? it->second.num_push_recv + 1
                                  : it->second.num_push_recv;

    // Remove old from indices, add new
    remove_from_indices(label, it->second);
    it->second = versioned;
    update_indices(label, versioned);

    stats_.num_updates++;
    return Result<bool>(true);
  }
}

bool Crds::upserts(const CrdsValue &value) const {
  std::lock_guard<std::mutex> lock(mutex_);

  CrdsValueLabel label = value.label();
  auto it = table_.find(label);

  if (it == table_.end()) {
    return true; // New entry would be inserted
  }

  return value.overrides(it->second.value);
}

const VersionedCrdsValue *Crds::get(const CrdsValueLabel &label) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = table_.find(label);
  if (it != table_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<VersionedCrdsValue>
Crds::get_records(const PublicKey &pubkey) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<VersionedCrdsValue> result;

  auto it = records_.find(pubkey);
  if (it != records_.end()) {
    for (const auto &label : it->second) {
      auto val_it = table_.find(label);
      if (val_it != table_.end()) {
        result.push_back(val_it->second);
      }
    }
  }

  return result;
}

std::vector<VersionedCrdsValue> Crds::get_entries_after(uint64_t ordinal,
                                                         size_t limit) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<VersionedCrdsValue> result;
  result.reserve(std::min(limit, table_.size()));

  for (const auto &[label, value] : table_) {
    if (value.ordinal > ordinal) {
      result.push_back(value);
      if (result.size() >= limit) {
        break;
      }
    }
  }

  // Sort by ordinal
  std::sort(result.begin(), result.end(),
            [](const VersionedCrdsValue &a, const VersionedCrdsValue &b) {
              return a.ordinal < b.ordinal;
            });

  return result;
}

std::vector<ContactInfo> Crds::get_contact_infos() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<ContactInfo> result;

  for (const auto &[label, value] : table_) {
    if (label.type == CrdsValueLabel::Type::ContactInfo) {
      if (std::holds_alternative<ContactInfo>(value.value.data())) {
        result.push_back(std::get<ContactInfo>(value.value.data()));
      }
    }
  }

  return result;
}

const ContactInfo *Crds::get_contact_info(const PublicKey &pubkey) const {
  std::lock_guard<std::mutex> lock(mutex_);

  CrdsValueLabel label(CrdsValueLabel::Type::ContactInfo, pubkey);
  auto it = table_.find(label);

  if (it != table_.end() &&
      std::holds_alternative<ContactInfo>(it->second.value.data())) {
    return &std::get<ContactInfo>(it->second.value.data());
  }

  return nullptr;
}

std::vector<Vote> Crds::get_votes(const PublicKey &pubkey) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<Vote> result;

  // Get all records for this pubkey
  auto it = records_.find(pubkey);
  if (it != records_.end()) {
    for (const auto &label : it->second) {
      if (label.type == CrdsValueLabel::Type::Vote) {
        auto val_it = table_.find(label);
        if (val_it != table_.end() &&
            std::holds_alternative<Vote>(val_it->second.value.data())) {
          result.push_back(std::get<Vote>(val_it->second.value.data()));
        }
      }
    }
  }

  return result;
}

size_t Crds::trim(uint64_t now, uint64_t timeout) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<CrdsValueLabel> to_remove;

  // Find entries older than timeout
  for (const auto &[label, value] : table_) {
    if (now > value.local_timestamp &&
        (now - value.local_timestamp) > timeout) {
      to_remove.push_back(label);
    }
  }

  // Remove them
  for (const auto &label : to_remove) {
    auto it = table_.find(label);
    if (it != table_.end()) {
      remove_from_indices(label, it->second);
      table_.erase(it);
    }
  }

  stats_.num_trims += to_remove.size();
  return to_remove.size();
}

size_t Crds::len() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return table_.size();
}

size_t Crds::num_nodes() const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (const auto &[label, value] : table_) {
    if (label.type == CrdsValueLabel::Type::ContactInfo) {
      count++;
    }
  }
  return count;
}

size_t Crds::num_votes() const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (const auto &[label, value] : table_) {
    if (label.type == CrdsValueLabel::Type::Vote) {
      count++;
    }
  }
  return count;
}

bool Crds::contains(const CrdsValueLabel &label) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return table_.find(label) != table_.end();
}

std::vector<CrdsValueLabel> Crds::get_labels() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<CrdsValueLabel> result;
  result.reserve(table_.size());

  for (const auto &[label, value] : table_) {
    result.push_back(label);
  }

  return result;
}

void Crds::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  table_.clear();
  records_.clear();
  ordinal_counter_ = 0;
}

bool Crds::overrides(const CrdsValue &value,
                     const VersionedCrdsValue &existing) const {
  return value.overrides(existing.value);
}

void Crds::update_indices(const CrdsValueLabel &label,
                           const VersionedCrdsValue &value) {
  PublicKey pk = value.value.pubkey();
  records_[pk].insert(label);
}

void Crds::remove_from_indices(const CrdsValueLabel &label,
                                const VersionedCrdsValue &value) {
  PublicKey pk = value.value.pubkey();
  auto it = records_.find(pk);
  if (it != records_.end()) {
    it->second.erase(label);
    if (it->second.empty()) {
      records_.erase(it);
    }
  }
}

} // namespace gossip
} // namespace network
} // namespace slonana
