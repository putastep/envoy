#pragma once

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/common/http/header_map_impl.h"

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
  CacheCustomConfig(const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config);

  uint32_t maxEntriesPerHost() const { return max_entries_per_host_; }
  uint32_t maxEntrySizeBytes() const { return max_entry_size_; }

private:
  const uint32_t max_entries_per_host_;
  const uint32_t max_entry_size_;
};

using CacheCustomConfigSharedPtr = std::shared_ptr<CacheCustomConfig>;

/**
 * Cache entry structure.
 */
struct CacheEntry {
  std::string response_body;
  Http::Code status_code;
  std::shared_ptr<Http::ResponseHeaderMap> headers;
  std::chrono::steady_clock::time_point expiry_time;
};

/**
 * Ring buffer cache implementation with request coalescing support.
 */
class RingBufferCache : public Logger::Loggable<Logger::Id::filter> {
public:
  RingBufferCache(uint32_t max_entries_per_host, uint32_t max_entry_size);

  // Get cached response for a given key
  absl::optional<CacheEntry> get(const std::string& host, const std::string& key);

  // Put a response into the cache
  void put(const std::string& host, const std::string& key, CacheEntry entry);

  // Request coalescing methods
  bool isInFlight(const std::string& host, const std::string& key);
  void markInFlight(const std::string& host, const std::string& key);
  void notifyCompletion(const std::string& host, const std::string& key);
  void addWaitingRequest(const std::string& host, const std::string& key,
                         std::function<void()> callback);

private:
  // Per-host cache
  struct HostCache {
    std::unordered_map<std::string, CacheEntry> cache;
    std::deque<std::string> ring_buffer;
    std::unordered_set<std::string> in_flight_requests;
    std::unordered_map<std::string, std::vector<std::function<void()>>> waiting_requests;
  };

  const uint32_t max_entries_per_host_;
  const uint32_t max_entry_size_;

  // Map from host to its cache state
  std::unordered_map<std::string, HostCache> host_caches_;
};

using RingBufferCacheSharedPtr = std::shared_ptr<RingBufferCache>;

/**
 * HTTP filter for ring buffer cache with request coalescing.
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

  // Http::StreamFilterBase
  void onDestroy() override {
    // Clean up in-flight tracking
    if (is_origin_request_ && should_cache_) {
      cache_->notifyCompletion(host_, cache_key_);
    }
  }

private:
  std::string extractHost(const Http::RequestHeaderMap& headers);
  std::string generateCacheKey(const Http::RequestHeaderMap& headers);
  void sendCachedResponse(const CacheEntry& entry);
  void cacheResponse();

  CacheCustomConfigSharedPtr config_;
  RingBufferCacheSharedPtr cache_;

  std::string host_;
  std::string cache_key_;
  std::string response_body_;
  Http::Code status_code_{Http::Code::OK};
  Http::ResponseHeaderMapPtr response_headers_;
  bool should_cache_{false};
  bool is_origin_request_{false}; // True if this is the first request (not coalesced)
  bool is_coalesced_{false};      // True if this request is waiting for another
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy