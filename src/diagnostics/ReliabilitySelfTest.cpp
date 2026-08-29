#include "diagnostics/ReliabilitySelfTest.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>

#include "display/GifScreensaver.h"
#include "media/DemoMediaProvider.h"
#include "media/RemoteMediaProvider.h"

namespace ReliabilitySelfTest {
namespace {

bool hasValidPosition(const MediaState& state) {
  return state.durationMs > 0 &&
         state.positionMs < state.durationMs;
}

bool testInitialState() {
  DemoMediaProvider provider;
  provider.begin(1234);
  const MediaState& state = provider.state();

  return state.trackIndex == 0 &&
         state.trackCount == DemoMediaProvider::TrackCount &&
         state.positionMs == 0 && state.durationMs == 20000 &&
         state.playing && state.coverAvailable &&
         state.cover != nullptr;
}

bool testTrackBoundary() {
  DemoMediaProvider provider;
  provider.begin(100);

  if (provider.update(20099) != MediaUpdate::PositionChanged ||
      provider.state().trackIndex != 0 ||
      provider.state().positionMs != 19999) {
    return false;
  }

  return provider.update(20100) == MediaUpdate::TrackChanged &&
         provider.state().trackIndex == 1 &&
         provider.state().positionMs == 0 &&
         hasValidPosition(provider.state());
}

bool testMultiTrackAdvance() {
  DemoMediaProvider provider;
  provider.begin(0);

  return provider.update(45000) == MediaUpdate::TrackChanged &&
         provider.state().trackIndex == 2 &&
         provider.state().positionMs == 1000 &&
         hasValidPosition(provider.state());
}

bool testPlaylistWrap() {
  DemoMediaProvider provider;
  provider.begin(0);

  return provider.update(72000) == MediaUpdate::TrackChanged &&
         provider.state().trackIndex == 0 &&
         provider.state().positionMs == 0 &&
         hasValidPosition(provider.state());
}

bool testMillisRollover() {
  DemoMediaProvider provider;
  constexpr uint32_t StartMs =
      std::numeric_limits<uint32_t>::max() - 1000U;
  provider.begin(StartMs);

  return provider.update(500) == MediaUpdate::PositionChanged &&
         provider.state().trackIndex == 0 &&
         provider.state().positionMs == 1501 &&
         hasValidPosition(provider.state());
}

bool testLongCatchUp() {
  DemoMediaProvider provider;
  provider.begin(0);

  return provider.update(149000) == MediaUpdate::TrackChanged &&
         provider.state().trackIndex == 0 &&
         provider.state().positionMs == 5000 &&
         hasValidPosition(provider.state());
}

bool testKaraokeInitial() {
  DemoMediaProvider provider;
  provider.begin(0);
  const MediaState& state = provider.state();

  return state.karaoke != nullptr &&
         state.karaoke->available &&
         state.karaoke->previousLine[0] == '\0' &&
         state.karaoke->currentLine[0] != '\0' &&
         state.karaoke->nextLine[0] != '\0' &&
         state.karaoke->lineStartMs == 0 &&
         state.karaoke->lineEndMs == 4000 &&
         state.karaoke->highlightPermille == 0;
}

bool testKaraokeProgress() {
  DemoMediaProvider provider;
  provider.begin(0);
  if (provider.state().karaoke == nullptr) {
    return false;
  }

  char firstLine[KaraokeText::CurrentCapacity];
  std::strncpy(firstLine,
               provider.state().karaoke->currentLine,
               sizeof(firstLine) - 1U);
  firstLine[sizeof(firstLine) - 1U] = '\0';

  if (provider.update(2000) != MediaUpdate::PositionChanged ||
      provider.state().karaoke == nullptr ||
      provider.state().karaoke->highlightPermille != 500 ||
      std::strcmp(provider.state().karaoke->currentLine,
                  firstLine) != 0) {
    return false;
  }

  return provider.update(4000) ==
             MediaUpdate::PositionChanged &&
         provider.state().karaoke != nullptr &&
         provider.state().karaoke->lineStartMs == 4000 &&
         provider.state().karaoke->lineEndMs == 8000 &&
         provider.state().karaoke->highlightPermille == 0 &&
         std::strcmp(provider.state().karaoke->previousLine,
                     firstLine) == 0 &&
         std::strcmp(provider.state().karaoke->currentLine,
                     firstLine) != 0;
}

bool testKaraokeUnavailable() {
  DemoMediaProvider provider;
  provider.begin(0);

  return provider.update(20000) ==
             MediaUpdate::TrackChanged &&
         provider.state().trackIndex == 1 &&
         provider.state().karaoke == nullptr;
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

uint32_t filledCoverCrc32(uint8_t value) {
  uint8_t chunk[256];
  std::memset(chunk, value, sizeof(chunk));
  uint32_t crc = 0xFFFFFFFFU;
  std::size_t remaining =
      RemoteMediaProvider::CoverByteCount;
  while (remaining > 0) {
    const std::size_t length =
        std::min(remaining, sizeof(chunk));
    crc = updateCrc32(crc, chunk, length);
    remaining -= length;
  }
  return crc ^ 0xFFFFFFFFU;
}

bool appendFilledCover(RemoteMediaProvider& provider,
                       uint32_t mediaId, uint8_t value,
                       std::size_t byteCount =
                           RemoteMediaProvider::CoverByteCount) {
  uint8_t chunk[256];
  std::memset(chunk, value, sizeof(chunk));
  std::size_t offset = 0;
  while (offset < byteCount) {
    const std::size_t length =
        std::min(byteCount - offset, sizeof(chunk));
    if (provider.appendCover(
            mediaId, static_cast<uint32_t>(offset), chunk,
            length, static_cast<uint32_t>(offset)) !=
        CoverTransferResult::Ok) {
      return false;
    }
    offset += length;
  }
  return true;
}

bool testCoverAtomicity(RemoteMediaProvider& provider) {
  constexpr uint32_t MediaId = 77;
  constexpr uint8_t FirstValue = 0x11;
  constexpr uint8_t SecondValue = 0x33;
  bool activated = false;

  provider.begin(0);
  if (provider.beginCover(
          MediaId, RemoteMediaProvider::CoverWidth,
          RemoteMediaProvider::CoverHeight,
          RemoteMediaProvider::CoverByteCount,
          filledCoverCrc32(FirstValue), 1) !=
          CoverTransferResult::Ok ||
      !appendFilledCover(provider, MediaId, FirstValue) ||
      provider.finishCover(MediaId, 2, activated) !=
          CoverTransferResult::Ok ||
      activated) {
    return false;
  }

  provider.setMedia(
      MediaId, "Title", "Artist", "Album", 60000, 3);
  const CoverImage* firstCover = provider.state().cover;
  if (!provider.state().coverAvailable ||
      firstCover == nullptr ||
      firstCover->pixels[0] != 0x1111U) {
    return false;
  }

  if (provider.beginCover(
          MediaId, RemoteMediaProvider::CoverWidth,
          RemoteMediaProvider::CoverHeight,
          RemoteMediaProvider::CoverByteCount,
          filledCoverCrc32(SecondValue), 4) !=
          CoverTransferResult::Ok ||
      !appendFilledCover(provider, MediaId, SecondValue, 256) ||
      provider.finishCover(MediaId, 5, activated) !=
          CoverTransferResult::Incomplete ||
      provider.state().cover != firstCover ||
      firstCover->pixels[0] != 0x1111U) {
    return false;
  }

  if (provider.beginCover(
          MediaId, RemoteMediaProvider::CoverWidth,
          RemoteMediaProvider::CoverHeight,
          RemoteMediaProvider::CoverByteCount,
          filledCoverCrc32(SecondValue) ^ 1U, 6) !=
          CoverTransferResult::Ok ||
      !appendFilledCover(provider, MediaId, SecondValue) ||
      provider.finishCover(MediaId, 7, activated) !=
          CoverTransferResult::BadCrc ||
      provider.state().cover != firstCover ||
      firstCover->pixels[0] != 0x1111U) {
    return false;
  }

  return provider.beginCover(
             MediaId, RemoteMediaProvider::CoverWidth,
             RemoteMediaProvider::CoverHeight,
             RemoteMediaProvider::CoverByteCount,
             filledCoverCrc32(SecondValue), 8) ==
             CoverTransferResult::Ok &&
         appendFilledCover(provider, MediaId, SecondValue) &&
         provider.finishCover(MediaId, 9, activated) ==
             CoverTransferResult::Ok &&
         activated &&
         provider.state().cover != firstCover &&
         provider.state().cover != nullptr &&
         provider.state().cover->pixels[0] == 0x3333U;
}

}  // namespace

Result run(RemoteMediaProvider& remoteMediaProvider) {
  if (!testInitialState()) {
    return Result::InitialState;
  }
  if (!testTrackBoundary()) {
    return Result::TrackBoundary;
  }
  if (!testMultiTrackAdvance()) {
    return Result::MultiTrackAdvance;
  }
  if (!testPlaylistWrap()) {
    return Result::PlaylistWrap;
  }
  if (!testMillisRollover()) {
    return Result::MillisRollover;
  }
  if (!testLongCatchUp()) {
    return Result::LongCatchUp;
  }
  if (!testKaraokeInitial()) {
    return Result::KaraokeInitial;
  }
  if (!testKaraokeProgress()) {
    return Result::KaraokeProgress;
  }
  if (!testKaraokeUnavailable()) {
    return Result::KaraokeUnavailable;
  }
  if (!testCoverAtomicity(remoteMediaProvider)) {
    return Result::CoverAtomicity;
  }
  if (!GifScreensaver::selfTestAnimation()) {
    return Result::GifAnimation;
  }
  return Result::Passed;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Passed:
      return "PASS";
    case Result::InitialState:
      return "initial state";
    case Result::TrackBoundary:
      return "track boundary";
    case Result::MultiTrackAdvance:
      return "multi-track advance";
    case Result::PlaylistWrap:
      return "playlist wrap";
    case Result::MillisRollover:
      return "millis rollover";
    case Result::LongCatchUp:
      return "long catch-up";
    case Result::KaraokeInitial:
      return "karaoke initial";
    case Result::KaraokeProgress:
      return "karaoke progress";
    case Result::KaraokeUnavailable:
      return "karaoke unavailable";
    case Result::CoverAtomicity:
      return "cover atomicity";
    case Result::GifAnimation:
      return "GIF animation";
  }
  return "unknown";
}

}  // namespace ReliabilitySelfTest
