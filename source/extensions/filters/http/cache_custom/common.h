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

struct CacheEntry {
  std::string response_body;
  uint64_t status_code;
  std::shared_ptr<Http::ResponseHeaderMap> headers;
};

struct InFlightRequestState {
  CacheCustomFilter* leader_filter;
  std::vector<CacheCustomFilter*> followers;
  uint32_t high_watermark_count = 0;
};

struct BroadcastMessage {
  enum class Type { Headers, Data };

  Type type;
  Http::ResponseHeaderMapPtr headers;     // For headers messages
  std::shared_ptr<Buffer::Instance> data; // For data messages
  bool end_stream;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy