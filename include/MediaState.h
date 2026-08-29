#pragma once

#include <cstddef>
#include <cstdint>

#include "CoverImage.h"
#include "KaraokeWindow.h"

namespace MediaText {

constexpr std::size_t TitleCapacity = 96;
constexpr std::size_t ArtistCapacity = 64;
constexpr std::size_t AlbumCapacity = 64;

}  // namespace MediaText

struct MediaState {
  char title[MediaText::TitleCapacity];
  char artist[MediaText::ArtistCapacity];
  char album[MediaText::AlbumCapacity];

  const CoverImage* cover;
  const KaraokeWindow* karaoke;

  uint32_t positionMs;
  uint32_t durationMs;

  uint8_t volume;
  uint8_t trackIndex;
  uint8_t trackCount;

  bool playing;
  bool coverAvailable;
  bool remoteSource;
};

enum class MediaUpdate : uint8_t {
  None,
  PositionChanged,
  TrackChanged
};
