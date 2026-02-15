#include "cache_manager.h"
#include "cache_filter.h"
#include "common.h"
#include <memory>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheManager::CacheManager(uint32_t max_entries_per_host, uint32_t max_entry_size)
    : max_entries_per_host_(max_entries_per_host), max_entry_size_(max_entry_size) {}

RegistrationResult CacheManager::joinOrStartInFlight(const std::string& host,
                                                     const std::string& key,
                                                     std::weak_ptr<CacheCustomFilter> filter) {
  Thread::LockGuard lock(mutex_);
  auto entry = &host_caches_[host].entries[key];

  // Register a new leader if no leader
  if (!entry->is_complete && !entry->leader_filter.lock()) {
    entry->leader_filter = filter;

    ENVOY_LOG(debug, "{} is now LEADER for {} {}.", static_cast<void*>(filter.lock().get()), host,
              key);
    return {RegistrationStatus::Leading, entry};
  }

  // Register a new follower
  ENVOY_LOG(debug, "{} is now FOLLOWER for {} {}.", static_cast<void*>(filter.lock().get()), host,
            key);
  entry->followers.push_back(filter);
  return {RegistrationStatus::Following, entry};
}

void CacheManager::updateEntryAndNotify(const std::string& host, const std::string& key,
                                        std::function<void(UnifiedCacheEntry&)> update_logic,
                                        bool end_stream) {
  std::vector<std::weak_ptr<CacheCustomFilter>> followers_to_notify;
  {
    Thread::LockGuard lock(mutex_);
    auto& entry = host_caches_[host].entries[key];

    update_logic(entry);

    entry.is_complete = end_stream;
    followers_to_notify = entry.followers;
  }

  for (const auto& weak_follower : followers_to_notify) {
    if (auto follower = weak_follower.lock()) {
      ENVOY_LOG(debug, "Notifying {} about entry CHANGE.", static_cast<void*>(follower.get()));
      follower->onEntryUpdated();
    }
  }
}

void CacheManager::publishHeaders(const std::string& host, const std::string& key,
                                  Http::ResponseHeaderMap& headers, bool end_stream) {
  auto headers_copy = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);

  updateEntryAndNotify(
      host, key,
      [&](UnifiedCacheEntry& entry) { entry.response_headers = std::move(headers_copy); },
      end_stream);
}

void CacheManager::publishDataChunk(const std::string& host, const std::string& key,
                                    Buffer::Instance& data, bool end_stream) {
  auto chunk = std::make_shared<Buffer::OwnedImpl>(data);

  updateEntryAndNotify(
      host, key, [&](UnifiedCacheEntry& entry) { entry.chunks.push_back(std::move(chunk)); },
      end_stream);
}

Http::ResponseHeaderMapPtr CacheManager::getHeaders(const std::string& host, const std::string& key,
                                                    bool& is_complete) {
  Thread::LockGuard lock(mutex_);

  auto it = host_caches_[host].entries.find(key);
  if (it == host_caches_[host].entries.end()) {
    is_complete = true;
    return Http::ResponseHeaderMapImpl::create();
  }

  const auto& entry = it->second;
  is_complete = entry.is_complete;

  // Return headers
  return Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*entry.response_headers);
}

std::vector<std::shared_ptr<Buffer::Instance>> CacheManager::getNewChunks(const std::string& host,
                                                                          const std::string& key,
                                                                          size_t last_read_chunk,
                                                                          bool& is_complete) {
  Thread::LockGuard lock(mutex_);

  std::vector<std::shared_ptr<Buffer::Instance>> new_chunks;
  auto it = host_caches_[host].entries.find(key);

  if (it == host_caches_[host].entries.end()) {
    is_complete = true;
    return new_chunks;
  }

  const auto& entry = it->second;
  is_complete = entry.is_complete;

  // Return unread chunks
  if (last_read_chunk < entry.chunks.size()) {
    for (size_t i = last_read_chunk; i < entry.chunks.size(); i++) {
      new_chunks.push_back(entry.chunks[i]);
    }
  }

  return new_chunks;
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy