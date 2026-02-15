#pragma once

#include "common.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "cache_config.h"
#include "cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheCustomFilter : public std::enable_shared_from_this<CacheCustomFilter>,
                          public Http::PassThroughFilter,
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

  void onEntryUpdated();

private:
  std::string extractHost(const Http::RequestHeaderMap& headers);
  std::string generateCacheKey(const Http::RequestHeaderMap& headers);
  void sendCachedHeaders();

  CacheConfigSharedPtr config_;
  CacheManagerSharedPtr cache_manager_;

  std::string host_;
  std::string cache_key_;

  RegistrationResult registration_result_;
  ReadStatus read_status_;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy