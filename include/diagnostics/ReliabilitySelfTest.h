#pragma once

#include <cstdint>

class RemoteMediaProvider;

namespace ReliabilitySelfTest {

enum class Result : std::uint8_t {
  Passed,
  InitialState,
  TrackBoundary,
  MultiTrackAdvance,
  PlaylistWrap,
  MillisRollover,
  LongCatchUp,
  KaraokeInitial,
  KaraokeProgress,
  KaraokeUnavailable,
  CoverAtomicity,
  GifAnimation,
};

Result run(RemoteMediaProvider& remoteMediaProvider);
const char* resultName(Result result);

}  // namespace ReliabilitySelfTest
