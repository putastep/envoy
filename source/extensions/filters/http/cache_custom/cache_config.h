#pragma once

#include "envoy/extensions/filters/http/cache_custom/v3/cache.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheCustomConfig {
public:
  CacheCustomConfig(const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config);

  uint32_t maxEntriesPerHost() const { return max_entries_per_host_; }

private:
  const uint32_t max_entries_per_host_;
  
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy