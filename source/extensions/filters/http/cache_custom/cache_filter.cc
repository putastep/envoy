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

  // Get a handle and start request
  handle_ = manager_->getHandle(host_, key_);

  // Join a request
  status_ = handle_->joinOrStartRequest(
      [weak_self = weak_from_this()](const CacheNotification& notification) {
        auto self = weak_self.lock();
        if (!self || !self->decoder_callbacks_) {
          return;
        }

        self->decoder_callbacks_->dispatcher().post([weak_self, notification]() {
          auto self = weak_self.lock();
          if (!self || !self->decoder_callbacks_) {
            return;
          }

          switch (notification.type) {
          case CacheEvent::Headers: {
            ENVOY_LOG(debug, "{} sending revieved HEADERS.", static_cast<void*>(self.get()));
            self->sendShaderHeaders(notification.headers, notification.end_stream);
            break;
          }
          case CacheEvent::Body: {
            ENVOY_LOG(debug, "{} sending recieved BODY.", static_cast<void*>(self.get()));
            self->sendSharedBody(notification.body, notification.end_stream);
            break;
          }
          }
        });
      });

  ENVOY_LOG(debug, "{} is now {} for key {} {}.", static_cast<void*>(this),
            static_cast<int>(status_), host_, key_);

  // Let request go through if leaders
  if (status_ == InFlightStatus::Leading) {
    return Http::FilterHeadersStatus::Continue;
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
  if (status_ == InFlightStatus::Leading) {
    handle_->setHeaders(headers, end_stream);
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus CacheCustomFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  // Publish data chunk if leader
  if (status_ == InFlightStatus::Leading) {
    handle_->appendChunk(data, end_stream);
  }

  return Http::FilterDataStatus::Continue;
}

// --------
// CACHED PATH
// --------

void CacheCustomFilter::sendShaderHeaders(SharedHeaders headers, bool end_stream) {
  auto headers_copy = Http::ResponseHeaderMapImpl::create();

  for (const auto& [name, value] : *headers) {
    headers_copy->addCopy(Http::LowerCaseString(name), value);
  }
  decoder_callbacks_->encodeHeaders(std::move(headers_copy), end_stream, "cache_hit");
  read_.headers = true;
}

void CacheCustomFilter::sendSharedBody(SharedBuffer body, bool end_stream) {
  Buffer::OwnedImpl buffer;

  auto* fragment = new CacheBodyFragment(body);
  buffer.addBufferFragment(*fragment);
  const size_t len = buffer.length();
  decoder_callbacks_->encodeData(buffer, end_stream);
  read_.index += len;
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

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy