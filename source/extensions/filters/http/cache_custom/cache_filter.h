#pragma once

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "envoy/extensions/filters/http/cache_custom/v3/cache.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

/**
 * Configuration for the cache custom filter.
 */
class CacheCustomConfig {
public:
  CacheCustomConfig(
      const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config);

  uint32_t maxEntries() const { return max_entries_; }
  uint32_t ttlSeconds() const { return ttl_seconds_; }
  uint32_t maxResponseSizeBytes() const { return max_response_size_bytes_; }

private:
  const uint32_t max_entries_;
  const uint32_t ttl_seconds_;
  const uint32_t max_response_size_bytes_;
};

using CacheCustomConfigSharedPtr = std::shared_ptr<CacheCustomConfig>;

/**
 * Cache entry structure.
 */
struct CacheEntry {
  std::string response_body;
  Http::Code status_code;
  Http::ResponseHeaderMapPtr headers;
  std::chrono::steady_clock::time_point expiry_time;
};

/**
 * Ring buffer cache implementation.
 */
class RingBufferCache : public Logger::Loggable<Logger::Id::filter> {
public:
  RingBufferCache(uint32_t max_entries, uint32_t ttl_seconds, uint32_t max_response_size);

  // Get cached response for a given key
  absl::optional<CacheEntry> get(const std::string& key);

  // Put a response into the cache
  void put(const std::string& key, CacheEntry entry);

private:
  const uint32_t max_entries_;
  const uint32_t ttl_seconds_;
  const uint32_t max_response_size_;
  
  std::unordered_map<std::string, CacheEntry> cache_;
  std::deque<std::string> ring_buffer_;
};

using RingBufferCacheSharedPtr = std::shared_ptr<RingBufferCache>;

/**
 * HTTP filter for ring buffer cache.
 */
class CacheCustomFilter : public Http::PassThroughFilter,
                          public Logger::Loggable<Logger::Id::filter> {
public:
  CacheCustomFilter(CacheCustomConfigSharedPtr config, RingBufferCacheSharedPtr cache);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

private:
  std::string generateCacheKey(const Http::RequestHeaderMap& headers);
  void sendCachedResponse(const CacheEntry& entry);

  CacheCustomConfigSharedPtr config_;
  RingBufferCacheSharedPtr cache_;
  
  std::string cache_key_;
  std::string response_body_;
  Http::Code status_code_{Http::Code::OK};
  Http::ResponseHeaderMapPtr response_headers_;
  bool should_cache_{false};
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy