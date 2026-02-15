#include "cache_filter.h"

#include "common.h"
#include "source/common/http/utility.h"
#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomFilter::CacheCustomFilter(CacheConfigSharedPtr config,
                                     CacheManagerSharedPtr cache_manager)
    : config_(std::move(config)), cache_manager_(std::move(cache_manager)) {}

// --------
// DECODE PATH
// --------

Http::FilterHeadersStatus CacheCustomFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (headers.getMethodValue() != "GET") {
    return Http::FilterHeadersStatus::Continue;
  }

  host_ = extractHost(headers);
  cache_key_ = generateCacheKey(headers);

  // Get registration result from cache manager
  registration_result_ = cache_manager_->joinOrStartInFlight(host_, cache_key_, weak_from_this());

  // Let request go through if leaders
  if (registration_result_.status == RegistrationStatus::Leading) {
    return Http::FilterHeadersStatus::Continue;
  }

  // If headers are available send them to trigger encode path
  if (registration_result_.cache_entry->response_headers) {
    encoder_callbacks_->dispatcher().post([this]() { sendCachedHeaders(); });
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus CacheCustomFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

// --------
// ENCODE PATH
// --------

Http::FilterHeadersStatus CacheCustomFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                           bool end_stream) {
  // Publish headers if leader
  if (registration_result_.status == RegistrationStatus::Leading) {
    read_status_.read_headers = true;
    cache_manager_->publishHeaders(host_, cache_key_, headers, end_stream);
    return Http::FilterHeadersStatus::Continue;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus CacheCustomFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  // Publish data chunk if leader
  if (registration_result_.status == RegistrationStatus::Leading) {
    cache_manager_->publishDataChunk(host_, cache_key_, data, end_stream);
  }

  // Follower logic: Drain the cache into the current 'data' buffer
  bool finished = false;
  auto new_chunks =
      cache_manager_->getNewChunks(host_, cache_key_, read_status_.last_read_chunk, finished);

  for (auto& chunk : new_chunks) {
    read_status_.last_read_chunk++;
    data.move(*chunk);
  }

  if (finished) {
    return Http::FilterDataStatus::Continue;
  }

  return Http::FilterDataStatus::StopIterationAndBuffer;
}

// --------
// HELPER FUNCTIONS
// --------

std::string CacheCustomFilter::generateCacheKey(const Http::RequestHeaderMap& headers) {
  return std::string(headers.getPathValue());
}

std::string CacheCustomFilter::extractHost(const Http::RequestHeaderMap& headers) {
  return std::string(headers.getHostValue());
}

void CacheCustomFilter::sendCachedHeaders() {
  bool finished = false;

  auto cached_headers = cache_manager_->getHeaders(host_, cache_key_, finished);
  if (cached_headers) {
    read_status_.read_headers = true;
    decoder_callbacks_->encodeHeaders(std::move(cached_headers), finished, "cache_hit");
  }
}

void CacheCustomFilter::onEntryUpdated() {
  encoder_callbacks_->dispatcher().post([this]() {
    if (!read_status_.read_headers) {
      sendCachedHeaders();
    }

    // encoder_callbacks_->continueEncoding();
  });
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy