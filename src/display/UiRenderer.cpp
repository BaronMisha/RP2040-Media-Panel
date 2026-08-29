#include "UiRenderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "UiColors.h"
#include "UiLayout.h"
#include "utils/TimeFormat.h"

namespace {

constexpr uint8_t KaraokeMaximumLines = 2;
constexpr std::size_t KaraokeLineCapacity =
    KaraokeText::CurrentCapacity;
constexpr int16_t LyricsPadding = 4;
constexpr int16_t LyricsTextX = UiLayout::LyricsX + LyricsPadding;
constexpr int16_t LyricsTextW = UiLayout::LyricsW - LyricsPadding * 2;
constexpr int16_t LyricsPreviousY = UiLayout::LyricsY + 4;
constexpr int16_t LyricsPreviousH = 48;
constexpr int16_t LyricsCurrentY = UiLayout::LyricsY + 55;
constexpr int16_t LyricsCurrentH = 58;
constexpr int16_t LyricsNextY = UiLayout::LyricsY + 118;
constexpr int16_t LyricsNextH = 38;

constexpr int16_t SinTable[64] = { 0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12, 0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126, -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12 };

int16_t interpolatedSine(uint16_t phase) {
  const uint8_t index = phase >> 10U;
  const uint16_t fraction = phase & 0x03FFU;
  const int16_t current = SinTable[index];
  const int16_t next = SinTable[(index + 1U) & 63U];
  return static_cast<int16_t>(
      current +
      (static_cast<int32_t>(next - current) * fraction) / 1024);
}

uint16_t swapRgb565(uint16_t color) {
  return static_cast<uint16_t>((color >> 8U) | (color << 8U));
}

uint8_t blendStep(uint16_t permille) {
  return static_cast<uint8_t>(
      (std::min<uint16_t>(permille, 1000U) * 32U + 500U) /
      1000U);
}

uint16_t blendRgb565Step(
    uint16_t from, uint16_t to, uint8_t step) {
  const uint8_t inverse = 32U - step;
  const uint16_t red = static_cast<uint16_t>(
      ((((from >> 11U) & 0x1FU) * inverse) +
       (((to >> 11U) & 0x1FU) * step) + 16U) >> 5U);
  const uint16_t green = static_cast<uint16_t>(
      ((((from >> 5U) & 0x3FU) * inverse) +
       (((to >> 5U) & 0x3FU) * step) + 16U) >> 5U);
  const uint16_t blue = static_cast<uint16_t>(
      (((from & 0x1FU) * inverse +
        (to & 0x1FU) * step + 16U) >> 5U));
  return static_cast<uint16_t>(
      (red << 11U) | (green << 5U) | blue);
}

uint16_t blendRgb565(
    uint16_t from, uint16_t to, uint16_t permille) {
  return blendRgb565Step(from, to, blendStep(permille));
}

void blendBufferRect(
    uint16_t* buffer, int16_t stride,
    int16_t x, int16_t y, int16_t width, int16_t height,
    uint16_t targetColor, uint16_t opacityPermille) {
  if (buffer == nullptr || width <= 0 || height <= 0) {
    return;
  }
  const uint8_t step = blendStep(opacityPermille);
  if (step == 0) {
    return;
  }
  for (int16_t row = 0; row < height; ++row) {
    uint16_t* destination =
        buffer + (y + row) * stride + x;
    for (int16_t column = 0; column < width; ++column) {
      const uint16_t current = swapRgb565(destination[column]);
      destination[column] = swapRgb565(
          blendRgb565Step(current, targetColor, step));
    }
  }
}

int16_t interpolateCoordinate(
    int16_t from, int16_t to, uint16_t permille) {
  return static_cast<int16_t>(
      from + static_cast<int32_t>(to - from) * permille / 1000);
}

struct WrappedKaraokeText {
  char lines[KaraokeMaximumLines][KaraokeLineCapacity]{};
  uint8_t lineCount = 0;
};

bool hasLyrics(const MediaState& state) {
  return state.karaoke != nullptr && state.karaoke->available;
}

bool hasSameLyricsText(const KaraokeWindow& left,
                       const KaraokeWindow& right) {
  return std::strcmp(left.previousLine, right.previousLine) == 0 &&
         std::strcmp(left.currentLine, right.currentLine) == 0 &&
         std::strcmp(left.nextLine, right.nextLine) == 0;
}

bool isAsciiSpace(char value) {
  return value == ' ' || value == '\t' || value == '\r' ||
         value == '\n';
}

void copyBytes(char* destination, std::size_t capacity,
               const char* source, std::size_t length) {
  if (capacity == 0) {
    return;
  }
  const std::size_t copied =
      std::min<std::size_t>(length, capacity - 1U);
  if (copied > 0) {
    std::memcpy(destination, source, copied);
  }
  destination[copied] = '\0';
}

void appendEllipsis(char* line, std::size_t capacity) {
  constexpr char Ellipsis[] = "...";
  constexpr std::size_t EllipsisBytes = sizeof(Ellipsis) - 1U;
  std::size_t length = std::strlen(line);
  while (length > 0 && length + EllipsisBytes >= capacity) {
    --length;
    while (length > 0 &&
           (static_cast<uint8_t>(line[length]) & 0xC0U) == 0x80U) {
      --length;
    }
  }
  if (length + EllipsisBytes < capacity) {
    std::memcpy(line + length, Ellipsis, EllipsisBytes + 1U);
  }
}

WrappedKaraokeText wrapKaraokeText(
    const char* text, int16_t maximumWidth,
    uint8_t fontSize, const Utf8TextRenderer& renderer) {
  WrappedKaraokeText wrapped;
  if (text == nullptr || *text == '\0' || maximumWidth <= 0) {
    return wrapped;
  }

  const char* cursor = text;
  while (*cursor != '\0') {
    while (isAsciiSpace(*cursor)) {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }

    const char* wordStart = cursor;
    while (*cursor != '\0' && !isAsciiSpace(*cursor)) {
      ++cursor;
    }
    const std::size_t wordLength =
        static_cast<std::size_t>(cursor - wordStart);
    if (wrapped.lineCount == 0) {
      wrapped.lineCount = 1;
    }

    char* current = wrapped.lines[wrapped.lineCount - 1U];
    const std::size_t currentLength = std::strlen(current);
    char candidate[KaraokeLineCapacity]{};
    copyBytes(candidate, sizeof(candidate), current, currentLength);
    std::size_t candidateLength = std::strlen(candidate);
    if (candidateLength > 0 &&
        candidateLength + 1U < sizeof(candidate)) {
      candidate[candidateLength++] = ' ';
      candidate[candidateLength] = '\0';
    }
    copyBytes(candidate + candidateLength,
              sizeof(candidate) - candidateLength,
              wordStart, wordLength);

    if (currentLength == 0 ||
        renderer.measureText(candidate, fontSize) <= maximumWidth) {
      copyBytes(current, KaraokeLineCapacity, candidate,
                std::strlen(candidate));
      continue;
    }
    if (wrapped.lineCount < KaraokeMaximumLines) {
      ++wrapped.lineCount;
      copyBytes(wrapped.lines[wrapped.lineCount - 1U],
                KaraokeLineCapacity, wordStart, wordLength);
      continue;
    }
    appendEllipsis(current, KaraokeLineCapacity);
    return wrapped;
  }
  return wrapped;
}

}  // namespace

UiRenderer::UiRenderer(TFT_eSPI& display)
    : display_(display),
      textRenderer_(display),
      primarySprite_(&display),
      spriteTextRenderer_(primarySprite_) {}

bool UiRenderer::begin() {
  primarySprite_.setColorDepth(16);
  primaryFrame_ = static_cast<uint16_t*>(
      primarySprite_.createSprite(
          UiLayout::PrimaryW, UiLayout::PrimaryH));
  if (primaryFrame_ == nullptr) {
    return false;
  }
  primarySprite_.fillSprite(UiColors::Background);
  return true;
}

void UiRenderer::setDiscPhase(uint16_t phase) {
  discPhase_ = phase;
}

void UiRenderer::render(const MediaState& state,
                        const UiDirtyFlags& dirtyFlags) {
  if (!dirtyFlags.any()) {
    return;
  }
  if (dirtyFlags.full) {
    display_.fillScreen(UiColors::Background);
  }
  if (dirtyFlags.full || dirtyFlags.status) {
    drawStatusBar(state);
  }
  if (dirtyFlags.full || dirtyFlags.primaryContent) {
    drawPrimaryContent(state);
  } else {
    if (dirtyFlags.disc) {
      if (hasLyrics(state)) {
        drawDisc(state, UiLayout::LyricsDiscX,
                 UiLayout::LyricsDiscY,
                 UiLayout::LyricsDiscDiameter);
      } else {
        drawDisc(state, UiLayout::FullDiscX,
                 UiLayout::FullDiscY,
                 UiLayout::FullDiscDiameter);
      }
    }
    if (dirtyFlags.lyrics && hasLyrics(state)) {
      drawLyricsPanel(*state.karaoke, UiLayout::LyricsW);
    }
  }
  if (dirtyFlags.full || dirtyFlags.metadata) {
    drawMetadata(state);
  }
  if (dirtyFlags.full || dirtyFlags.time) {
    drawTime(state);
  }
  if (dirtyFlags.full || dirtyFlags.progress) {
    drawProgress(state);
  }
  if (dirtyFlags.full || dirtyFlags.playbackStatus) {
    drawPlaybackStatus(state);
  }
  if (dirtyFlags.full || dirtyFlags.volume) {
    drawVolume(state);
  }
}

void UiRenderer::drawPrimaryTransitionFrame(
    const MediaState& state, uint16_t layoutPermille) {
  if (primaryFrame_ == nullptr) {
    return;
  }
  layoutPermille =
      std::min<uint16_t>(layoutPermille, 1000U);
  const int16_t x = interpolateCoordinate(
      UiLayout::FullDiscX, UiLayout::LyricsDiscX,
      layoutPermille);
  const int16_t y = interpolateCoordinate(
      UiLayout::FullDiscY, UiLayout::LyricsDiscY,
      layoutPermille);
  const int16_t diameter = interpolateCoordinate(
      UiLayout::FullDiscDiameter,
      UiLayout::LyricsDiscDiameter, layoutPermille);
  const KaraokeWindow* lyrics = hasLyrics(state)
      ? state.karaoke
      : hasLastKaraoke_ ? &lastKaraoke_ : nullptr;

  primarySprite_.fillSprite(UiColors::Background);
  renderDiscToBuffer(
      state, primaryFrame_, UiLayout::PrimaryW,
      x - UiLayout::PrimaryX, y - UiLayout::PrimaryY,
      diameter);
  if (lyrics != nullptr && layoutPermille > 0) {
    drawTransitionLyrics(*lyrics, layoutPermille);
  }
  pushPrimaryFrame();

  if (hasLyrics(state)) {
    lastKaraoke_ = *state.karaoke;
    hasLastKaraoke_ = true;
  }
}

void UiRenderer::drawStatusBar(const MediaState& state) {
  display_.fillRect(UiLayout::StatusX, UiLayout::StatusY,
                    UiLayout::StatusW, UiLayout::StatusH,
                    UiColors::StatusBackground);
  textRenderer_.drawFitted(
      "MEDIA PANEL RP2040", 6, UiLayout::StatusY, 194,
      UiLayout::StatusH, UiColors::PrimaryText,
      UiColors::StatusBackground, 10);
  display_.setTextFont(1);
  display_.setTextColor(UiColors::Accent,
                        UiColors::StatusBackground);
  display_.setCursor(207, 6);
  display_.print(state.trackIndex + 1U);
  display_.print('/');
  display_.print(state.trackCount);
}

void UiRenderer::drawPrimaryContent(const MediaState& state) {
  display_.fillRect(UiLayout::PrimaryX, UiLayout::PrimaryY,
                    UiLayout::PrimaryW, UiLayout::PrimaryH,
                    UiColors::Background);
  if (hasLyrics(state)) {
    hasLastKaraoke_ = false;
    drawDisc(state, UiLayout::LyricsDiscX,
             UiLayout::LyricsDiscY,
             UiLayout::LyricsDiscDiameter);
    drawLyricsPanel(*state.karaoke, UiLayout::LyricsW);
  } else {
    drawDisc(state, UiLayout::FullDiscX,
             UiLayout::FullDiscY,
             UiLayout::FullDiscDiameter);
  }
}

void UiRenderer::drawDisc(const MediaState& state,
                          int16_t x, int16_t y,
                          int16_t diameter) {
  if (primaryFrame_ == nullptr || diameter <= 0 ||
      diameter > UiLayout::FullDiscDiameter) {
    return;
  }
  renderDiscToBuffer(
      state, primaryFrame_, diameter, 0, 0, diameter);

  const bool previousSwap = display_.getSwapBytes();
  display_.setSwapBytes(false);
  display_.startWrite();
  display_.setAddrWindow(x, y, diameter, diameter);
  display_.pushPixels(
      primaryFrame_, static_cast<uint32_t>(diameter) * diameter);
  display_.endWrite();
  display_.setSwapBytes(previousSwap);
}

void UiRenderer::renderDiscToBuffer(
    const MediaState& state, uint16_t* buffer,
    int16_t stride, int16_t x, int16_t y,
    int16_t diameter) {
  if (buffer == nullptr || stride <= 0 || diameter <= 0 ||
      diameter > UiLayout::FullDiscDiameter || x < 0 || y < 0 ||
      x + diameter > stride) {
    return;
  }
  const int16_t radius = diameter / 2;
  const int32_t radiusSquared =
      static_cast<int32_t>(radius) * radius;
  const int16_t ringRadius = std::max<int16_t>(radius - 2, 0);
  const int32_t ringSquared =
      static_cast<int32_t>(ringRadius) * ringRadius;
  const int16_t hubRadius = std::max<int16_t>(diameter / 10, 4);
  const int16_t holeRadius = std::max<int16_t>(diameter / 24, 2);
  const int32_t hubSquared =
      static_cast<int32_t>(hubRadius) * hubRadius;
  const int32_t holeSquared =
      static_cast<int32_t>(holeRadius) * holeRadius;
  const int16_t sine = interpolatedSine(discPhase_);
  const int16_t cosine = interpolatedSine(
      static_cast<uint16_t>(discPhase_ + 16384U));
  const int32_t coordinateScaleQ16 =
      (160L << 16U) / (127L * diameter);
  const int32_t sourceXStepQ16 =
      static_cast<int32_t>(cosine) * coordinateScaleQ16;
  const int32_t sourceYStepQ16 =
      -static_cast<int32_t>(sine) * coordinateScaleQ16;
  const bool coverAvailable = hasRenderableCover(state);

  for (int16_t row = 0; row < diameter; ++row) {
    const int16_t dy = row - radius;
    int32_t sourceXQ16 =
        (80L << 16U) +
        (static_cast<int32_t>(cosine) * -radius +
         static_cast<int32_t>(sine) * dy) *
            coordinateScaleQ16;
    int32_t sourceYQ16 =
        (80L << 16U) +
        (-static_cast<int32_t>(sine) * -radius +
         static_cast<int32_t>(cosine) * dy) *
            coordinateScaleQ16;
    uint16_t* destination =
        buffer + (y + row) * stride + x;
    for (int16_t column = 0; column < diameter; ++column) {
      const int16_t dx = column - radius;
      const int32_t distanceSquared =
          static_cast<int32_t>(dx) * dx +
          static_cast<int32_t>(dy) * dy;
      uint16_t color = UiColors::Background;
      if (distanceSquared <= radiusSquared) {
        if (coverAvailable) {
          const int16_t sourceX = static_cast<int16_t>(
              sourceXQ16 >> 16U);
          const int16_t sourceY = static_cast<int16_t>(
              sourceYQ16 >> 16U);
          const int16_t clampedX =
              std::min<int16_t>(std::max<int16_t>(sourceX, 0), 159);
          const int16_t clampedY =
              std::min<int16_t>(std::max<int16_t>(sourceY, 0), 159);
          color = state.cover->pixels[
              clampedY * 160 + clampedX];
        } else {
          const uint8_t band = static_cast<uint8_t>(
              (distanceSquared / std::max<int32_t>(radius, 1) +
               (discPhase_ >> 12U)) & 0x0FU);
          color = band < 8 ? UiColors::ControlBackground
                           : UiColors::Accent;
        }
        if (distanceSquared >= ringSquared) {
          color = UiColors::CoverBorder;
        }
        if (distanceSquared <= hubSquared) {
          color = UiColors::ControlBackground;
        }
        if (distanceSquared <= holeSquared) {
          color = UiColors::Background;
        }
      }
      destination[column] = swapRgb565(color);
      sourceXQ16 += sourceXStepQ16;
      sourceYQ16 += sourceYStepQ16;
    }
  }
}

void UiRenderer::pushPrimaryFrame() {
  if (primaryFrame_ == nullptr) {
    return;
  }
  const bool previousSwap = display_.getSwapBytes();
  display_.setSwapBytes(false);
  display_.startWrite();
  display_.setAddrWindow(
      UiLayout::PrimaryX, UiLayout::PrimaryY,
      UiLayout::PrimaryW, UiLayout::PrimaryH);
  display_.pushPixels(
      primaryFrame_,
      static_cast<uint32_t>(UiLayout::PrimaryW) *
          UiLayout::PrimaryH);
  display_.endWrite();
  display_.setSwapBytes(previousSwap);
}

void UiRenderer::drawTransitionLyrics(
    const KaraokeWindow& karaoke,
    uint16_t visibilityPermille) {
  visibilityPermille =
      std::min<uint16_t>(visibilityPermille, 1000U);
  if (primaryFrame_ == nullptr || visibilityPermille == 0) {
    return;
  }
  const int16_t panelX =
      UiLayout::LyricsX - UiLayout::PrimaryX;
  const int16_t panelY =
      UiLayout::LyricsY - UiLayout::PrimaryY;
  const int16_t currentY =
      LyricsCurrentY - UiLayout::PrimaryY;

  blendBufferRect(
      primaryFrame_, UiLayout::PrimaryW,
      panelX, panelY, UiLayout::LyricsW, UiLayout::LyricsH,
      UiColors::StatusBackground, visibilityPermille);
  blendBufferRect(
      primaryFrame_, UiLayout::PrimaryW,
      panelX + 2, currentY,
      UiLayout::LyricsW - 4, LyricsCurrentH,
      UiColors::ControlBackground, visibilityPermille);

  const uint16_t panelColor = blendRgb565(
      UiColors::Background, UiColors::StatusBackground,
      visibilityPermille);
  const uint16_t currentPanelColor = blendRgb565(
      panelColor, UiColors::ControlBackground,
      visibilityPermille);
  const uint16_t secondaryColor = blendRgb565(
      panelColor, UiColors::SecondaryText,
      visibilityPermille);
  const uint16_t primaryColor = blendRgb565(
      currentPanelColor, UiColors::PrimaryText,
      visibilityPermille);
  const uint16_t accentColor = blendRgb565(
      currentPanelColor, UiColors::Accent,
      visibilityPermille);
  const int16_t clipX = panelX;
  const int16_t clipY = panelY;

  drawKaraokeTextBlockOn(
      spriteTextRenderer_, karaoke.previousLine,
      LyricsTextX - UiLayout::PrimaryX,
      LyricsPreviousY - UiLayout::PrimaryY,
      LyricsTextW, LyricsPreviousH, 9,
      secondaryColor, false, 0,
      clipX, clipY, UiLayout::LyricsW,
      UiLayout::LyricsH, accentColor);
  drawKaraokeTextBlockOn(
      spriteTextRenderer_, karaoke.currentLine,
      LyricsTextX - UiLayout::PrimaryX,
      LyricsCurrentY + 2 - UiLayout::PrimaryY,
      LyricsTextW, LyricsCurrentH - 4, 11,
      primaryColor, true, karaoke.highlightPermille,
      clipX, clipY, UiLayout::LyricsW,
      UiLayout::LyricsH, accentColor);
  drawKaraokeTextBlockOn(
      spriteTextRenderer_, karaoke.nextLine,
      LyricsTextX - UiLayout::PrimaryX,
      LyricsNextY - UiLayout::PrimaryY,
      LyricsTextW, LyricsNextH, 9,
      secondaryColor, false, 0,
      clipX, clipY, UiLayout::LyricsW,
      UiLayout::LyricsH, accentColor);
}
void UiRenderer::drawLyricsPanel(
    const KaraokeWindow& karaoke, int16_t revealWidth) {
  revealWidth = std::min<int16_t>(
      std::max<int16_t>(revealWidth, 0), UiLayout::LyricsW);
  if (revealWidth <= 0) {
    return;
  }
  const bool canExtendHighlight =
      revealWidth == UiLayout::LyricsW && hasLastKaraoke_ &&
      hasSameLyricsText(lastKaraoke_, karaoke) &&
      karaoke.highlightPermille >= lastKaraoke_.highlightPermille;
  if (canExtendHighlight) {
    if (karaoke.highlightPermille >
        lastKaraoke_.highlightPermille) {
      drawKaraokeTextBlock(
          karaoke.currentLine, LyricsTextX,
          LyricsCurrentY + 2, LyricsTextW,
          LyricsCurrentH - 4, 11, UiColors::PrimaryText,
          true, karaoke.highlightPermille,
          UiLayout::LyricsX, UiLayout::LyricsW, false);
    }
    lastKaraoke_ = karaoke;
    return;
  }
  display_.fillRect(UiLayout::LyricsX, UiLayout::LyricsY,
                    revealWidth, UiLayout::LyricsH,
                    UiColors::StatusBackground);
  const int16_t clipX = UiLayout::LyricsX;

  drawKaraokeTextBlock(
      karaoke.previousLine, LyricsTextX,
      LyricsPreviousY, LyricsTextW, LyricsPreviousH,
      9, UiColors::SecondaryText, false, 0,
      clipX, revealWidth);
  if (revealWidth > 2) {
    display_.fillRect(
        UiLayout::LyricsX + 2, LyricsCurrentY,
        std::min<int16_t>(UiLayout::LyricsW - 4,
                          revealWidth - 2),
        LyricsCurrentH, UiColors::ControlBackground);
  }
  drawKaraokeTextBlock(
      karaoke.currentLine, LyricsTextX,
      LyricsCurrentY + 2, LyricsTextW, LyricsCurrentH - 4,
      11, UiColors::PrimaryText, true,
      karaoke.highlightPermille, clipX, revealWidth);
  drawKaraokeTextBlock(
      karaoke.nextLine, LyricsTextX,
      LyricsNextY, LyricsTextW, LyricsNextH,
      9, UiColors::SecondaryText, false, 0,
      clipX, revealWidth);
  lastKaraoke_ = karaoke;
  hasLastKaraoke_ = true;
}

void UiRenderer::drawKaraokeTextBlock(
    const char* text, int16_t x, int16_t y, int16_t width,
    int16_t height, std::uint8_t fontSize,
    std::uint16_t color, bool highlight,
    std::uint16_t highlightPermille,
    int16_t clipX, int16_t clipWidth, bool drawBase) {
  drawKaraokeTextBlockOn(
      textRenderer_, text, x, y, width, height, fontSize,
      color, highlight, highlightPermille,
      clipX, UiLayout::LyricsY, clipWidth,
      UiLayout::LyricsH, UiColors::Accent, drawBase);
}

void UiRenderer::drawKaraokeTextBlockOn(
    Utf8TextRenderer& renderer, const char* text,
    int16_t x, int16_t y, int16_t width,
    int16_t height, std::uint8_t fontSize,
    std::uint16_t color, bool highlight,
    std::uint16_t highlightPermille,
    int16_t clipX, int16_t clipY,
    int16_t clipWidth, int16_t clipHeight,
    std::uint16_t highlightColor, bool drawBase) {
  if (clipWidth <= 0 || clipHeight <= 0) {
    return;
  }
  const WrappedKaraokeText wrapped =
      wrapKaraokeText(text, width, fontSize, renderer);
  if (wrapped.lineCount == 0) {
    return;
  }
  const int16_t lineHeight = fontSize + 2;
  const int16_t totalHeight = wrapped.lineCount * lineHeight;
  int16_t lineY = y + std::max<int16_t>(
      (height - totalHeight) / 2, 0);
  int16_t lineWidths[KaraokeMaximumLines]{};
  uint32_t totalTextWidth = 0;
  for (uint8_t index = 0; index < wrapped.lineCount; ++index) {
    lineWidths[index] = std::min<int16_t>(
        width, renderer.measureText(
                   wrapped.lines[index], fontSize));
    totalTextWidth += lineWidths[index];
  }
  const uint32_t highlightedTextWidth =
      totalTextWidth *
      std::min<std::uint16_t>(highlightPermille, 1000U) /
      1000U;
  uint32_t consumedTextWidth = 0;

  for (uint8_t index = 0; index < wrapped.lineCount; ++index) {
    if (drawBase) {
      renderer.drawFixedClipped(
          wrapped.lines[index], x, lineY, width, lineHeight,
          clipX, clipY, clipWidth, clipHeight,
          color, fontSize);
    }
    if (highlight &&
        highlightedTextWidth > consumedTextWidth) {
      const int16_t highlightedLineWidth =
          static_cast<int16_t>(std::min<uint32_t>(
              lineWidths[index],
              highlightedTextWidth - consumedTextWidth));
      const int16_t highlightRight =
          x + highlightedLineWidth;
      const int16_t highlightWidth = std::min<int16_t>(
          clipWidth, highlightRight - clipX);
      if (highlightWidth > 0) {
        renderer.drawFixedClipped(
            wrapped.lines[index], x, lineY, width, lineHeight,
            clipX, clipY, highlightWidth, clipHeight,
            highlightColor, fontSize);
      }
    }
    consumedTextWidth += lineWidths[index];
    lineY += lineHeight;
  }
}
bool UiRenderer::hasRenderableCover(
    const MediaState& state) const {
  return state.coverAvailable && state.cover != nullptr &&
         state.cover->pixels != nullptr &&
         state.cover->width == 160 &&
         state.cover->height == 160;
}
void UiRenderer::drawMetadata(const MediaState& state) {
  constexpr int16_t metadataHeight =
      UiLayout::AlbumY + UiLayout::AlbumH - UiLayout::TitleY;
  display_.fillRect(0, UiLayout::TitleY, UiLayout::ScreenWidth,
                    metadataHeight, UiColors::Background);

  textRenderer_.drawFitted(
      state.title, UiLayout::TitleX, UiLayout::TitleY,
      UiLayout::TitleW, UiLayout::TitleH,
      UiColors::PrimaryText, UiColors::Background, 14);
  textRenderer_.drawFitted(
      state.artist, UiLayout::ArtistX, UiLayout::ArtistY,
      UiLayout::ArtistW, UiLayout::ArtistH,
      UiColors::SecondaryText, UiColors::Background, 14);
  textRenderer_.drawFitted(
      state.album, UiLayout::AlbumX, UiLayout::AlbumY,
      UiLayout::AlbumW, UiLayout::AlbumH,
      UiColors::SecondaryText, UiColors::Background, 12);
}

void UiRenderer::drawTime(const MediaState& state) {
  char positionText[12];
  char durationText[12];

  TimeFormat::formatMmSs(state.positionMs, positionText,
                         sizeof(positionText));
  TimeFormat::formatMmSs(state.durationMs, durationText,
                         sizeof(durationText));

  display_.fillRect(0, UiLayout::TimeY, UiLayout::ScreenWidth, 10,
                    UiColors::Background);
  display_.setTextFont(1);
  display_.setTextColor(UiColors::SecondaryText,
                        UiColors::Background);
  display_.setCursor(12, UiLayout::TimeY);
  display_.print(positionText);
  display_.setCursor(198, UiLayout::TimeY);
  display_.print(durationText);
}

void UiRenderer::drawProgress(const MediaState& state) {
  display_.fillRect(UiLayout::ProgressX, UiLayout::ProgressY,
                    UiLayout::ProgressW, UiLayout::ProgressH,
                    UiColors::ProgressBackground);

  int16_t progressWidth = 0;
  if (state.durationMs > 0) {
    const uint32_t calculatedWidth =
        static_cast<uint32_t>(UiLayout::ProgressW) *
        state.positionMs / state.durationMs;
    progressWidth = static_cast<int16_t>(
        std::min<uint32_t>(calculatedWidth, UiLayout::ProgressW));
  }

  if (progressWidth > 0) {
    display_.fillRect(UiLayout::ProgressX, UiLayout::ProgressY,
                      progressWidth, UiLayout::ProgressH,
                      UiColors::Accent);
  }
}

void UiRenderer::drawPlaybackStatus(const MediaState& state) {
  display_.fillRect(UiLayout::PlaybackStatusX,
                    UiLayout::PlaybackStatusY,
                    UiLayout::PlaybackStatusW,
                    UiLayout::PlaybackStatusH,
                    UiColors::Background);
  display_.drawFastHLine(UiLayout::PlaybackStatusX,
                         UiLayout::PlaybackStatusY + 6, 42,
                         UiColors::ProgressBackground);
  display_.drawFastHLine(UiLayout::PlaybackStatusX +
                             UiLayout::PlaybackStatusW - 42,
                         UiLayout::PlaybackStatusY + 6, 42,
                         UiColors::ProgressBackground);

  const char* statusText = nullptr;
  if (state.playing) {
    if (state.remoteSource) {
      statusText = u8"ВОСПРОИЗВЕДЕНИЕ С ПК";
    } else {
      statusText = u8"АВТОНОМНЫЙ РЕЖИМ";
    }
  } else {
    statusText = u8"ПАУЗА";
  }

  textRenderer_.drawFitted(
      statusText, UiLayout::PlaybackStatusX + 44,
      UiLayout::PlaybackStatusY,
      UiLayout::PlaybackStatusW - 88,
      UiLayout::PlaybackStatusH, UiColors::Accent,
      UiColors::Background, 10, UiTextAlign::Center);
}

void UiRenderer::drawVolume(const MediaState& state) {
  display_.fillRect(0, UiLayout::VolumeY, UiLayout::ScreenWidth,
                    UiLayout::ScreenHeight - UiLayout::VolumeY,
                    UiColors::Background);
  char volumeText[24];
  if (state.volume <= 100) {
    std::snprintf(volumeText, sizeof(volumeText),
                  u8"ГРОМ. %u%%",
                  static_cast<unsigned>(state.volume));
  } else {
    std::snprintf(volumeText, sizeof(volumeText),
                  u8"ГРОМ. --");
  }
  textRenderer_.drawFitted(
      volumeText, UiLayout::VolumeX, UiLayout::VolumeY - 2,
      UiLayout::VolumeBarX - UiLayout::VolumeX - 4, 14,
      UiColors::SecondaryText, UiColors::Background, 8);

  display_.fillRect(UiLayout::VolumeBarX, UiLayout::VolumeBarY,
                    UiLayout::VolumeBarW, UiLayout::VolumeBarH,
                    UiColors::ProgressBackground);
  const int16_t volumeWidth =
      state.volume <= 100
          ? static_cast<int16_t>(
                static_cast<uint32_t>(UiLayout::VolumeBarW) *
                state.volume / 100U)
          : 0;
  display_.fillRect(UiLayout::VolumeBarX, UiLayout::VolumeBarY,
                    volumeWidth, UiLayout::VolumeBarH,
                    UiColors::Accent);
}
