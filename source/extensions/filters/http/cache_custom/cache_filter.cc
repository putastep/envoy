#include "cache_filter.h"

#include "common.h"
#include <cstddef>
#include <utility>
#include "cache_manager.h"
#include "cache_entry_handle.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomFilter::CacheCustomFilter(CacheConfigSharedPtr config, CacheManagerSharedPtr manager)
    : config_(std::move(config)), manager_(std::move(manager)) {}

// --------
// DECODE PATH
// --------

Http::FilterHeadersStatus CacheCustomFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  if (headers.getMethodValue() != "GET") {
    return Http::FilterHeadersStatus::Continue;
  }

  host_ = extractHost(headers);
  key_ = generateCacheKey(headers);

  // Get registration result from cache manager
  request_ = manager_->joinOrStartInFlight(host_, key_, weak_from_this());

  // Let request go through if leaders
  if (request_.status == InFlightStatus::Leading) {
    return Http::FilterHeadersStatus::Continue;
  }

  // Send cached data
  if (request_.cached_data && request_.cached_data->headers) {
    sendCachedData();
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
  if (request_.status == InFlightStatus::Leading) {
    request_.handle->setHeaders(headers, end_stream);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  // Publish data chunk if leader
  if (request_.status == InFlightStatus::Leading) {
    request_.handle->appendChunk(data, end_stream);
  }

  return Http::FilterDataStatus::Continue;
}

void CacheCustomFilter::recieveHeaders(std::shared_ptr<const Http::ResponseHeaderMap> headers,
                                       bool end_stream) {
  std::weak_ptr<CacheCustomFilter> weak_self = weak_from_this();
  encoder_callbacks_->dispatcher().post([weak_self, headers, end_stream]() {
    auto self = weak_self.lock();

    if (!self) {
      return;
    }

    ENVOY_LOG(debug, "{} sending recieved HEADERS.", static_cast<void*>(self.get()));

    auto headers_copy = Http::ResponseHeaderMapImpl::create();
    Http::HeaderMapImpl::copyFrom(*headers_copy, *headers);

    self->decoder_callbacks_->encodeHeaders(std::move(headers_copy), end_stream, "cache_hit");
    self->read_.headers = true;
  });
}

void CacheCustomFilter::recieveBody(std::shared_ptr<const Envoy::Buffer::Instance> body,
                                    bool end_stream) {
  std::weak_ptr<CacheCustomFilter> weak_self = weak_from_this();
  encoder_callbacks_->dispatcher().post([weak_self, body, end_stream]() {
    auto self = weak_self.lock();

    if (!self) {
      return;
    }

    ENVOY_LOG(debug, "{} sending recieved BODY.", static_cast<void*>(self.get()));

    Buffer::OwnedImpl body_copy;
    body_copy.add(*body);

    self->encoder_callbacks_->injectEncodedDataToFilterChain(body_copy, end_stream);
    self->read_.index += body_copy.length();
  });
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
  ENVOY_LOG(debug, "{} sending cached HEADERS.", static_cast<void*>(this));
  auto headers = Http::ResponseHeaderMapImpl::create();
  Http::HeaderMapImpl::copyFrom(*headers, *request_.cached_data->headers);

  decoder_callbacks_->encodeHeaders(std::move(headers), false, "cache_hit");
  read_.headers = true;
}

void CacheCustomFilter::sendCachedBody() {
  ENVOY_LOG(debug, "{} sending cached BODY.", static_cast<void*>(this));
  Buffer::OwnedImpl body;
  body.add(*request_.cached_data->body);

  encoder_callbacks_->injectEncodedDataToFilterChain(body, true);
  read_.index += body.length();
}

void CacheCustomFilter::sendCachedData() {
  ENVOY_LOG(debug, "{} sending cached DATA.", static_cast<void*>(this));
  std::weak_ptr<CacheCustomFilter> weak_self = weak_from_this();

  encoder_callbacks_->dispatcher().post([weak_self]() {
    auto self = weak_self.lock();
    auto handle = self->request_.handle;
    if (!self || !handle) {
      return;
    }

    // Send cached headers
    self->sendCachedHeaders();

    // Send cached body
    self->sendCachedBody();

    // Send ending data
    if (handle->isComplete() && self->read_.index == handle->totalBytes()) {
      Buffer::OwnedImpl empty_buffer;

      ENVOY_LOG(debug, "{} sending ENDING.", static_cast<void*>(self.get()));
      self->encoder_callbacks_->injectEncodedDataToFilterChain(empty_buffer, true);
      // self->encoder_callbacks_->continueEncoding();
    }
  });
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy