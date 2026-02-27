#include "cache_manager.h"

#include "common.h"
#include "cache_entry_handle.h"
#include "cache_filter.h"
#include <memory>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheManager::CacheManager(uint32_t max_entries_per_host)
    : max_entries_per_host_(max_entries_per_host) {}

CacheHandleSharedPtr CacheManager::getHandle(const Hostname& host, const RequestKey& key) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto& host_buffer = registry_.hosts[host];
  auto& handle_ptr = host_buffer.records[key];

  if (!handle_ptr) {
    if (host_buffer.records.size() > max_entries_per_host_) {
      evictOldest(host_buffer);
    }

    handle_ptr = std::make_shared<CacheEntryHandle>();
    host_buffer.eviction_order.push_back(key);
  }

  return handle_ptr;
}

void CacheManager::evictOldest(HostRegistry::RingBuffer& buffer) {
  if (buffer.eviction_order.empty()) {
    return;
  }

  const RequestKey oldest_key = buffer.eviction_order.front();
  buffer.eviction_order.pop_front();

  buffer.records.erase(oldest_key);
  ENVOY_LOG(debug, "CACHE MANAGER: Evicted oldest key {}.", oldest_key);
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy