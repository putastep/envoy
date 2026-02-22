#pragma once

#include "common.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheEntryHandle : public Logger::Loggable<Logger::Id::filter> {
public:
  friend class CacheManager;

  CacheEntryHandle(std::mutex& mutex) : mutex_(mutex) {}

  // --- Leader ---
  void appendChunk(const Buffer::Instance& chunk, bool end_stream);
  void setHeaders(const Http::ResponseHeaderMap& headers, bool end_stream);

  // --- State ---
  size_t totalBytes() const;
  bool isComplete() const;

private:
  const DataView data();

  const Envoy::Buffer::InstancePtr& body();
  const Http::ResponseHeaderMap& headers();

  CacheEntry entry_;
  std::mutex& mutex_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy