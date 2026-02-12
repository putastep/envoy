#include "source/extensions/filters/http/cache_custom/cache_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

CacheCustomConfig::CacheCustomConfig(
    const envoy::extensions::filters::http::cache_custom::v3::CacheCustom& config)
    : max_entries_per_host_(config.max_entries_per_host()),
      max_entry_size_(config.max_entry_size_bytes()) {}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy