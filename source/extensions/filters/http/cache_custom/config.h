#pragma once

#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

#include "envoy/extensions/filters/http/cache_custom/v3/cache.pb.h"
#include "envoy/extensions/filters/http/cache_custom/v3/cache.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

/**
 * Config registration for the cache custom filter.
 */
class CacheCustomFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::cache_custom::v3::CacheCustom> {
public:
  CacheCustomFilterFactory() : FactoryBase("envoy.filters.http.cache_custom") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& proto_config,
      const std::string& stats_prefix,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy