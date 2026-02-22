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

  RequestStatus joinOrStartInFlight(const Hostname& host, const RequestKey& key,
                                    FilterWeakPtr filter);

private:
  const uint32_t max_entries_per_host_;

  struct HostRegistry {
    struct RingBuffer {
      std::unordered_map<RequestKey, CacheHandleSharedPtr> records;
      std::deque<RequestKey> eviction_order;
    };

    std::unordered_map<Hostname, RingBuffer> hosts;
  };

  HostRegistry registry_;

  // Total number of mutexes shared between hosts
  static constexpr size_t NumShards = 64;
  std::array<std::mutex, NumShards> mutexes_;

  std::mutex& getMutexForHost(const Hostname& host) {
    return mutexes_[std::hash<std::string>{}(host) % NumShards];
  }
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy