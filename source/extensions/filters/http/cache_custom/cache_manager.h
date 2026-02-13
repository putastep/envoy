#pragma once

#include <deque>
#include <unordered_map>
#include "source/common/common/logger.h"
#include "source/common/buffer/buffer_impl.h"
#include "common.h"

#include "envoy/thread/thread.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheManager : public Logger::Loggable<Logger::Id::filter> {
public:
  CacheManager(uint32_t max_entries_per_host, uint32_t max_entry_size);

  absl::optional<CacheEntry> get(const std::string& host, const std::string& key);
  void put(const std::string& host, const std::string& key, CacheEntry entry);

  bool isInFlight(const std::string& host, const std::string& key);
  void registerLeader(const std::string& host, const std::string& key,
                      CacheCustomFilter* leader_filter);
  void registerFollower(const std::string& host, const std::string& key,
                        Http::StreamDecoderFilterCallbacks* follower_callbacks);

  void broadcastData(const std::string& host, const std::string& key, Buffer::Instance& data,
                     bool end_stream);
  void broadcastHeaders(const std::string& host, const std::string& key,
                        Http::ResponseHeaderMap& headers, bool end_stream);

  void unregisterFollower(const std::string& host, const std::string& key,
                          Http::StreamDecoderFilterCallbacks* follower_callbacks);
  void notifyCompletion(const std::string& host, const std::string& key);
  void updateWatermark(const std::string& host, const std::string& key, bool high_watermark);

private:
  struct HostCacheState {
    std::unordered_map<std::string, InFlightRequestState> in_flight_requests;
    std::unordered_map<std::string, CacheEntry> entries;
    std::deque<std::string> eviction_queue;
  };

  const uint32_t max_entries_per_host_;
  const uint32_t max_entry_size_;
  std::unordered_map<std::string, HostCacheState> host_caches_;

  // Mutex to protect all shared state
  mutable Thread::MutexBasicLockable mutex_;
};

using CacheManagerSharedPtr = std::shared_ptr<CacheManager>;

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy