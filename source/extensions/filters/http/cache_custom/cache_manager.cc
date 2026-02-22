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

RequestStatus CacheManager::joinOrStartInFlight(const Hostname& host, const RequestKey& key,
                                                FilterWeakPtr filter) {
  std::lock_guard<std::mutex> lock(getMutexForHost(host));
  auto& handle_ptr = registry_.hosts[host].records[key];

  if (max_entries_per_host_ < 0) {
    return {};
  }

  // If handler does not exists
  if (!handle_ptr) {
    ENVOY_LOG(debug, "{} is now LEADER.", static_cast<void*>(filter.lock().get()));
    handle_ptr = std::make_shared<CacheEntryHandle>(getMutexForHost(host));
    handle_ptr->entry_.coalescing.leader = filter;

    return {handle_ptr, InFlightStatus::Leading, std::nullopt};
  };

  // If request still in flight
  if (!handle_ptr->entry_.is_finished) {
    handle_ptr->entry_.coalescing.followers.push_back(filter);
  }
  ENVOY_LOG(debug, "{} is now FOLLOWER.", static_cast<void*>(filter.lock().get()));

  return {handle_ptr, InFlightStatus::Following, handle_ptr->data()};
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy