#include "source/extensions/filters/http/cache_custom/cache_filter.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomConfig::CacheCustomConfig(
    const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config)
    : max_entries_(config.max_entries()),
      max_response_size_bytes_(config.max_response_size_bytes()) {}

RingBufferCache::RingBufferCache(uint32_t max_entries,
                                 uint32_t max_response_size)
    : max_entries_(max_entries), max_response_size_(max_response_size) {}

absl::optional<CacheEntry> RingBufferCache::get(const std::string& key) {
  auto it = cache_.find(key);
  if (it == cache_.end()) {
    return absl::nullopt;
  }

  ENVOY_LOG(debug, "Cache hit for key: {}", key);
  return {it->second};
}

void RingBufferCache::put(const std::string& key, CacheEntry entry) {
  // Check if response size exceeds limit
  if (entry.response_body.size() > max_response_size_) {
    ENVOY_LOG(debug, "Response too large to cache: {} bytes", entry.response_body.size());
    return;
  }

  // If cache is full, remove oldest entry
  if (cache_.size() >= max_entries_ && cache_.find(key) == cache_.end()) {
    if (!ring_buffer_.empty()) {
      std::string oldest_key = ring_buffer_.front();
      ring_buffer_.pop_front();
      cache_.erase(oldest_key);
      ENVOY_LOG(debug, "Evicted oldest entry: {}", oldest_key);
    }
  }

  // Add/update entry
  cache_[key] = std::move(entry);
  ring_buffer_.push_back(key);
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

  cache_key_ = generateCacheKey(headers);

  // Try to get from cache
  auto cached_entry = cache_->get(cache_key_);
  if (cached_entry.has_value()) {
    ENVOY_LOG(debug, "Serving response from cache for: {}", cache_key_);
    sendCachedResponse(cached_entry.value());
    return Http::FilterHeadersStatus::StopIteration;
  }

  should_cache_ = true;
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
    return Http::FilterHeadersStatus::Continue;
  }

  status_code_ = static_cast<Http::Code>(status);
  response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);

  if (end_stream) {
    // Cache immediately if no body
    CacheEntry entry;
    entry.response_body = response_body_;
    entry.status_code = status_code_;
    entry.headers = std::move(response_headers_);
    cache_->put(cache_key_, std::move(entry));
    should_cache_ = false;
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
    // Cache the complete response
    CacheEntry entry;
    entry.response_body = response_body_;
    entry.status_code = status_code_;
    entry.headers = std::move(response_headers_);
    cache_->put(cache_key_, std::move(entry));
    should_cache_ = false;
  }

  return Http::FilterDataStatus::Continue;
}

std::string CacheCustomFilter::generateCacheKey(const Http::RequestHeaderMap& headers) {
  // Cache key: method + path + host
  std::string key;
  key.append(std::string(headers.getMethodValue()));
  key.append(":");
  key.append(std::string(headers.getPathValue()));
  if (headers.Host() != nullptr) {
    key.append(":");
    key.append(std::string(headers.getHostValue()));
  }
  return key;
}

void CacheCustomFilter::sendCachedResponse(const CacheEntry& entry) {
  // Send cached headers
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