#pragma once

#include <memory>
#include <vector>
#include <string>

namespace Envoy {
namespace Buffer {
class Instance;
using InstancePtr = std::unique_ptr<Instance>;
} // namespace Buffer

namespace Http {
class ResponseHeaderMap;
class ResponseHeaderMapImpl;

using ResponseHeaderMapPtr = std::unique_ptr<ResponseHeaderMap>;
} // namespace Http

namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

class CacheEntryHandle;
class CacheCustomFilter;
class CacheCustomConfig;
class CacheManager;
struct CacheEntry;

using CacheHandleSharedPtr = std::shared_ptr<CacheEntryHandle>;
using CacheConfigSharedPtr = std::shared_ptr<CacheCustomConfig>;
using CacheManagerSharedPtr = std::shared_ptr<CacheManager>;
using FilterWeakPtr = std::weak_ptr<CacheCustomFilter>;
using Hostname = std::string;
using RequestKey = std::string;

enum class InFlightStatus { Leading, Following };

struct DataView {
  const Http::ResponseHeaderMap* headers;
  const Buffer::Instance* body;
};

struct RequestStatus {
  CacheHandleSharedPtr handle;
  InFlightStatus status;
  std::optional<DataView> cached_data;
};

// -----
// Cache entry
// -----

struct CacheEntry {
  struct Data {
    Http::ResponseHeaderMapPtr headers;
    Buffer::InstancePtr body;
  };

  struct Coalescing {
    FilterWeakPtr leader;
    std::vector<FilterWeakPtr> followers;
  };

  Data data;
  Coalescing coalescing;

  alignas(64) bool is_finished{false};
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy