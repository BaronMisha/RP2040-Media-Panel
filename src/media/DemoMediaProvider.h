#pragma once

#include <cstdint>

#include "MediaState.h"

class DemoMediaProvider {
 public:
  static constexpr uint8_t TrackCount = 3;

  void begin(uint32_t nowMs);
  MediaUpdate update(uint32_t nowMs);
  const MediaState& state() const;

 private:
  void loadTrack(uint8_t trackIndex);
  void updateKaraoke();

  MediaState state_{};
  KaraokeWindow karaokeWindow_{};
  uint32_t lastUpdateMs_ = 0;
};
