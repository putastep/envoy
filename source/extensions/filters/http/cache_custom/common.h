#pragma once

#include <memory>
#include <vector>
#include <string>
#include "envoy/buffer/buffer.h"

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
struct PackedHeader;
struct CacheNotification;

using CacheHandleSharedPtr = std::shared_ptr<CacheEntryHandle>;
using CacheConfigSharedPtr = std::shared_ptr<CacheCustomConfig>;
using CacheManagerSharedPtr = std::shared_ptr<CacheManager>;
using SharedBuffer = std::shared_ptr<const std::vector<uint8_t>>;
using SharedHeaders = std::shared_ptr<std::vector<PackedHeader>>;
using CacheCallback = std::function<void(const CacheNotification&)>;

using FilterWeakPtr = std::weak_ptr<CacheCustomFilter>;
using Hostname = std::string;
using RequestKey = std::string;

enum class InFlightStatus { Leading, Following, Finished };

struct Data {
  SharedHeaders headers;
  std::vector<SharedBuffer> chunks;
};

enum class CacheEvent { Headers, Body };

struct CacheNotification {
  CacheEvent type;
  SharedHeaders headers; // set if type == Headers
  SharedBuffer body;     // set if type == Body
  bool end_stream;
};

struct PackedHeader {
  const std::string name;
  const std::string value;
};

class CacheBodyFragment : public Buffer::BufferFragment {
public:
  CacheBodyFragment(SharedBuffer data) : data_(std::move(data)) {}

  const void* data() const override { return data_->data(); }
  size_t size() const override { return data_->size(); }

  // Envoy calls this when the buffer is finally drained
  void done() override { delete this; }

private:
  const SharedBuffer data_;
};

// -----
// Cache entry
// -----
struct CacheEntry {
  struct Coalescing {
    bool leader;
    std::vector<CacheCallback> followers;
  };

  Data data;
  Coalescing coalescing;

  alignas(64) bool is_finished{false};
};

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy