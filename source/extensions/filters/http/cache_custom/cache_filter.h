#pragma once

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "cache_config.h"
#include "cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheCustomFilter : public Http::PassThroughFilter,
                          public Logger::Loggable<Logger::Id::filter> {
public:
  CacheCustomFilter(CacheConfigSharedPtr config, CacheManagerSharedPtr cache_manager);

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;

  // Http::StreamFilterBase
  void onDestroy() override;
  void decodeComplete() override;

private:
  std::string extractHost(const Http::RequestHeaderMap& headers);
  std::string generateCacheKey(const Http::RequestHeaderMap& headers);
  void cacheResponse();

  CacheConfigSharedPtr config_;
  CacheManagerSharedPtr cache_manager_;

  std::string host_;
  std::string cache_key_;
  Buffer::OwnedImpl response_body_;
  uint64_t status_code_{200};
  Http::ResponseHeaderMapPtr response_headers_;
  bool should_cache_{false};
  bool is_leader_{false};
  bool is_follower_{false};

  // For sending cache
  bool has_cached_response_{false};
  Http::ResponseHeaderMapPtr cached_response_headers_;
  Buffer::OwnedImpl cached_response_body_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy