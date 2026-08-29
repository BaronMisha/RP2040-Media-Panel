#pragma once

#include <cstddef>
#include <cstdint>

#include "MediaState.h"

enum class CoverTransferResult : uint8_t {
  Ok,
  BadValue,
  BadMediaId,
  BadOffset,
  Incomplete,
  BadCrc,
};

class RemoteMediaProvider {
 public:
  static constexpr uint16_t CoverWidth = 160;
  static constexpr uint16_t CoverHeight = 160;
  static constexpr std::size_t CoverByteCount =
      static_cast<std::size_t>(CoverWidth) * CoverHeight * 2U;

  void begin(uint32_t nowMs);
  MediaUpdate update(uint32_t nowMs);

  void setMedia(uint32_t mediaId, const char* title,
                const char* artist, const char* album,
                uint32_t durationMs, uint32_t nowMs);
  bool setPlayback(uint32_t positionMs, uint32_t durationMs,
                   uint8_t volume, bool playing, uint32_t nowMs);
  CoverTransferResult beginCover(
      uint32_t mediaId, uint16_t width, uint16_t height,
      uint32_t byteCount, uint32_t expectedCrc32, uint32_t nowMs);
  CoverTransferResult appendCover(
      uint32_t mediaId, uint32_t offset, const uint8_t* data,
      std::size_t dataLength, uint32_t nowMs);
  CoverTransferResult finishCover(
      uint32_t mediaId, uint32_t nowMs, bool& activated);
  bool setKaraoke(uint32_t mediaId,
                  const KaraokeWindow& karaoke,
                  uint32_t nowMs);
  bool clearKaraoke(uint32_t mediaId, uint32_t nowMs);
  bool release();

  bool active() const;
  const MediaState& state() const;

 private:
  void updateKaraokeHighlight();

  MediaState state_{};
  KaraokeWindow remoteKaraoke_{};
  uint32_t mediaId_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint32_t lastContactMs_ = 0;
  uint16_t coverPixels_[2][CoverWidth * CoverHeight]{};
  CoverImage coverImages_[2]{};
  uint32_t activeCoverMediaId_ = 0;
  uint32_t coverMediaId_ = 0;
  uint32_t expectedCoverCrc32_ = 0;
  uint32_t runningCoverCrc32_ = 0xFFFFFFFFU;
  std::size_t receivedCoverBytes_ = 0;
  uint8_t activeCoverIndex_ = 0;
  uint8_t receivingCoverIndex_ = 1;
  bool hasMedia_ = false;
  bool active_ = false;
  bool coverReceiving_ = false;
  bool activeCoverValid_ = false;
};
