#include "cache_manager.h"
#include "cache_filter.h"
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheManager::CacheManager(uint32_t max_entries_per_host, uint32_t max_entry_size)
    : max_entries_per_host_(max_entries_per_host), max_entry_size_(max_entry_size) {}

absl::optional<CacheEntry> CacheManager::get(const std::string& host, const std::string& key) {
  Thread::LockGuard lock(mutex_);

  auto host_cache = host_caches_.find(host);
  if (host_cache == host_caches_.end()) {
    return absl::nullopt;
  }

  auto it = host_cache->second.entries.find(key);
  if (it == host_cache->second.entries.end()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "Cache hit for key: {}", key);
  return {it->second};
}

void CacheManager::put(const std::string& host, const std::string& key, CacheEntry entry) {
  // Check if response size exceeds limit
  if (entry.response_body.size() > max_entry_size_) {
    ENVOY_LOG(debug, "Response too large to cache: {} bytes", entry.response_body.size());
    return;
  }

  Thread::LockGuard lock(mutex_);

  auto& host_cache = host_caches_[host];

  // If cache is full and this is a new entry, evict oldest
  if (host_cache.entries.size() >= max_entries_per_host_ &&
      host_cache.entries.find(key) == host_cache.entries.end()) {
    if (!host_cache.eviction_queue.empty()) {
      std::string oldest_key = host_cache.eviction_queue.front();
      host_cache.eviction_queue.pop_front();
      host_cache.entries.erase(oldest_key);
      ENVOY_LOG(debug, "Evicted oldest entry: {}", oldest_key);
    }
  }

  // Add/update entry
  host_cache.entries[key] = std::move(entry);
  host_cache.eviction_queue.push_back(key);
  ENVOY_LOG(debug, "Cached response for key: {}", key);
}

bool CacheManager::isInFlight(const std::string& host, const std::string& key) {
  Thread::LockGuard lock(mutex_);

  auto host_cache = host_caches_.find(host);
  if (host_cache == host_caches_.end()) {
    return false;
  }

  return host_cache->second.in_flight_requests.find(key) !=
         host_cache->second.in_flight_requests.end();
}

void CacheManager::registerLeader(const std::string& host, const std::string& key,
                                  CacheCustomFilter* leader_filter) {
  Thread::LockGuard lock(mutex_);

  auto& state = host_caches_[host].in_flight_requests[key];
  state.leader_filter = leader_filter;
  state.high_watermark_count = 0;
  ENVOY_LOG(debug, "Registered leader for key: {}", key);
}

void CacheManager::registerFollower(const std::string& host, const std::string& key,
                                    CacheCustomFilter* follower_filter) {
  Thread::LockGuard lock(mutex_);

  auto& state = host_caches_[host].in_flight_requests[key];
  state.followers.push_back(follower_filter);

  ENVOY_LOG(debug, "Registered follower for key: {}", key);
}

void CacheManager::broadcastHeaders(const std::string& host, const std::string& key,
                                    Http::ResponseHeaderMap& headers, bool end_stream) {
  // Create a copy of the followers list while holding the lock
  std::vector<CacheCustomFilter*> followers_copy;
  {
    Thread::LockGuard lock(mutex_);

    auto host_it = host_caches_.find(host);
    if (host_it == host_caches_.end()) {
      return;
    }

    auto state_it = host_it->second.in_flight_requests.find(key);
    if (state_it == host_it->second.in_flight_requests.end()) {
      return;
    }

    followers_copy = state_it->second.followers;
  }

  // Broadcast to followers
  for (auto* follower : followers_copy) {
    // Create header map for each follower
    auto headers_copy = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);
    follower->receiveBroadcastHeaders(std::move(headers_copy), end_stream);
  }
}

void CacheManager::broadcastData(const std::string& host, const std::string& key,
                                 Buffer::Instance& data, bool end_stream) {
  // Create a copy of the followers list while holding the lock
  std::vector<CacheCustomFilter*> followers_copy;
  {
    Thread::LockGuard lock(mutex_);

    auto host_it = host_caches_.find(host);
    if (host_it == host_caches_.end()) {
      return;
    }

    auto state_it = host_it->second.in_flight_requests.find(key);
    if (state_it == host_it->second.in_flight_requests.end()) {
      return;
    }

    followers_copy = state_it->second.followers;
  }

  // Broadcast to followers without holding the lock
  for (auto* follower : followers_copy) {
    auto data_copy = std::make_shared<Buffer::OwnedImpl>(data);

    follower->receiveBroadcastData(data_copy, end_stream);
  }
}

void CacheManager::unregisterFollower(const std::string& host, const std::string& key,
                                      CacheCustomFilter* follower_filter) {
  Thread::LockGuard lock(mutex_);

  auto host_it = host_caches_.find(host);
  if (host_it == host_caches_.end()) {
    return;
  }

  auto state_it = host_it->second.in_flight_requests.find(key);
  if (state_it == host_it->second.in_flight_requests.end()) {
    return;
  }

  auto& followers = state_it->second.followers;
  auto it = std::find(followers.begin(), followers.end(), follower_filter);
  if (it != followers.end()) {
    followers.erase(it);
    ENVOY_LOG(debug, "Unregistered follower for key: {}", key);
  }
}

void CacheManager::notifyCompletion(const std::string& host, const std::string& key) {
  Thread::LockGuard lock(mutex_);

  auto host_it = host_caches_.find(host);
  if (host_it == host_caches_.end()) {
    return;
  }

  host_it->second.in_flight_requests.erase(key);
  ENVOY_LOG(debug, "Request completed for key: {}", key);
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy