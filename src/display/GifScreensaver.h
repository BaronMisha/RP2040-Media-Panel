#pragma once

#include <TFT_eSPI.h>

#include <cstdint>

class GifScreensaver {
 public:
  explicit GifScreensaver(TFT_eSPI& display);

  void begin(std::uint32_t nowMs);
  void update(std::uint32_t nowMs);
  std::uint16_t frameIndex() const;

  static bool selfTestAnimation();

 private:
  static constexpr std::int16_t FrameWidth = 240;
  static constexpr std::int16_t FrameHeight = 320;

  void drawFrame();

  TFT_eSPI& display_;
  std::uint16_t rowPixels_[FrameWidth]{};
  std::uint16_t animationFrame_ = 0;
  std::uint32_t lastAnimationFrameMs_ = 0;
};
