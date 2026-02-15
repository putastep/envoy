#pragma once

#include <string>
#include <vector>
#include <memory>
#include "envoy/http/header_map.h"
#include "envoy/http/filter.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheCustomFilter;

enum class EntryStatus { InFlight, Completed };

struct UnifiedCacheEntry {
  EntryStatus status = EntryStatus::InFlight;

  // Cached
  Http::ResponseHeaderMapPtr response_headers;
  std::vector<std::shared_ptr<Buffer::Instance>> chunks;

  // In flight
  bool is_complete = false;
  std::weak_ptr<CacheCustomFilter> leader_filter;
  std::vector<std::weak_ptr<CacheCustomFilter>> followers;
};

enum class RegistrationStatus { Leading, Following };

struct RegistrationResult {
  RegistrationStatus status;
  UnifiedCacheEntry* cache_entry;
};

struct ReadStatus {
  size_t last_read_chunk = 0;
  bool read_headers = false;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy