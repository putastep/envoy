#pragma once

#include "common.h"
#include "source/common/common/logger.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheEntryHandle : public Logger::Loggable<Logger::Id::filter> {
public:
  // --- Leader ---
  void appendChunk(const Buffer::Instance& chunk, bool end_stream);
  void setHeaders(const Http::ResponseHeaderMap& headers, bool end_stream);

  InFlightStatus joinOrStartRequest(CacheCallback callback);

private:
  CacheEntry entry_;
  mutable std::mutex mutex_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy