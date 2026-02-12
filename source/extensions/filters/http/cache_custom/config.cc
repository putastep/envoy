#include "source/extensions/filters/http/cache_custom/config.h"

#include "cache_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

Http::FilterFactoryCb CacheCustomFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& proto_config,
    const std::string&, Server::Configuration::FactoryContext&) {
  
  auto config = std::make_shared<CacheCustomConfig>(proto_config);
  auto cache_manager = std::make_shared<CacheManager>(
      proto_config.max_entries_per_host(),
      proto_config.max_entry_size_bytes());

  return [config, cache_manager](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<CacheCustomFilter>(config, cache_manager));
  };
}

REGISTER_FACTORY(CacheCustomFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy