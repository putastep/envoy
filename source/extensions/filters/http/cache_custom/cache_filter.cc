#include "source/extensions/filters/http/cache_custom/cache_filter.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomConfig::CacheCustomConfig(
    const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config)
    : max_entries_per_host_(config.max_entries_per_host()),
      max_entry_size_(config.max_entry_size_bytes()) {}

RingBufferCache::RingBufferCache(uint32_t max_entries, uint32_t max_entry_size)
    : max_entries_per_host_(max_entries), max_entry_size_(max_entry_size) {}

absl::optional<CacheEntry> RingBufferCache::get(const std::string& host, const std::string& key) {
  auto host_cache = host_caches_.find(host);
  if (host_cache == host_caches_.end()) {
    return absl::nullopt;
  }

  auto it = host_cache->second.cache.find(key);
  if (it == host_cache->second.cache.end()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "Cache hit for key: {}", key);
  return {it->second};
}

bool RingBufferCache::isInFlight(const std::string& host, const std::string& key) {
  auto host_cache = host_caches_.find(host);
  if (host_cache == host_caches_.end()) {
    return false;
  }

  return host_cache->second.in_flight_requests.find(key) !=
         host_cache->second.in_flight_requests.end();
}

void RingBufferCache::markInFlight(const std::string& host, const std::string& key) {
  host_caches_[host].in_flight_requests.insert(key);

  ENVOY_LOG(debug, "Marked request as in-flight: {}", key);
}

void RingBufferCache::notifyCompletion(const std::string& host, const std::string& key) {
  auto host_cache = host_caches_.find(host);
  if (host_cache == host_caches_.end()) {
    return;
  }

  host_cache->second.in_flight_requests.erase(key);

  // Notify all waiting requests for this key
  auto it = host_cache->second.waiting_requests.find(key);
  if (it != host_cache->second.waiting_requests.end()) {
    ENVOY_LOG(debug, "Notifying {} waiting requests for key: {}", it->second.size(), key);
    for (auto& callback : it->second) {
      callback();
    }
    host_cache->second.waiting_requests.erase(it);
  }
}

void RingBufferCache::addWaitingRequest(const std::string& host, const std::string& key,
                                        std::function<void()> callback) {
  host_caches_[host].waiting_requests[key].push_back(std::move(callback));
  ENVOY_LOG(debug, "Added waiting request for key: {}", key);
}

void RingBufferCache::put(const std::string& host, const std::string& key, CacheEntry entry) {
  // Check if response size exceeds limit
  if (entry.response_body.size() > max_entry_size_) {
    ENVOY_LOG(debug, "Response too large to cache: {} bytes", entry.response_body.size());
    return;
  }

  // If cache is full, remove oldest entry
  if (host_caches_[host].cache.size() >= max_entries_per_host_ &&
      host_caches_[host].cache.find(key) == host_caches_[host].cache.end()) {
    if (!host_caches_[host].ring_buffer.empty()) {
      std::string oldest_key = host_caches_[host].ring_buffer.front();
      host_caches_[host].ring_buffer.pop_front();
      host_caches_[host].cache.erase(oldest_key);
      ENVOY_LOG(debug, "Evicted oldest entry: {}", oldest_key);
    }
  }

  // Add/update entry
  host_caches_[host].cache[key] = std::move(entry);
  host_caches_[host].ring_buffer.push_back(key);
  ENVOY_LOG(debug, "Cached response for key: {}", key);
}

CacheCustomFilter::CacheCustomFilter(CacheCustomConfigSharedPtr config,
                                     RingBufferCacheSharedPtr cache)
    : config_(std::move(config)), cache_(std::move(cache)) {}

Http::FilterHeadersStatus CacheCustomFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Only cache GET requests
  if (headers.getMethodValue() != "GET") {
    return Http::FilterHeadersStatus::Continue;
  }

  host_ = extractHost(headers);
  cache_key_ = generateCacheKey(headers);

  // Try to get from cache
  auto cached_entry = cache_->get(host_, cache_key_);
  if (cached_entry.has_value()) {
    ENVOY_LOG(debug, "Serving response from cache for: {}", cache_key_);
    sendCachedResponse(cached_entry.value());
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Check if request is already in-flight (request coalescing)
  if (cache_->isInFlight(host_, cache_key_)) {
    ENVOY_LOG(debug, "Request coalescing: waiting for in-flight request: {}", cache_key_);
    is_coalesced_ = true;

    // Add this request to waiting queue
    cache_->addWaitingRequest(host_, cache_key_, [this]() {
      // When the in-flight request completes, serve from cache
      auto cached_entry = cache_->get(host_, cache_key_);
      if (cached_entry.has_value()) {
        ENVOY_LOG(debug, "Serving coalesced response from cache for: {}", cache_key_);
        sendCachedResponse(cached_entry.value());
      } else {
        ENVOY_LOG(warn, "Coalesced request completed but cache miss for: {}", cache_key_);
        // Let the request continue to backend as fallback
        decoder_callbacks_->continueDecoding();
      }
    });

    return Http::FilterHeadersStatus::StopIteration;
  }

  // Mark this request as in-flight for coalescing
  cache_->markInFlight(host_, cache_key_);
  should_cache_ = true;
  is_origin_request_ = true;

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus CacheCustomFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                           bool end_stream) {
  if (!should_cache_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Only cache successful responses
  const auto status = Http::Utility::getResponseStatus(headers);
  if (status != 200) {
    should_cache_ = false;
    // Notify waiting requests that the original request failed
    if (is_origin_request_) {
      cache_->notifyCompletion(host_, cache_key_);
    }
    return Http::FilterHeadersStatus::Continue;
  }

  status_code_ = static_cast<Http::Code>(status);
  response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);

  if (end_stream) {
    cacheResponse();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!should_cache_) {
    return Http::FilterDataStatus::Continue;
  }

  // Accumulate response body
  response_body_.append(data.toString());

  if (end_stream) {
    cacheResponse();
  }

  return Http::FilterDataStatus::Continue;
}

void CacheCustomFilter::cacheResponse() {
  CacheEntry entry;
  entry.response_body = response_body_;
  entry.status_code = status_code_;
  entry.headers = std::move(response_headers_);
  cache_->put(host_, cache_key_, std::move(entry));
  should_cache_ = false;

  // Notify waiting coalesced requests
  if (is_origin_request_) {
    cache_->notifyCompletion(host_, cache_key_);
  }
}

// Cache key: path
std::string CacheCustomFilter::generateCacheKey(const Http::RequestHeaderMap& headers) {
  std::string key;

  key.append(std::string(headers.getPathValue()));

  return key;
}

std::string CacheCustomFilter::extractHost(const Http::RequestHeaderMap& headers) {
  std::string key;

  key.append(std::string(headers.getHostValue()));

  return key;
}

// Send cached headers
void CacheCustomFilter::sendCachedResponse(const CacheEntry& entry) {
  decoder_callbacks_->encodeHeaders(
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*entry.headers),
      entry.response_body.empty(), "cache_custom");

  // Send cached body if present
  if (!entry.response_body.empty()) {
    Buffer::OwnedImpl buffer(entry.response_body);
    decoder_callbacks_->encodeData(buffer, true);
  }
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy