#include "cache_entry_handle.h"

#include "common.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheCustom {

// --- Leader ---
void CacheEntryHandle::appendChunk(const Buffer::Instance& chunk, bool end_stream) {
  std::vector<std::pair<CacheCallback, CacheNotification>> to_notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    // Create a copy
    auto segment = std::make_shared<std::vector<uint8_t>>(chunk.length());
    chunk.copyOut(0, chunk.length(), segment->data());
    entry_.data.chunks.push_back(std::move(segment));

    if (end_stream) {
      entry_.is_finished = true;
    }

    auto notification =
        CacheNotification{CacheEvent::Body, nullptr, entry_.data.chunks.back(), end_stream};
    for (auto& cb : entry_.coalescing.followers) {
      to_notify.emplace_back(cb, notification);
    }
  }

  for (auto& [cb, notification] : to_notify) {
    cb(notification);
  }
}

void CacheEntryHandle::setHeaders(const Http::ResponseHeaderMap& headers, bool end_stream) {
  std::vector<std::pair<CacheCallback, CacheNotification>> to_notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    entry_.data.headers = std::make_shared<std::vector<PackedHeader>>();

    // Create a copy
    headers.iterate([this](const Http::HeaderEntry& header) {
      entry_.data.headers->emplace_back(std::string(header.key().getStringView()),
                                        std::string(header.value().getStringView()));
      return Http::HeaderMap::Iterate::Continue;
    });

    if (end_stream) {
      entry_.is_finished = true;
    }

    auto notification =
        CacheNotification{CacheEvent::Headers, entry_.data.headers, nullptr, end_stream};
    for (auto& cb : entry_.coalescing.followers) {
      to_notify.emplace_back(cb, notification);
    }
  }

  for (auto& [cb, notification] : to_notify) {
    cb(notification);
  }
}

InFlightStatus CacheEntryHandle::joinOrStartRequest(CacheCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!entry_.coalescing.leader) {
    entry_.coalescing.leader = true;
    return InFlightStatus::Leading;
  }

  // Replay all data
  if (entry_.data.headers) {
    const bool end_stream = entry_.is_finished && entry_.data.chunks.empty();
    callback({CacheEvent::Headers, entry_.data.headers, nullptr, end_stream});
  }

  for (size_t i = 0; i < entry_.data.chunks.size(); i++) {
    const bool end_stream = entry_.is_finished && (i == entry_.data.chunks.size() - 1);
    callback({CacheEvent::Body, nullptr, entry_.data.chunks[i], end_stream});
  }

  if (!entry_.is_finished) {
    entry_.coalescing.followers.push_back(std::move(callback));
    return InFlightStatus::Following;
  }

  return InFlightStatus::Finished;
}

} // namespace CacheCustom
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy