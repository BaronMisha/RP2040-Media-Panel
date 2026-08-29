#include "display/GifScreensaver.h"

#include <cstddef>

#include "UiColors.h"
#include "assets/generated/ScreensaverAnimationRle.h"

static_assert(
    ScreensaverAnimationData::FrameWidth ==
        240,
    "Screensaver frame width mismatch");
static_assert(
    ScreensaverAnimationData::FrameHeight ==
        320,
    "Screensaver frame height mismatch");

GifScreensaver::GifScreensaver(TFT_eSPI& display)
    : display_(display) {}

void GifScreensaver::begin(std::uint32_t nowMs) {
  animationFrame_ = 0;
  lastAnimationFrameMs_ = nowMs;
  drawFrame();
}

void GifScreensaver::update(std::uint32_t nowMs) {
  if (nowMs - lastAnimationFrameMs_ <
      ScreensaverAnimationData::FrameDurationsMs[
          animationFrame_]) {
    return;
  }

  lastAnimationFrameMs_ = nowMs;
  animationFrame_ =
      static_cast<std::uint16_t>(
          (animationFrame_ + 1U) %
          ScreensaverAnimationData::FrameCount);
  drawFrame();
}

std::uint16_t GifScreensaver::frameIndex() const {
  return animationFrame_;
}

bool GifScreensaver::selfTestAnimation() {
  if (ScreensaverAnimationData::FrameOffsets[0] != 0 ||
      ScreensaverAnimationData::FrameOffsets[
          ScreensaverAnimationData::FrameCount] !=
          ScreensaverAnimationData::FrameRleDataSize) {
    return false;
  }

  constexpr std::uint32_t ExpectedPixels =
      static_cast<std::uint32_t>(FrameWidth) *
      FrameHeight;
  for (std::uint16_t frame = 0;
       frame < ScreensaverAnimationData::FrameCount;
       ++frame) {
    const std::uint32_t start =
        ScreensaverAnimationData::FrameOffsets[frame];
    const std::uint32_t end =
        ScreensaverAnimationData::FrameOffsets[frame + 1U];
    if (ScreensaverAnimationData::FrameDurationsMs[frame] == 0 ||
        end < start || (end - start) % 3U != 0) {
      return false;
    }

    std::uint32_t decodedPixels = 0;
    for (std::uint32_t offset = start;
         offset < end; offset += 3U) {
      decodedPixels +=
          ScreensaverAnimationData::FrameRleData[offset];
    }
    if (decodedPixels != ExpectedPixels) {
      return false;
    }
  }
  return true;
}

void GifScreensaver::drawFrame() {
  std::uint32_t cursor =
      ScreensaverAnimationData::FrameOffsets[
          animationFrame_];
  const std::uint32_t end =
      ScreensaverAnimationData::FrameOffsets[
          animationFrame_ + 1U];
  std::uint16_t runRemaining = 0;
  std::uint16_t runColor = UiColors::Background;

  display_.startWrite();
  for (std::int16_t row = 0; row < FrameHeight; ++row) {
    std::int16_t column = 0;
    while (column < FrameWidth) {
      if (runRemaining == 0) {
        if (cursor + 2U >= end) {
          runRemaining =
              static_cast<std::uint16_t>(
                  FrameWidth - column);
          runColor = UiColors::Background;
        } else {
          runRemaining =
              ScreensaverAnimationData::FrameRleData[
                  cursor];
          runColor = static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(
                  ScreensaverAnimationData::FrameRleData[
                      cursor + 1U])
                  << 8U |
              ScreensaverAnimationData::FrameRleData[
                  cursor + 2U]);
          cursor += 3U;
        }
      }

      const std::int16_t copied =
          static_cast<std::int16_t>(
              runRemaining <
                      static_cast<std::uint16_t>(
                          FrameWidth - column)
                  ? runRemaining
                  : FrameWidth - column);
      for (std::int16_t index = 0;
           index < copied; ++index) {
        rowPixels_[column + index] = runColor;
      }
      column += copied;
      runRemaining =
          static_cast<std::uint16_t>(
              runRemaining - copied);
    }
    // Re-anchor every row so one transfer error cannot shift the
    // remainder of a full-screen frame.
    display_.setAddrWindow(0, row, FrameWidth, 1);
    display_.pushPixels(rowPixels_, FrameWidth);
  }
  display_.endWrite();
}
