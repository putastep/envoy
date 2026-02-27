#pragma once

#include "common.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheCustomFilter : public std::enable_shared_from_this<CacheCustomFilter>,
                          public Http::PassThroughFilter,
                          public Logger::Loggable<Logger::Id::filter> {
public:
  CacheCustomFilter(CacheConfigSharedPtr config, CacheManagerSharedPtr manager);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

private:
  struct ReadStatus {
    size_t index = 0;
    bool headers = false;
  };

  Hostname extractHost(const Http::RequestHeaderMap& headers);
  RequestKey generateCacheKey(const Http::RequestHeaderMap& headers);
  void sendShaderHeaders(SharedHeaders headers, bool end_stream);
  void sendSharedBody(SharedBuffer body, bool end_stream);

  CacheConfigSharedPtr config_;
  CacheManagerSharedPtr manager_;

  Hostname host_;
  RequestKey key_;

  CacheHandleSharedPtr handle_;
  InFlightStatus status_;
  ReadStatus read_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy