#include "RemoteMediaProvider.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include "AppConfig.h"

namespace {

void copyText(char* destination, std::size_t destinationSize,
              const char* source) {
  if (destinationSize == 0) {
    return;
  }

  std::strncpy(destination, source, destinationSize - 1U);
  destination[destinationSize - 1U] = '\0';
}

uint32_t updateCrc32(uint32_t crc, const uint8_t* data,
                     std::size_t dataLength) {
  for (std::size_t index = 0; index < dataLength; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 1U) != 0
                ? (crc >> 1U) ^ 0xEDB88320U
                : crc >> 1U;
    }
  }
  return crc;
}

}  // namespace

void RemoteMediaProvider::begin(uint32_t nowMs) {
  state_ = MediaState{};
  lastUpdateMs_ = nowMs;
  lastContactMs_ = nowMs;
  for (uint8_t index = 0; index < 2; ++index) {
    coverImages_[index].pixels = coverPixels_[index];
    coverImages_[index].width = CoverWidth;
    coverImages_[index].height = CoverHeight;
  }
  remoteKaraoke_ = KaraokeWindow{};
  activeCoverMediaId_ = 0;
  coverMediaId_ = 0;
  activeCoverIndex_ = 0;
  receivingCoverIndex_ = 1;
  coverReceiving_ = false;
  activeCoverValid_ = false;
  hasMedia_ = false;
  active_ = false;
}

MediaUpdate RemoteMediaProvider::update(uint32_t nowMs) {
  const uint32_t deltaMs = nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;

  if (!active_) {
    return MediaUpdate::None;
  }

  if (nowMs - lastContactMs_ >=
      AppConfig::RemoteMediaTimeoutMs) {
    active_ = false;
    return MediaUpdate::TrackChanged;
  }

  if (!state_.playing || deltaMs == 0) {
    return MediaUpdate::None;
  }

  const uint64_t nextPositionMs =
      static_cast<uint64_t>(state_.positionMs) + deltaMs;
  state_.positionMs = state_.durationMs == 0
                          ? static_cast<uint32_t>(nextPositionMs)
                          : static_cast<uint32_t>(
                                std::min<uint64_t>(
                                    nextPositionMs,
                                    state_.durationMs));
  updateKaraokeHighlight();
  return MediaUpdate::PositionChanged;
}

void RemoteMediaProvider::setMedia(
    uint32_t mediaId, const char* title, const char* artist,
    const char* album, uint32_t durationMs, uint32_t nowMs) {
  copyText(state_.title, sizeof(state_.title), title);
  copyText(state_.artist, sizeof(state_.artist), artist);
  copyText(state_.album, sizeof(state_.album), album);

  const bool matchingCover =
      activeCoverValid_ && activeCoverMediaId_ == mediaId;
  remoteKaraoke_ = KaraokeWindow{};
  state_.cover =
      matchingCover ? &coverImages_[activeCoverIndex_] : nullptr;
  state_.karaoke = nullptr;
  state_.positionMs = 0;
  state_.durationMs = durationMs;
  state_.volume = 0;
  state_.trackIndex = 0;
  state_.trackCount = 1;
  state_.playing = false;
  state_.coverAvailable = matchingCover;
  state_.remoteSource = true;

  mediaId_ = mediaId;
  lastUpdateMs_ = nowMs;
  lastContactMs_ = nowMs;
  hasMedia_ = true;
  active_ = true;
}

bool RemoteMediaProvider::setPlayback(
    uint32_t positionMs, uint32_t durationMs, uint8_t volume,
    bool playing, uint32_t nowMs) {
  if (!hasMedia_) {
    return false;
  }

  state_.durationMs = durationMs;
  state_.positionMs =
      durationMs == 0 ? positionMs
                      : std::min(positionMs, durationMs);
  state_.volume = volume;
  state_.playing = playing;
  state_.remoteSource = true;
  updateKaraokeHighlight();

  lastUpdateMs_ = nowMs;
  lastContactMs_ = nowMs;
  active_ = true;
  return true;
}

CoverTransferResult RemoteMediaProvider::beginCover(
    uint32_t mediaId, uint16_t width, uint16_t height,
    uint32_t byteCount, uint32_t expectedCrc32, uint32_t nowMs) {
  if (width != CoverWidth || height != CoverHeight ||
      byteCount != CoverByteCount) {
    return CoverTransferResult::BadValue;
  }

  coverMediaId_ = mediaId;
  receivingCoverIndex_ =
      static_cast<uint8_t>(activeCoverIndex_ ^ 1U);
  expectedCoverCrc32_ = expectedCrc32;
  runningCoverCrc32_ = 0xFFFFFFFFU;
  receivedCoverBytes_ = 0;
  coverReceiving_ = true;
  if (active_) {
    lastContactMs_ = nowMs;
  }
  return CoverTransferResult::Ok;
}

