#pragma once

#include "common.h"
#include <deque>
#include <unordered_map>
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheManager : public Logger::Loggable<Logger::Id::filter> {
public:
  CacheManager(uint32_t max_entries_per_host);

  CacheHandleSharedPtr getHandle(const Hostname& host, const RequestKey& key);

private:
  const uint32_t max_entries_per_host_;

  struct HostRegistry {
    struct RingBuffer {
      std::unordered_map<RequestKey, CacheHandleSharedPtr> records;
      std::deque<RequestKey> eviction_order;
    };

    std::unordered_map<Hostname, RingBuffer> hosts;
  };

  void evictOldest(HostRegistry::RingBuffer& buffer);
  HostRegistry registry_;
  std::mutex mutex_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy