#pragma once

#include <TFT_eSPI.h>

#include "MediaState.h"
#include "UiDirtyFlags.h"
#include "display/Utf8TextRenderer.h"

class UiRenderer {
 public:
  explicit UiRenderer(TFT_eSPI& display);

  bool begin();
  void setDiscPhase(uint16_t phase);
  void render(const MediaState& state,
              const UiDirtyFlags& dirtyFlags);
  void drawPrimaryTransitionFrame(
      const MediaState& state, uint16_t layoutPermille);

 private:
  void drawStatusBar(const MediaState& state);
  void drawPrimaryContent(const MediaState& state);
  void drawDisc(const MediaState& state, int16_t x, int16_t y,
                int16_t diameter);
  void renderDiscToBuffer(
      const MediaState& state, uint16_t* buffer,
      int16_t stride, int16_t x, int16_t y,
      int16_t diameter);
  void pushPrimaryFrame();
  void drawTransitionLyrics(
      const KaraokeWindow& karaoke,
      uint16_t visibilityPermille);
  void drawLyricsPanel(const KaraokeWindow& karaoke,
                       int16_t revealWidth);
  void drawKaraokeTextBlock(
      const char* text, int16_t x, int16_t y, int16_t width,
      int16_t height, std::uint8_t fontSize,
      std::uint16_t color, bool highlight,
      std::uint16_t highlightPermille,
      int16_t clipX, int16_t clipWidth,
      bool drawBase = true);
  void drawKaraokeTextBlockOn(
      Utf8TextRenderer& renderer, const char* text,
      int16_t x, int16_t y, int16_t width,
      int16_t height, std::uint8_t fontSize,
      std::uint16_t color, bool highlight,
      std::uint16_t highlightPermille,
      int16_t clipX, int16_t clipY,
      int16_t clipWidth, int16_t clipHeight,
      std::uint16_t highlightColor,
      bool drawBase = true);
  bool hasRenderableCover(const MediaState& state) const;
  void drawMetadata(const MediaState& state);
  void drawTime(const MediaState& state);
  void drawProgress(const MediaState& state);
  void drawPlaybackStatus(const MediaState& state);
  void drawVolume(const MediaState& state);

  TFT_eSPI& display_;
  Utf8TextRenderer textRenderer_;
  TFT_eSprite primarySprite_;
  Utf8TextRenderer spriteTextRenderer_;
  KaraokeWindow lastKaraoke_{};
  uint16_t* primaryFrame_ = nullptr;
  uint16_t discPhase_ = 0;
  bool hasLastKaraoke_ = false;
};