CoverTransferResult RemoteMediaProvider::appendCover(
    uint32_t mediaId, uint32_t offset, const uint8_t* data,
    std::size_t dataLength, uint32_t nowMs) {
  if (!coverReceiving_ || mediaId != coverMediaId_) {
    return CoverTransferResult::BadMediaId;
  }
  if (data == nullptr || dataLength == 0 ||
      offset != receivedCoverBytes_ ||
      receivedCoverBytes_ + dataLength > CoverByteCount) {
    return CoverTransferResult::BadOffset;
  }

  auto* coverBytes =
      reinterpret_cast<uint8_t*>(
          coverPixels_[receivingCoverIndex_]);
  std::memcpy(coverBytes + receivedCoverBytes_, data, dataLength);
  runningCoverCrc32_ =
      updateCrc32(runningCoverCrc32_, data, dataLength);
  receivedCoverBytes_ += dataLength;
  if (active_) {
    lastContactMs_ = nowMs;
  }
  return CoverTransferResult::Ok;
}

CoverTransferResult RemoteMediaProvider::finishCover(
    uint32_t mediaId, uint32_t nowMs, bool& activated) {
  activated = false;
  if (!coverReceiving_ || mediaId != coverMediaId_) {
    return CoverTransferResult::BadMediaId;
  }

  coverReceiving_ = false;
  if (receivedCoverBytes_ != CoverByteCount) {
    return CoverTransferResult::Incomplete;
  }
  if ((runningCoverCrc32_ ^ 0xFFFFFFFFU) !=
      expectedCoverCrc32_) {
    return CoverTransferResult::BadCrc;
  }

  activeCoverIndex_ = receivingCoverIndex_;
  activeCoverMediaId_ = coverMediaId_;
  activeCoverValid_ = true;
  if (hasMedia_ && mediaId_ == activeCoverMediaId_) {
    state_.cover = &coverImages_[activeCoverIndex_];
    state_.coverAvailable = true;
    activated = active_;
    lastContactMs_ = nowMs;
  }
  return CoverTransferResult::Ok;
}

bool RemoteMediaProvider::setKaraoke(
    uint32_t mediaId, const KaraokeWindow& karaoke,
    uint32_t nowMs) {
  if (!hasMedia_ || mediaId != mediaId_ ||
      !karaoke.available ||
      karaoke.currentLine[0] == '\0' ||
      karaoke.lineEndMs <= karaoke.lineStartMs) {
    return false;
  }

  remoteKaraoke_ = karaoke;
  state_.karaoke = &remoteKaraoke_;
  updateKaraokeHighlight();
  lastUpdateMs_ = nowMs;
  lastContactMs_ = nowMs;
  active_ = true;
  return true;
}

bool RemoteMediaProvider::clearKaraoke(
    uint32_t mediaId, uint32_t nowMs) {
  if (!hasMedia_ || mediaId != mediaId_) {
    return false;
  }

  remoteKaraoke_ = KaraokeWindow{};
  state_.karaoke = nullptr;
  lastUpdateMs_ = nowMs;
  lastContactMs_ = nowMs;
  active_ = true;
  return true;
}

bool RemoteMediaProvider::release() {
  const bool wasActive = active_;
  active_ = false;
  return wasActive;
}

bool RemoteMediaProvider::active() const {
  return active_;
}

const MediaState& RemoteMediaProvider::state() const {
  return state_;
}

void RemoteMediaProvider::updateKaraokeHighlight() {
  if (state_.karaoke == nullptr ||
      !remoteKaraoke_.available) {
    return;
  }

  if (state_.positionMs <= remoteKaraoke_.lineStartMs) {
    remoteKaraoke_.highlightPermille = 0;
  } else if (state_.positionMs >=
             remoteKaraoke_.lineEndMs) {
    remoteKaraoke_.highlightPermille = 1000;
  } else {
    remoteKaraoke_.highlightPermille =
        static_cast<uint16_t>(
            static_cast<uint64_t>(
                state_.positionMs -
                remoteKaraoke_.lineStartMs) *
            1000U /
            (remoteKaraoke_.lineEndMs -
             remoteKaraoke_.lineStartMs));
  }
}
