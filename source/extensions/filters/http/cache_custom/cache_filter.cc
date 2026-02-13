#include "cache_filter.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomFilter::CacheCustomFilter(CacheConfigSharedPtr config,
                                     CacheManagerSharedPtr cache_manager)
    : config_(std::move(config)), cache_manager_(std::move(cache_manager)) {}

Http::FilterHeadersStatus CacheCustomFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (headers.getMethodValue() != "GET") {
    return Http::FilterHeadersStatus::Continue;
  }

  host_ = extractHost(headers);
  cache_key_ = generateCacheKey(headers);

  // 1. Cache Hit
  auto cached_entry = cache_manager_->get(host_, cache_key_);
  if (cached_entry.has_value()) {
    cached_response_headers_ =
        Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*cached_entry->headers);
    cached_response_body_.add(cached_entry->response_body);
    has_cached_response_ = true;

    return Http::FilterHeadersStatus::StopIteration;
  }

  // 2. Request Coalescing
  if (cache_manager_->isInFlight(host_, cache_key_)) {
    is_follower_ = true;

    cache_manager_->registerFollower(host_, cache_key_, this);

    ENVOY_LOG(debug, "Request coalescing: becoming follower for key: {}", cache_key_);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // 3. Cache Miss - Become the leader
  is_leader_ = true;
  should_cache_ = true;
  cache_manager_->registerLeader(host_, cache_key_, this);

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus CacheCustomFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                           bool end_stream) {
  if (!is_leader_) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Store headers for caching
  response_headers_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(headers);
  status_code_ = Http::Utility::getResponseStatus(headers);

  // Broadcast to followers
  cache_manager_->broadcastHeaders(host_, cache_key_, headers, end_stream);

  if (end_stream) {
    cacheResponse();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_leader_) {
    return Http::FilterDataStatus::Continue;
  }

  // Accumulate for caching
  response_body_.add(data);

  // Broadcast to followers
  cache_manager_->broadcastData(host_, cache_key_, data, end_stream);

  if (end_stream) {
    cacheResponse();
  }

  return Http::FilterDataStatus::Continue;
}

std::string CacheCustomFilter::generateCacheKey(const Http::RequestHeaderMap& headers) {
  return std::string(headers.getPathValue());
}

std::string CacheCustomFilter::extractHost(const Http::RequestHeaderMap& headers) {
  return std::string(headers.getHostValue());
}

void CacheCustomFilter::receiveBroadcastHeaders(Http::ResponseHeaderMapPtr headers,
                                                bool end_stream) {
  decoder_callbacks_->dispatcher().post([this, headers = std::move(headers), end_stream]() mutable {
    if (!decode_complete_) {

      BroadcastMessage msg;
      msg.type = BroadcastMessage::Type::Headers;
      msg.headers = std::move(headers); // Move the headers into the message
      msg.end_stream = end_stream;

      pending_broadcasts_.push(std::move(msg));
    } else {
      // Decoding is done, can send now
      decoder_callbacks_->encodeHeaders(std::move(headers), end_stream, "cache_hit");
    }
  });
}

void CacheCustomFilter::receiveBroadcastData(std::shared_ptr<Buffer::Instance> data,
                                             bool end_stream) {
  decoder_callbacks_->dispatcher().post([this, data, end_stream]() mutable {
    if (!decode_complete_) {

      // Create the message
      BroadcastMessage msg;
      msg.type = BroadcastMessage::Type::Data;
      msg.data = data; // Store the shared_ptr
      msg.end_stream = end_stream;

      // Queue it
      pending_broadcasts_.push(std::move(msg));
    } else {
      // Decoding is done, can send now
      decoder_callbacks_->encodeData(*data, end_stream);
    }
  });
}

void CacheCustomFilter::decodeComplete() {
  decode_complete_ = true;

  if (has_cached_response_) {
    decoder_callbacks_->encodeHeaders(std::move(cached_response_headers_), false, "cache_hit");
    decoder_callbacks_->encodeData(cached_response_body_, true);
    has_cached_response_ = false;
  }

  while (!pending_broadcasts_.empty()) {
    // Get reference to front message
    BroadcastMessage& msg = pending_broadcasts_.front();

    // Process based on type
    if (msg.type == BroadcastMessage::Type::Headers) {
      decoder_callbacks_->encodeHeaders(std::move(msg.headers), msg.end_stream,
                                        "cache_custom_coalesced");
    } else { // Type::Data
      decoder_callbacks_->encodeData(*msg.data, msg.end_stream);
    }

    // Remove from queue
    pending_broadcasts_.pop();
  }
}

void CacheCustomFilter::cacheResponse() {
  if (!should_cache_ || !response_headers_) {
    cache_manager_->notifyCompletion(host_, cache_key_);
    return;
  }

  CacheEntry entry;
  entry.headers = std::move(response_headers_);
  entry.response_body = response_body_.toString();
  entry.status_code = status_code_;

  cache_manager_->put(host_, cache_key_, std::move(entry));
  cache_manager_->notifyCompletion(host_, cache_key_);
}

void CacheCustomFilter::onDestroy() {
  if (is_follower_) {
    cache_manager_->unregisterFollower(host_, cache_key_, this);
  }
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy