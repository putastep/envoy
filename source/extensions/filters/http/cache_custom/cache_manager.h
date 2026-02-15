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

  absl::optional<UnifiedCacheEntry> get(const std::string& host, const std::string& key);
  void put(const std::string& host, const std::string& key, UnifiedCacheEntry entry);

  void unregisterFollower(const std::string& host, const std::string& key,
                          std::weak_ptr<CacheCustomFilter> follower);
  void notifyCompletion(const std::string& host, const std::string& key);

  RegistrationResult joinOrStartInFlight(const std::string& host, const std::string& key,
                                         std::weak_ptr<CacheCustomFilter> filter);
  void publishHeaders(const std::string& host, const std::string& key,
                      Http::ResponseHeaderMap& headers, bool end_stream);
  void publishDataChunk(const std::string& host, const std::string& key, Buffer::Instance& data,
                        bool end_stream);
  Http::ResponseHeaderMapPtr getHeaders(const std::string& host, const std::string& key,
                                        bool& is_complete);
  std::vector<std::shared_ptr<Buffer::Instance>> getNewChunks(const std::string& host,
                                                              const std::string& key,
                                                              size_t last_read_chunk,
                                                              bool& is_complete);

private:
  void updateEntryAndNotify(const std::string& host, const std::string& key,
                            std::function<void(UnifiedCacheEntry&)> update_logic, bool end_stream);

  struct HostCacheState {
    std::unordered_map<std::string, UnifiedCacheEntry> entries;
    std::deque<std::string> eviction_queue;
  };

  const uint32_t max_entries_per_host_;
  const uint32_t max_entry_size_;
  std::unordered_map<std::string, HostCacheState> host_caches_;

  mutable Thread::MutexBasicLockable mutex_;
};

using CacheManagerSharedPtr = std::shared_ptr<CacheManager>;

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy