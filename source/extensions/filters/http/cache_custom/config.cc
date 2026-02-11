#include "config.h"
#include "filter.h"
#include "filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RingBufferCache {

Http::FilterFactoryCb RingBufferCacheFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache_custom::v3::RingBufferCache& proto_config,
    const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  
  // Create shared filter configuration (one per filter chain)
  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      proto_config, context.scope());
  
  // Return lambda that creates filter instances
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    // Each request gets its own filter instance
    callbacks.addStreamFilter(std::make_shared<RingBufferCacheFilter>(config));
  };
}

Http::FilterFactoryCb 
RingBufferCacheFilterFactory::createFilterFactoryFromProtoWithServerContextTyped(
    const envoy::extensions::filters::http::cache_custom::v3::RingBufferCache& proto_config,
    const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  
  FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
      proto_config, context.scope());
  
  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<RingBufferCacheFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
RingBufferCacheFilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::cache_custom::v3::RingBufferCachePerRoute& proto_config,
    Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor& validator) {
  
  return std::make_shared<FilterConfigPerRoute>(proto_config);
}

/**
 * Static registration for the ring buffer cache filter.
 */
REGISTER_FACTORY(RingBufferCacheFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace RingBufferCache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy