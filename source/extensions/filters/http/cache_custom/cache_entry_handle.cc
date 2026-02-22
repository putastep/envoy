#include "cache_entry_handle.h"

#include "common.h"
#include "envoy/buffer/buffer.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"
#include "cache_filter.h"
#
#include <cstddef>
#include <memory>

#include <utility>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

// --- Leader ---
void CacheEntryHandle::appendChunk(const Buffer::Instance& chunk, bool end_stream) {
  auto shared_chunk = std::make_shared<Envoy::Buffer::OwnedImpl>();
  shared_chunk->add(chunk);

  std::vector<FilterWeakPtr> followers_to_notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!entry_.data.body) {
      entry_.data.body = std::make_unique<Envoy::Buffer::OwnedImpl>();
    }

    entry_.data.body->add(chunk);

    if (end_stream) {
      entry_.is_finished = true;
    }

    followers_to_notify = entry_.coalescing.followers;
  }

  for (auto& follower_ptr : followers_to_notify) {
    if (auto follower = follower_ptr.lock()) {
      follower->recieveBody(shared_chunk, end_stream);
    }
  }
}

void CacheEntryHandle::setHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) {
  auto shared_header = Http::ResponseHeaderMapImpl::create();
  Http::HeaderMapImpl::copyFrom(*shared_header, headers);
  std::shared_ptr<Http::ResponseHeaderMap> shared_header_ptr = std::move(shared_header);

  std::vector<FilterWeakPtr> followers_to_notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    entry_.data.headers = Http::ResponseHeaderMapImpl::create();
    Http::HeaderMapImpl::copyFrom(*entry_.data.headers, headers);

    if (end_stream) {
      entry_.is_finished = true;
    }

    followers_to_notify = entry_.coalescing.followers;
  }

  for (auto& follower_ptr : followers_to_notify) {
    if (auto follower = follower_ptr.lock()) {
      follower->recieveHeaders(shared_header_ptr, end_stream);
    }
  }
}

// --- Follower ---
const DataView CacheEntryHandle::data() {
  return {entry_.data.headers.get(), entry_.data.body.get()};
}

const Http::ResponseHeaderMap& CacheEntryHandle::headers() { return *entry_.data.headers; }

const Envoy::Buffer::InstancePtr& CacheEntryHandle::body() { return entry_.data.body; }

// --- State ---
size_t CacheEntryHandle::totalBytes() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entry_.data.body->length();
}

bool CacheEntryHandle::isComplete() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entry_.is_finished;
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy